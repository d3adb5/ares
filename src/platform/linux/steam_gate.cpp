/**
 * @file src/platform/linux/steam_gate.cpp
 * @brief fanotify gate on Steam's launcher.
 */
#include "steam_gate.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/capability.h>
#include <sys/eventfd.h>
#include <sys/fanotify.h>
#include <thread>
#include <unistd.h>

#include "src/logging.h"

namespace platf::steam_gate {
  namespace {
    /// CAP_SYS_ADMIN in effect on this thread only while `run` does.
    template<class F>
    auto with_sys_admin(F run) {
      cap_t caps = cap_get_proc();
      cap_value_t sys_admin = CAP_SYS_ADMIN;
      const bool raised = caps && cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_admin, CAP_SET) == 0 && cap_set_proc(caps) == 0;
      auto result = run();
      if (raised) {
        cap_set_flag(caps, CAP_EFFECTIVE, 1, &sys_admin, CAP_CLEAR);
        cap_set_proc(caps);
      }
      if (caps) {
        cap_free(caps);
      }
      return result;
    }

    std::string fd_path(int fd) {
      char link[PATH_MAX];
      const auto proc = "/proc/self/fd/" + std::to_string(fd);
      const auto size = ::readlink(proc.c_str(), link, sizeof(link) - 1);
      return size > 0 ? std::string {link, static_cast<std::size_t>(size)} : std::string {};
    }

    /// Shared with the deciders, which may outlive the gate.
    struct group_t {
      std::mutex mutex;
      int fd;
      std::function<void(pid_t)> decide;

      void allow(int event_fd) {
        std::lock_guard lock {mutex};
        if (fd >= 0) {
          fanotify_response response {event_fd, FAN_ALLOW};
          (void) !::write(fd, &response, sizeof(response));
        }
        ::close(event_fd);
      }

      void close() {
        std::lock_guard lock {mutex};
        ::close(fd);  // what is still held goes on
        fd = -1;
      }
    };

    class fanotify_gate_t final: public gate_t {
    public:
      fanotify_gate_t(int fd, int stop, std::string path, std::function<void(pid_t)> decide):
          group {std::make_shared<group_t>()},
          stop {stop},
          path {std::move(path)} {
        group->fd = fd;
        group->decide = std::move(decide);
        reader = std::thread {[this]() {
          read_events();
        }};
      }

      ~fanotify_gate_t() override {
        const std::uint64_t one = 1;
        (void) !::write(stop, &one, sizeof(one));
        reader.join();
        group->close();
        ::close(stop);
      }

    private:
      void read_events() {
        alignas(fanotify_event_metadata) char buffer[4096];
        for (;;) {
          pollfd fds[] {{group->fd, POLLIN, 0}, {stop, POLLIN, 0}};
          if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
              continue;
            }
            return;
          }
          if (fds[1].revents) {
            return;
          }
          const auto length = ::read(group->fd, buffer, sizeof(buffer));
          if (length <= 0) {
            if (length < 0 && (errno == EINTR || errno == EAGAIN)) {
              continue;
            }
            return;
          }
          auto *event = reinterpret_cast<fanotify_event_metadata *>(buffer);
          for (auto left = length; FAN_EVENT_OK(event, left); event = FAN_EVENT_NEXT(event, left)) {
            if (event->fd < 0) {
              continue;
            }
            if (!(event->mask & FAN_OPEN_EXEC_PERM) || fd_path(event->fd) != path) {
              group->allow(event->fd);
              continue;
            }
            std::thread {[group = group, pid = static_cast<pid_t>(event->pid), event_fd = event->fd]() {
              try {
                group->decide(pid);
              } catch (const std::exception &e) {
                BOOST_LOG(warning) << "[SteamGate] " << e.what();
              }
              group->allow(event_fd);
            }}.detach();
          }
        }
      }

      std::shared_ptr<group_t> group;
      int stop;
      std::string path;
      std::thread reader;
    };
  }  // namespace

  std::unique_ptr<gate_t> start(const std::string &dir, const std::string &file, std::function<void(pid_t)> decide) {
    char resolved[PATH_MAX];
    if (!::realpath((dir + '/' + file).c_str(), resolved)) {
      BOOST_LOG(info) << "[SteamGate] No " << dir << '/' << file << "; desktop starts of Steam are not watched.";
      return nullptr;
    }
    const int fd = with_sys_admin([]() {
      return ::fanotify_init(FAN_CLASS_CONTENT | FAN_CLOEXEC, O_RDONLY | O_CLOEXEC | O_LARGEFILE);
    });
    if (fd < 0) {
      BOOST_LOG(warning) << "[SteamGate] fanotify: " << std::strerror(errno)
                         << "; a desktop start of Steam goes to the Steam Ares keeps running (needs CAP_SYS_ADMIN).";
      return nullptr;
    }
    const std::string target {resolved};
    const auto parent = target.substr(0, target.rfind('/'));
    // The directory, not the file: a package update replaces the file and would drop an inode mark.
    if (::fanotify_mark(fd, FAN_MARK_ADD, FAN_OPEN_EXEC_PERM | FAN_EVENT_ON_CHILD, AT_FDCWD, parent.c_str()) != 0) {
      BOOST_LOG(warning) << "[SteamGate] fanotify_mark " << parent << ": " << std::strerror(errno);
      ::close(fd);
      return nullptr;
    }
    const int stop = ::eventfd(0, EFD_CLOEXEC);
    if (stop < 0) {
      ::close(fd);
      return nullptr;
    }
    BOOST_LOG(info) << "[SteamGate] Watching starts of " << target;
    return std::make_unique<fanotify_gate_t>(fd, stop, target, std::move(decide));
  }
}  // namespace platf::steam_gate
