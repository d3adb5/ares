/**
 * @file src/platform/linux/input/kwin_input.cpp
 * @brief Keyboard, mouse and touch for a private KWin desktop, through its fake-input protocol.
 */
#include "kwin_input.h"
#include "client_key_map.h"
#include "virtualhid_gamepad.h"

#include "src/logging.h"
#include "src/platform/linux/kwin_session.h"

#include <algorithm>
#include <boost/locale.hpp>
#include <libevdev/libevdev.h>
#include <map>
#include <mutex>
#include <stdexcept>

namespace platf {
  input_raw_t::input_raw_t(std::shared_ptr<kwin::connection_t> private_kwin, std::string input_session_tag):
      session_tag(std::move(input_session_tag)),
      kwin(std::move(private_kwin)),
      runtime(session_tag.empty() ? nullptr : gamepad::runtime_for(session_tag)),
      gamepads(MAX_GAMEPADS) {
    if (!session_tag.empty() && !runtime) {
      BOOST_LOG(error) << "[libvirtualhid] Failed to create the detached desktop's gamepad runtime";
    }
  }

  namespace {
    // One input connection per detached desktop, for its whole life. KWin
    // gives the desktop's seat a pointer and keyboard only while such a
    // connection exists; with none, a game's pointer lock leaves Xwayland
    // asking for a lock on no pointer, and it exits.
    std::mutex kept_kwin_input_mutex;
    std::map<std::string, std::shared_ptr<kwin::connection_t>> kept_kwin_input;
  }  // namespace

  input_t input_private_kwin(const std::string &wayland_socket, const std::string &session_tag) {
    std::lock_guard lock {kept_kwin_input_mutex};
    auto &connection = kept_kwin_input[wayland_socket];
    // A connection KWin has dropped is no use to the next stream.
    if (connection && !connection->dispatch(std::chrono::milliseconds(0))) {
      connection.reset();
    }
    if (!connection) {
      std::string error;
      connection = kwin::connection_t::open({wayland_socket, {}, {}}, false, true, error);
      if (!connection) {
        kept_kwin_input.erase(wayland_socket);
        throw std::runtime_error("Private KWin input: " + error);
      }
    }
    return {new input_raw_t(connection, session_tag)};
  }

  void forget_private_kwin_input(const std::string &wayland_socket) {
    std::shared_ptr<kwin::connection_t> connection;
    {
      std::lock_guard lock {kept_kwin_input_mutex};
      const auto found = kept_kwin_input.find(wayland_socket);
      if (found == kept_kwin_input.end()) {
        return;
      }
      connection = std::move(found->second);
      kept_kwin_input.erase(found);
    }
  }
}  // namespace platf

namespace platf::kwin_input {
  bool active(const input_raw_t *raw) {
    return raw && raw->kwin;
  }

  void move(input_raw_t *raw, int delta_x, int delta_y) {
    const auto &o = raw->kwin->output();
    raw->kwin->pointer_motion(double(delta_x) / o.scale, double(delta_y) / o.scale);
  }

  void move_abs(input_raw_t *raw, float x, float y) {
    const auto &o = raw->kwin->output();
    raw->kwin->pointer_absolute(o.x + double(x) / o.scale, o.y + double(y) / o.scale);
  }

  void button(input_raw_t *raw, int button, bool release) {
    int code;
    switch (button) {
      case BUTTON_LEFT:
        code = BTN_LEFT;
        break;
      case BUTTON_MIDDLE:
        code = BTN_MIDDLE;
        break;
      case BUTTON_RIGHT:
        code = BTN_RIGHT;
        break;
      case BUTTON_X1:
        code = BTN_SIDE;
        break;
      case BUTTON_X2:
        code = BTN_EXTRA;
        break;
      default:
        return;
    }
    raw->kwin->button(code, !release);
  }

  void scroll(input_raw_t *raw, int high_resolution_distance, bool horizontal) {
    // Moonlight scrolls up with a positive distance, Wayland axes grow downwards.
    raw->kwin->axis(horizontal, (horizontal ? 1.0 : -1.0) * double(high_resolution_distance) / 8.0);
  }

  void key(input_raw_t *raw, std::uint16_t key_code, bool release) {
    if (const auto key = evdev_key_for_client_key(key_code)) {
      raw->kwin->key(*key, !release);
    }
  }

  void unicode(input_raw_t *raw, const char *utf8, int size) {
    if (!utf8 || size <= 0) {
      return;
    }
    try {
      const auto text = boost::locale::conv::utf_to_utf<char32_t>(utf8, utf8 + size);
      for (char32_t character : text) {
        const std::uint32_t symbol = character <= 0xff ? character : (0x01000000u | character);
        if (!raw->kwin->keysym(symbol, true) || !raw->kwin->keysym(symbol, false)) {
          BOOST_LOG(warning) << "Private KWin Unicode input failed";
          break;
        }
      }
    } catch (const std::exception &error) {
      BOOST_LOG(warning) << "Private KWin text input: " << error.what();
    }
  }

  void touch(client_input_raw_t *raw, const touch_input_t &touch) {
    auto &kwin = *raw->global->kwin;
    const auto &o = kwin.output();
    // touch.x/y are normalized to the streamed output, which is the private
    // desktop's only one.
    const double x = o.x + touch.x * o.width / o.scale;
    const double y = o.y + touch.y * o.height / o.scale;
    switch (touch.eventType) {
      case LI_TOUCH_EVENT_DOWN:
      case LI_TOUCH_EVENT_MOVE:
        if (raw->kwin_touches.insert(touch.pointerId).second) {
          kwin.touch_down(touch.pointerId, x, y);
        } else {
          kwin.touch_motion(touch.pointerId, x, y);
        }
        break;
      case LI_TOUCH_EVENT_UP:
      case LI_TOUCH_EVENT_CANCEL:
        if (raw->kwin_touches.erase(touch.pointerId)) {
          kwin.touch_up(touch.pointerId);
        }
        break;
      case LI_TOUCH_EVENT_CANCEL_ALL:
        for (const auto id : raw->kwin_touches) {
          kwin.touch_up(id);
        }
        raw->kwin_touches.clear();
        break;
      default:
        // Hover has no equivalent in the fake-input protocol.
        break;
    }
  }
}  // namespace platf::kwin_input
