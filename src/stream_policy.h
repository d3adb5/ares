/**
 * @file src/stream_policy.h
 * @brief Small decisions of a stream that depend only on time: FEC while the link is clean, encoding a still picture.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace stream_policy {
  using clock = std::chrono::steady_clock;

  /**
   * FEC that follows the client's loss reports: the configured percentage is the ceiling, used for the first 10 s
   * and for 30 s after a loss, a key frame request or a reference invalidation; the floor otherwise.
   * loss() comes from the control thread, percentage() from the video one.
   */
  class adaptive_fec_t {
  public:
    static constexpr int FLOOR = 5;
    static constexpr std::chrono::seconds START {10};
    static constexpr std::chrono::seconds HOLD {30};

    explicit adaptive_fec_t(clock::time_point now = clock::now()):
        careful_until {(now + START).time_since_epoch().count()} {}

    void loss(clock::time_point now = clock::now()) {
      careful_until.store((now + HOLD).time_since_epoch().count());
    }

    int percentage(int ceiling, clock::time_point now = clock::now()) const {
      return now.time_since_epoch().count() < careful_until.load() ? ceiling : std::min(ceiling, FLOOR);
    }

  private:
    std::atomic<clock::rep> careful_until;
  };

  /**
   * How long the encoder waits for a new picture before encoding the last one again: `active` (rate control
   * converging) until CONVERGE after the last new picture, then IDLE, a keep-alive for the client's stall detection.
   */
  class still_pacer_t {
  public:
    static constexpr std::chrono::milliseconds CONVERGE {500};
    static constexpr std::chrono::milliseconds IDLE {500};

    still_pacer_t(std::chrono::nanoseconds active, bool idles):
        active {active},
        idles {idles} {}

    void new_picture(clock::time_point now) {
      last_new = now;
    }

    std::chrono::nanoseconds wait(clock::time_point now) const {
      return idles && now - last_new >= CONVERGE ? std::max<std::chrono::nanoseconds>(active, IDLE) : active;
    }

  private:
    std::chrono::nanoseconds active;
    bool idles;
    clock::time_point last_new {};
  };
}  // namespace stream_policy
