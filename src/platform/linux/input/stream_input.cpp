/**
 * @file src/platform/linux/input/stream_input.cpp
 * @brief Keyboard, mouse and touch for a Gamescope stream session, over its socket.
 */
#include "stream_input.h"

#include "client_key_map.h"
#include "virtualhid_gamepad.h"

#include "src/logging.h"
#include "src/platform/linux/gamescope_stream.h"
#include "src/platform/linux/hermes_stream_v1.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <linux/input-event-codes.h>

namespace platf {
  input_raw_t::input_raw_t(std::shared_ptr<gamescope_stream::session_t> stream_session, std::string input_session_tag):
      session_tag(std::move(input_session_tag)),
      stream(std::move(stream_session)),
      runtime(session_tag.empty() ? nullptr : gamepad::runtime_for(session_tag)),
      gamepads(MAX_GAMEPADS) {
    if (!session_tag.empty() && !runtime) {
      BOOST_LOG(error) << "[libvirtualhid] Failed to create the stream session's gamepad runtime";
    }
  }

  input_t input_gamescope_stream(const std::string &socket, const std::string &session_tag) {
    return {new input_raw_t(gamescope_stream::session_for(socket), session_tag)};
  }
}  // namespace platf

namespace platf::stream_input {
  namespace {
    template<typename T>
    void send(input_raw_t *raw, T &message, std::uint16_t type) {
      message.hdr.type = type;
      message.hdr.size = sizeof(T);
      raw->stream->send(&message, sizeof(T));
    }
  }  // namespace

  bool active(const input_raw_t *raw) {
    return raw && raw->stream;
  }

  void move(input_raw_t *raw, int delta_x, int delta_y) {
    hermes_client_pointer_motion message {};
    message.dx = delta_x;
    message.dy = delta_y;
    send(raw, message, HERMES_CLIENT_POINTER_MOTION);
  }

  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y) {
    if (touch_port.width <= 0 || touch_port.height <= 0) {
      return;
    }
    hermes_client_pointer_absolute message {};
    message.x = std::clamp(double(x) / touch_port.width, 0.0, 1.0);
    message.y = std::clamp(double(y) / touch_port.height, 0.0, 1.0);
    send(raw, message, HERMES_CLIENT_POINTER_ABSOLUTE);
  }

  void button(input_raw_t *raw, int button, bool release) {
    hermes_client_pointer_button message {};
    switch (button) {
      case BUTTON_LEFT:
        message.button = BTN_LEFT;
        break;
      case BUTTON_MIDDLE:
        message.button = BTN_MIDDLE;
        break;
      case BUTTON_RIGHT:
        message.button = BTN_RIGHT;
        break;
      case BUTTON_X1:
        message.button = BTN_SIDE;
        break;
      case BUTTON_X2:
        message.button = BTN_EXTRA;
        break;
      default:
        return;
    }
    message.pressed = release ? 0 : 1;
    send(raw, message, HERMES_CLIENT_POINTER_BUTTON);
  }

  void scroll(input_raw_t *raw, int high_resolution_distance, bool horizontal) {
    hermes_client_pointer_axis message {};
    // Moonlight scrolls up with a positive distance, the protocol grows downwards.
    if (horizontal) {
      message.value120_x = high_resolution_distance;
    } else {
      message.value120_y = -high_resolution_distance;
    }
    send(raw, message, HERMES_CLIENT_POINTER_AXIS);
  }

  void key(input_raw_t *raw, std::uint16_t key_code, bool release) {
    if (const auto code = evdev_key_for_client_key(key_code)) {
      hermes_client_key message {};
      message.keycode = static_cast<std::uint32_t>(*code);
      message.pressed = release ? 0 : 1;
      send(raw, message, HERMES_CLIENT_KEY);
    }
  }

  void unicode(input_raw_t *raw, const char *utf8, int size) {
    if (!utf8 || size <= 0 || size > HERMES_MAX_TEXT) {
      return;
    }
    std::array<std::uint8_t, sizeof(hermes_client_text) + HERMES_MAX_TEXT> data {};
    hermes_client_text message {};
    message.hdr.type = HERMES_CLIENT_TEXT;
    message.hdr.size = static_cast<std::uint32_t>(sizeof(message) + size);
    message.length = static_cast<std::uint32_t>(size);
    std::memcpy(data.data(), &message, sizeof(message));
    std::memcpy(data.data() + sizeof(message), utf8, size);
    raw->stream->send(data.data(), sizeof(message) + size);
  }

  void touch(input_raw_t *raw, const touch_input_t &touch) {
    hermes_client_touch message {};
    message.id = static_cast<std::int32_t>(touch.pointerId);
    message.x = std::clamp(double(touch.x), 0.0, 1.0);
    message.y = std::clamp(double(touch.y), 0.0, 1.0);
    switch (touch.eventType) {
      case LI_TOUCH_EVENT_DOWN:
        message.type = HERMES_TOUCH_DOWN;
        break;
      case LI_TOUCH_EVENT_MOVE:
        message.type = HERMES_TOUCH_MOTION;
        break;
      case LI_TOUCH_EVENT_UP:
        message.type = HERMES_TOUCH_UP;
        break;
      case LI_TOUCH_EVENT_CANCEL:
      case LI_TOUCH_EVENT_CANCEL_ALL:
        message.type = HERMES_TOUCH_CANCEL;
        break;
      default:
        // Hover has no equivalent.
        return;
    }
    send(raw, message, HERMES_CLIENT_TOUCH);
  }
}  // namespace platf::stream_input
