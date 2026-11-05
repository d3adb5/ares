/**
 * @file src/platform/linux/input/stream_input.h
 * @brief Keyboard, mouse and touch for a Gamescope stream session, over its socket.
 */
#pragma once

#include "virtualhid_common.h"

#include <cstdint>

namespace platf::stream_input {
  /** Whether this input drives a Gamescope stream instead of virtual devices. */
  bool active(const input_raw_t *raw);

  void move(input_raw_t *raw, int delta_x, int delta_y);
  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y);
  void button(input_raw_t *raw, int button, bool release);
  void scroll(input_raw_t *raw, int high_resolution_distance, bool horizontal);
  void key(input_raw_t *raw, std::uint16_t key_code, bool release);
  void unicode(input_raw_t *raw, const char *utf8, int size);
  void touch(input_raw_t *raw, const touch_input_t &touch);
}  // namespace platf::stream_input
