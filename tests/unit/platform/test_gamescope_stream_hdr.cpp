/**
 * @file tests/unit/platform/test_gamescope_stream_hdr.cpp
 * @brief Opt-in tests of HDR capture from a live `gamescope --backend=hermes --hermes-hdr --hdr-enabled`.
 */
#include "../../tests_common.h"

#include <cmath>
#include <cstdlib>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <src/platform/linux/gamescope_stream.h>
#include <src/platform/linux/graphics.h>
#include <src/video.h>
#include <src/video_colorspace.h>
#include <vector>

namespace {
  const char *stream_socket() {
    const char *socket = std::getenv("HERMES_STREAM_TEST_SOCKET");
    return socket && *socket ? socket : nullptr;
  }

  video::config_t stream_config(const char *socket, bool hdr) {
    video::config_t config {};
    config.width = 1920;
    config.height = 1080;
    config.framerate = 60;
    config.dynamicRange = hdr ? 1 : 0;
    config.gamescope_stream_socket = socket;
    return config;
  }

  std::shared_ptr<platf::img_t> capture_one(platf::display_t &display) {
    std::shared_ptr<platf::img_t> frame;
    std::vector<std::shared_ptr<platf::img_t>> pool;
    bool cursor = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {5};
    display.capture(
      [&](std::shared_ptr<platf::img_t> &&img, bool captured) {
        if (captured && img) {
          frame = std::move(img);
          return false;
        }
        return std::chrono::steady_clock::now() < deadline;
      },
      [&](std::shared_ptr<platf::img_t> &out) {
        out = pool.emplace_back(display.alloc_img());
        return true;
      },
      &cursor
    );
    return frame;
  }

  /** Y codes (10-bit) along the frame's middle row, converted as the encoder would. */
  std::vector<int> middle_row_luma(platf::img_t &img, const video::sunshine_colorspace_t &colorspace) {
    auto &descriptor = static_cast<egl::img_descriptor_t &>(img);
    file_t fd {open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC)};
    gbm::gbm_t gbm {gbm::create_device(fd.el)};
    auto display = egl::make_display(gbm.get());
    auto ctx = egl::make_ctx(display.get());
    auto rgb = egl::import_source(display.get(), descriptor.sd);
    auto target = egl::create_target(img.width, img.height, AV_PIX_FMT_P010LE);
    auto sws = egl::sws_t::make(img.width, img.height, img.width, img.height, AV_PIX_FMT_P010LE);
    if (!display || !ctx || !rgb || !target || !sws) {
      ADD_FAILURE() << "EGL import or conversion setup failed";
      return {};
    }
    sws->load_vram(descriptor, 0, 0, (*rgb)->tex[0]);
    sws->apply_colorspace(colorspace);
    sws->convert((*target)->buf);
    std::vector<std::uint16_t> row(img.width);
    gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, (*target)->buf[0]);
    gl::ctx.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl::ctx.ReadPixels(0, img.height / 2, img.width, 1, GL_RED, GL_UNSIGNED_SHORT, row.data());
    std::vector<int> luma;
    for (const auto value : row) {
      luma.push_back(value >> 6);
    }
    return luma;
  }

  double pq_from_nits(double nits) {
    const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
                 c3 = 2392.0 / 4096 * 32;
    const double y = std::pow(nits / 10000, m1);
    return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
  }
}  // namespace

// SDR content goes into PQ as gamma 2.2 at 203 cd/m^2 (Gamescope's SDR-on-HDR brightness). Compared on
// greys, where luma is the signal whatever the matrix; run a static grey ramp in the session (eight bands).
TEST(GamescopeStreamHdr, FramesArePqAndMatchTheSdrPicture) {
  const char *socket = stream_socket();
  if (!socket) {
    GTEST_SKIP() << "Set HERMES_STREAM_TEST_SOCKET to a gamescope started with --hermes-hdr --hdr-enabled";
  }
  ASSERT_EQ(gbm::init(), 0);

  auto sdr = platf::gamescope_stream::display(platf::mem_type_e::vaapi, stream_config(socket, false));
  ASSERT_TRUE(sdr);
  EXPECT_FALSE(sdr->is_hdr());
  auto sdr_frame = capture_one(*sdr);
  ASSERT_TRUE(sdr_frame);
  EXPECT_EQ(static_cast<egl::img_descriptor_t &>(*sdr_frame).sd.fourcc, DRM_FORMAT_XRGB8888);
  const auto sdr_luma = middle_row_luma(*sdr_frame, {video::colorspace_e::rec709, true, 10});
  sdr_frame.reset();
  sdr.reset();

  auto hdr = platf::gamescope_stream::display(platf::mem_type_e::vaapi, stream_config(socket, true));
  ASSERT_TRUE(hdr);
  ASSERT_TRUE(hdr->is_hdr());
  SS_HDR_METADATA metadata;
  ASSERT_TRUE(hdr->get_hdr_metadata(metadata));
  EXPECT_EQ(metadata.displayPrimaries[0].x, 35400);  // BT.2020 red, 0.708
  EXPECT_GT(metadata.maxDisplayLuminance, 0);
  auto hdr_frame = capture_one(*hdr);
  ASSERT_TRUE(hdr_frame);
  EXPECT_EQ(static_cast<egl::img_descriptor_t &>(*hdr_frame).sd.fourcc, DRM_FORMAT_XBGR2101010);
  const auto hdr_luma = middle_row_luma(*hdr_frame, {video::colorspace_e::bt2020, true, 10});

  ASSERT_EQ(sdr_luma.size(), hdr_luma.size());
  for (std::size_t band = 0; band < 8; ++band) {
    const auto x = (2 * band + 1) * sdr_luma.size() / 16;
    const double expected = pq_from_nits(std::pow(sdr_luma[x] / 1023.0, 2.2) * 203) * 1023;
    std::cout << "x " << x << ": SDR " << sdr_luma[x] << ", PQ " << hdr_luma[x] << ", expected " << expected << std::endl;
    // Gamescope's own composite lands SDR white a little low (581 against 594 on RADV), on any backend.
    EXPECT_NEAR(hdr_luma[x], expected, band == 7 ? 16 : 4) << "x " << x;
  }
}

// The capture readiness check: an HDR request takes HEVC Main10 through the shader conversion.
TEST(GamescopeStreamHdr, CaptureReadinessEncodesHdr) {
  const char *socket = stream_socket();
  if (!socket) {
    GTEST_SKIP() << "Set HERMES_STREAM_TEST_SOCKET to a gamescope started with --hermes-hdr --hdr-enabled";
  }
  ASSERT_EQ(video::probe_encoders_without_display(), 0);
  auto config = stream_config(socket, true);
  std::string failure;
  ASSERT_EQ(video::check_capture_path(config, failure), 0) << failure;
  EXPECT_TRUE(video::last_capture_readiness().frame_seen);
}
