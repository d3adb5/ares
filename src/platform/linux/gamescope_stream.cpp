/**
 * @file src/platform/linux/gamescope_stream.cpp
 * @brief Zero-copy capture from a Gamescope running `--backend ares`.
 *
 * Gamescope composites into a pool of dma-bufs it shares over a Unix socket
 * (ares_stream_v1.h) and sends a fence with each frame. The frames take the
 * same EGL import and VA-API path as a Hermes-KMS capture, and their colour
 * (SDR, or PQ/BT.2020 with HDR metadata) the same snapshot type.
 */
#include "gamescope_stream.h"

#include "graphics.h"
#include "hermes_kms_color.h"
#include "ares_stream_v1.h"
#include "misc.h"
#include "src/logging.h"
#include "src/video.h"
#include "vaapi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <limits>
#include <map>
#include <optional>
#include <thread>
#include <poll.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

using namespace std::literals;

namespace platf::gamescope_stream {
  namespace {
    std::mutex sessions_mutex;
    std::map<std::string, std::shared_ptr<session_t>> sessions;

    // Enough for capture, the encoder's mailbox and one being imported.
    constexpr std::uint32_t pool_slots = 4;

    namespace color = VDISPLAY::hermes_kms;

    color::color_t make_color(bool pq) {
      color::color_t result;
      result.flags = color::color_valid | color::color_rgb_full_range;
      if (pq) {
        result.flags |= color::color_hdr_valid;
        result.colorspace = color::colorspace_bt2020_rgb;
        result.eotf = 2;  // SMPTE ST 2084
      }
      return result;
    }

    color::color_t color_of(const ares_server_frame &frame) {
      auto result = make_color(frame.transfer == ARES_TRANSFER_PQ && frame.primaries == ARES_PRIMARIES_BT2020);
      if (result.pq() && (frame.flags & ARES_FRAME_FLAG_HDR)) {
        for (int i = 0; i < 3; ++i) {
          result.primaries[i] = {frame.hdr_display_primaries_x[i], frame.hdr_display_primaries_y[i]};
        }
        result.white_point = {frame.hdr_white_point_x, frame.hdr_white_point_y};
        result.max_luminance = frame.hdr_max_display_mastering_luminance;
        result.min_luminance = frame.hdr_min_display_mastering_luminance;
        result.max_cll = frame.hdr_max_cll;
        result.max_fall = frame.hdr_max_fall;
      }
      return result;
    }

    template<typename T>
    T make_message(std::uint16_t type) {
      T message {};
      message.hdr.type = type;
      message.hdr.size = sizeof(T);
      return message;
    }

    bool poll_fd(int fd, short events, int timeout_ms) {
      pollfd entry {fd, events, 0};
      for (;;) {
        const int result = ::poll(&entry, 1, timeout_ms);
        if (result >= 0) {
          return result > 0;
        }
        if (errno != EINTR) {
          return false;
        }
      }
    }

    /** A slot the client owns until the image reading it is done with. */
    struct lease_t {
      std::shared_ptr<img_t> image;
      std::uint64_t sequence;
      std::uint32_t pool_id;
      std::uint32_t slot;
    };

    struct slot_t {
      file_t fd;
      std::uint32_t offset {};
      std::uint32_t pitch {};
    };

    struct frame_message_t {
      ares_server_frame message;
      file_t fence;
    };

    class stream_display_t final: public display_t {
    public:
      ~stream_display_t() override {
        leases.clear();
        if (session) {
          session->detach(connection.el);
        }
      }

      bool init(mem_type_e memory, const video::config_t &config) {
        if (memory != mem_type_e::vaapi) {
          BOOST_LOG(::error) << "Gamescope stream capture needs a VA-API encoder";
          return false;
        }
        probe_only = config.encoder_probe;
        hdr = config.dynamicRange != 0;

        encode_render_fd.el = open_encode_render_node();
        if (encode_render_fd.el < 0 || !va::validate(encode_render_fd.el)) {
          BOOST_LOG(::error) << "Gamescope stream capture: no VA-API GPU to encode on";
          return false;
        }

        // A client can ask for its panel's real rate, which is seldom a whole number.
        refresh_mhz = config.encodingFramerate > 1000 ? static_cast<std::uint32_t>(config.encodingFramerate) : 0;

        if (!connect_socket(config.gamescope_stream_socket) || !read_hello()) {
          return false;
        }
        // A probe only asks what a stream would get. Closing before HELLO leaves the
        // current stream and the HDR offered to games alone.
        if (probe_only) {
          return true;
        }
        if (!configure() || !read_first_frame()) {
          return false;
        }
        session = session_for(config.gamescope_stream_socket);
        session->attach(connection.el);

        BOOST_LOG(info) << "Gamescope stream capture: " << width << 'x' << height << (color.pq() ? " HDR" : "")
                        << " on " << config.gamescope_stream_socket;
        return true;
      }

      bool is_hdr() override {
        return color.pq();
      }

      bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
        return color.copy_hdr_metadata(metadata);
      }

      std::shared_ptr<img_t> alloc_img() override {
        auto image = std::make_shared<egl::img_descriptor_t>();
        image->width = width;
        image->height = height;
        image->serial = std::numeric_limits<decltype(image->serial)>::max();
        image->data = nullptr;
        image->pixel_pitch = 4;
        image->sequence = 0;
        std::fill_n(image->sd.fds, 4, -1);
        return image;
      }

      int dummy_img(img_t *) override {
        // A zero sequence number marks a dummy.
        return 0;
      }

      std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e) override {
        return va::make_stream_encode_device(width, height, dup(encode_render_fd.el), converted);
      }

      capture_e capture(const push_captured_image_cb_t &push, const pull_free_image_cb_t &pull, bool *) override {
        if (probe_only) {
          return capture_e::error;
        }

        // Gamescope paints only for a change, and a static scene would hold back
        // both the pool and the first frame.
        auto request = make_message<ares_client_request_frame>(ARES_CLIENT_REQUEST_FRAME);
        if (!send_message(&request, sizeof(request))) {
          return capture_e::error;
        }

        for (;;) {
          release_finished();

          // Gamescope exits with its app. Ending capture now would drop the client with an error before
          // the session's process watch tells it the app finished, so stay until that happens.
          if (gamescope_gone) {
            if (std::chrono::steady_clock::now() - *gamescope_gone > 5s) {
              BOOST_LOG(::error) << "Gamescope stream capture: Gamescope closed the connection";
              return capture_e::error;
            }
            std::this_thread::sleep_for(50ms);
            if (!push(nullptr, false)) {
              return capture_e::ok;
            }
            continue;
          }

          // The next release is near once the encoder is past a slot; nothing wakes this otherwise.
          const auto reading = converted->load();
          const bool releasing = std::ranges::any_of(leases, [&](const lease_t &lease) {
            return lease.sequence != reading;
          });
          if (!poll_fd(connection.el, POLLIN, releasing ? 1 : 100)) {
            if (!push(nullptr, false)) {
              return capture_e::ok;
            }
            continue;
          }

          std::optional<frame_message_t> frame;
          if (const auto status = read_message(frame)) {
            return *status;
          }
          if (frame) {
            if (const auto status = on_frame(frame->message, std::move(frame->fence), push, pull)) {
              return *status;
            }
          }
        }
      }

    private:
      bool connect_socket(const std::string &path) {
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        if (path.empty() || path.size() >= sizeof(address.sun_path)) {
          BOOST_LOG(::error) << "Gamescope stream capture: bad socket path [" << path << ']';
          return false;
        }
        std::memcpy(address.sun_path, path.c_str(), path.size());

        connection.el = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (connection.el < 0 ||
            ::connect(connection.el, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
          BOOST_LOG(::error) << "Gamescope stream capture: cannot connect to " << path << ": " << std::strerror(errno);
          return false;
        }

        // A stuck Gamescope must not hold the input or capture thread for long.
        timeval timeout {0, 100'000};
        ::setsockopt(connection.el, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        return true;
      }

      bool read_hello() {
        ares_server_hello hello {};
        if (!poll_fd(connection.el, POLLIN, 2000) ||
            ::recv(connection.el, &hello, sizeof(hello), 0) != static_cast<ssize_t>(sizeof(hello)) ||
            hello.hdr.type != ARES_SERVER_HELLO) {
          BOOST_LOG(::error) << "Gamescope stream capture: no greeting from Gamescope";
          return false;
        }
        if (hello.version_major != ARES_STREAM_VERSION_MAJOR) {
          BOOST_LOG(::error) << "Gamescope stream capture: protocol " << hello.version_major << ", expected "
                             << ARES_STREAM_VERSION_MAJOR;
          return false;
        }
        width = env_width = static_cast<int>(hello.width);
        height = env_height = static_cast<int>(hello.height);
        offset_x = offset_y = 0;

        struct stat render_stat {};
        if (hello.render_dev_major && ::fstat(encode_render_fd.el, &render_stat) == 0 &&
            (major(render_stat.st_rdev) != hello.render_dev_major || minor(render_stat.st_rdev) != hello.render_dev_minor)) {
          BOOST_LOG(warning) << "Gamescope composites on a different GPU than Ares encodes on; frames will cross devices";
        }
        // Zero from a 1.0 Gamescope, which offers HDR only once configured.
        hello_pq = hello.state_flags & ARES_STATE_HDR_ACTIVE;
        color = make_color(hello_pq);
        return true;
      }

      bool configure() {
        auto client_hello = make_message<ares_client_hello>(ARES_CLIENT_HELLO);
        client_hello.version_major = ARES_STREAM_VERSION_MAJOR;
        client_hello.version_minor = ARES_STREAM_VERSION_MINOR;
        std::strncpy(client_hello.client_name, "ares", sizeof(client_hello.client_name) - 1);
        if (!send_message(&client_hello, sizeof(client_hello))) {
          return false;
        }

        // Linear: every GPU's EGL imports it, and Gamescope only writes it once. PQ needs
        // ten bits; 8-bit stays second, for a GPU that cannot export them.
        struct {
          ares_client_configure configure;
          ares_format_modifier formats[2];
        } request {};
        request.configure.hdr.type = ARES_CLIENT_CONFIGURE;
        request.configure.refresh_mhz = refresh_mhz;
        request.configure.slot_count = pool_slots;
        request.configure.flags = hdr ? ARES_CONFIGURE_HDR : 0;
        request.configure.num_formats = 0;
        if (hdr) {
          request.formats[request.configure.num_formats++] = {DRM_FORMAT_XBGR2101010, 0, DRM_FORMAT_MOD_LINEAR};
        }
        request.formats[request.configure.num_formats++] = {DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR};
        request.configure.hdr.size = sizeof(request.configure) + request.configure.num_formats * sizeof(ares_format_modifier);
        return send_message(&request, request.configure.hdr.size);
      }

      /**
       * The encoder is built for the colour of the first frame. One painted before Gamescope took this
       * CONFIGURE can still have the old colour, so wait briefly for one that matches the request.
       */
      bool read_first_frame() {
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        bool seen = false;
        for (auto now = std::chrono::steady_clock::now(); now < deadline; now = std::chrono::steady_clock::now()) {
          const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
          if (!poll_fd(connection.el, POLLIN, static_cast<int>(left) + 1)) {
            break;
          }
          std::optional<frame_message_t> frame;
          if (read_message(frame) || gamescope_gone) {
            BOOST_LOG(::error) << "Gamescope stream capture: no first frame";
            return false;
          }
          if (!frame) {
            continue;
          }
          seen = true;
          color = color_of(frame->message);
          release(frame->message.pool_id, frame->message.slot);
          if (color.pq() == hdr) {
            return true;
          }
          auto request = make_message<ares_client_request_frame>(ARES_CLIENT_REQUEST_FRAME);
          send_message(&request, sizeof(request));
        }
        if (!seen) {
          // Nothing painted yet: guess from HELLO; a different first frame rebuilds capture.
          color = make_color(hdr && hello_pq);
        }
        return true;
      }

      bool send_message(const void *message, std::size_t size) {
        const auto sent = ::send(connection.el, message, size, MSG_NOSIGNAL);
        if (sent != static_cast<ssize_t>(size)) {
          BOOST_LOG(::error) << "Gamescope stream capture: send failed: " << std::strerror(errno);
          return false;
        }
        return true;
      }

      // Nothing to report means keep capturing. A FRAME is handed back in frame.
      std::optional<capture_e> read_message(std::optional<frame_message_t> &frame) {
        std::array<std::uint8_t, ARES_MAX_MESSAGE_SIZE> data;
        std::array<char, CMSG_SPACE(sizeof(int))> control;
        iovec io {data.data(), data.size()};
        msghdr message {};
        message.msg_iov = &io;
        message.msg_iovlen = 1;
        message.msg_control = control.data();
        message.msg_controllen = control.size();

        const auto size = ::recvmsg(connection.el, &message, MSG_CMSG_CLOEXEC);
        if (size <= 0) {
          BOOST_LOG(info) << "Gamescope stream capture: Gamescope closed the connection";
          gamescope_gone = std::chrono::steady_clock::now();
          return std::nullopt;
        }

        file_t received;
        for (auto *header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
          if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
            int fd;
            std::memcpy(&fd, CMSG_DATA(header), sizeof(fd));
            received.el = fd;
          }
        }

        if (static_cast<std::size_t>(size) < sizeof(ares_msg_header)) {
          return capture_e::error;
        }
        ares_msg_header header;
        std::memcpy(&header, data.data(), sizeof(header));

        const auto view = [&]<typename T>(T &out) {
          if (static_cast<std::size_t>(size) < sizeof(T)) {
            return false;
          }
          std::memcpy(&out, data.data(), sizeof(T));
          return true;
        };

        switch (header.type) {
          case ARES_SERVER_POOL: {
            ares_server_pool pool;
            if (!view(pool)) {
              return capture_e::error;
            }
            if (static_cast<int>(pool.width) != width || static_cast<int>(pool.height) != height) {
              return capture_e::reinit;
            }
            current_pool = pool.pool_id;
            fourcc = pool.drm_format;
            modifier = pool.modifier;
            slots.clear();
            slots.resize(pool.slot_count);
            return std::nullopt;
          }
          case ARES_SERVER_BUFFER: {
            ares_server_buffer buffer;
            if (!view(buffer) || buffer.pool_id != current_pool || buffer.slot >= slots.size() || received.el < 0) {
              return capture_e::error;
            }
            slots[buffer.slot] = {std::move(received), buffer.offsets[0], buffer.strides[0]};
            return std::nullopt;
          }
          case ARES_SERVER_FRAME: {
            ares_server_frame message;
            if (!view(message)) {
              return capture_e::error;
            }
            frame.emplace(message, std::move(received));
            return std::nullopt;
          }
          case ARES_SERVER_ERROR: {
            ares_server_error error;
            if (!view(error)) {
              return capture_e::error;
            }
            error.message[sizeof(error.message) - 1] = '\0';
            BOOST_LOG(::error) << "Gamescope stream capture: " << error.message;
            return error.fatal ? std::optional {capture_e::error} : std::nullopt;
          }
          default:
            return std::nullopt;
        }
      }

      std::optional<capture_e> on_frame(const ares_server_frame &frame, file_t fence, const push_captured_image_cb_t &push, const pull_free_image_cb_t &pull) {
        if (frame.pool_id != current_pool || frame.slot >= slots.size() || slots[frame.slot].fd.el < 0) {
          return capture_e::error;
        }

        // A display's colour is fixed, as its encoder and the client's HDR state were set up for it.
        if (!(color_of(frame) == color)) {
          BOOST_LOG(info) << "Gamescope stream capture: the frames' colour changed";
          release(frame.pool_id, frame.slot);
          return capture_e::reinit;
        }

        if (fence.el >= 0 && !poll_fd(fence.el, POLLIN, 1000)) {
          BOOST_LOG(warning) << "Gamescope stream capture: frame fence timed out";
          release(frame.pool_id, frame.slot);
          return std::nullopt;
        }

        std::shared_ptr<img_t> image;
        if (!pull(image)) {
          release(frame.pool_id, frame.slot);
          return capture_e::interrupted;
        }

        auto *descriptor = static_cast<egl::img_descriptor_t *>(image.get());
        descriptor->reset();
        descriptor->sd.width = width;
        descriptor->sd.height = height;
        descriptor->sd.fourcc = fourcc;
        descriptor->sd.modifier = modifier;
        std::fill_n(descriptor->sd.offsets, 4, 0);
        std::fill_n(descriptor->sd.pitches, 4, 0);
        descriptor->sd.fds[0] = ::fcntl(slots[frame.slot].fd.el, F_DUPFD_CLOEXEC, 0);
        descriptor->sd.offsets[0] = static_cast<int>(slots[frame.slot].offset);
        descriptor->sd.pitches[0] = static_cast<int>(slots[frame.slot].pitch);
        descriptor->sequence = ++sequence;
        // Both clocks are CLOCK_MONOTONIC. It counts the composite as host time.
        descriptor->frame_timestamp = std::chrono::steady_clock::time_point {std::chrono::nanoseconds {frame.submit_time_ns}};

        leases.push_back({image, sequence, frame.pool_id, frame.slot});
        if (!push(std::move(image), true)) {
          return capture_e::ok;
        }
        return std::nullopt;
      }

      void release(std::uint32_t pool_id, std::uint32_t slot) {
        auto message = make_message<ares_client_release>(ARES_CLIENT_RELEASE);
        message.pool_id = pool_id;
        message.slot = slot;
        send_message(&message, sizeof(message));
      }

      // A slot goes back once the encoder is past it: its image is free and is not the one it
      // last started on, which it reads until the next begins (the VCN reads the buffer directly,
      // and the last picture is encoded again when nothing new comes). A dma-buf polls writable
      // when all its fences have signalled.
      void release_finished() {
        const auto reading = converted->load();
        std::erase_if(leases, [&](const lease_t &lease) {
          if (lease.image.use_count() > 2 || lease.sequence == reading) {
            return false;
          }
          if (lease.pool_id == current_pool && lease.slot < slots.size() &&
              !poll_fd(slots[lease.slot].fd.el, POLLOUT, 0)) {
            return false;
          }
          static_cast<egl::img_descriptor_t *>(lease.image.get())->reset();
          if (lease.pool_id == current_pool) {
            release(lease.pool_id, lease.slot);
          }
          return true;
        });
      }

      bool probe_only {false};
      bool hdr {false};  ///< Asked Gamescope for PQ.
      bool hello_pq {false};
      color::color_t color;
      std::optional<std::chrono::steady_clock::time_point> gamescope_gone;
      std::uint32_t refresh_mhz {};
      file_t connection;
      file_t encode_render_fd;
      std::shared_ptr<session_t> session;
      std::uint32_t current_pool {};
      std::uint32_t fourcc {};
      std::uint64_t modifier {};
      std::vector<slot_t> slots;
      std::deque<lease_t> leases;
      std::uint64_t sequence {};
      std::shared_ptr<std::atomic_uint64_t> converted {std::make_shared<std::atomic_uint64_t>(0)};
    };
  }  // namespace

  void session_t::attach(int new_fd) {
    std::lock_guard lock {mutex};
    fd = new_fd;
  }

  void session_t::detach(int old_fd) {
    std::lock_guard lock {mutex};
    if (fd == old_fd) {
      fd = -1;
    }
  }

  bool session_t::send(const void *message, std::size_t size) {
    std::lock_guard lock {mutex};
    return fd >= 0 && ::send(fd, message, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size);
  }

  std::shared_ptr<session_t> session_for(const std::string &socket) {
    std::lock_guard lock {sessions_mutex};
    auto &session = sessions[socket];
    if (!session) {
      session = std::make_shared<session_t>();
    }
    return session;
  }

  void forget_session(const std::string &socket) {
    std::lock_guard lock {sessions_mutex};
    sessions.erase(socket);
  }

  std::shared_ptr<display_t> display(mem_type_e memory, const video::config_t &config) {
    auto display = std::make_shared<stream_display_t>();
    if (!display->init(memory, config)) {
      return nullptr;
    }
    return display;
  }
}  // namespace platf::gamescope_stream
