/**
 * @file src/platform/linux/gamescope_focus.h
 * @brief Which app Gamescope shows: GAMESCOPE_FOCUSED_APP_GFX on the root window of its first Xwayland.
 *
 * The app whose picture is the base layer. GAMESCOPE_FOCUSED_APP is who has the input instead, which becomes Steam
 * (769) while its overlay is open over a game; the _GFX one stays the game's then.
 */
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#ifdef SUNSHINE_BUILD_X11
  #include <xcb/xcb.h>
#endif

namespace platf::gamescope {
  /// Reads the property over one kept xcb connection (xcb reports a lost server; Xlib would exit the process).
  class focus_reader_t {
  public:
    focus_reader_t() = default;
    focus_reader_t(const focus_reader_t &) = delete;
    focus_reader_t &operator=(const focus_reader_t &) = delete;

    ~focus_reader_t() {
      close();
    }

    /// The focused app's Steam id (769 is Steam itself, 0 none); nothing when the display cannot be asked.
    std::optional<std::uint32_t> focused_app(const std::string &display) {
#ifdef SUNSHINE_BUILD_X11
      if (display.empty()) {
        return std::nullopt;
      }
      if (connection && (xcb_connection_has_error(connection) || display != connected_to)) {
        close();
      }
      if (!connection) {
        connection = xcb_connect(display.c_str(), nullptr);
        if (xcb_connection_has_error(connection)) {
          close();
          return std::nullopt;
        }
        connected_to = display;
        static constexpr char NAME[] = "GAMESCOPE_FOCUSED_APP_GFX";
        auto *reply = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, std::strlen(NAME), NAME), nullptr);
        atom = reply ? reply->atom : XCB_ATOM_NONE;
        std::free(reply);
        root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
      }
      auto *reply = xcb_get_property_reply(connection, xcb_get_property(connection, 0, root, atom, XCB_ATOM_CARDINAL, 0, 1), nullptr);
      if (!reply) {
        return std::nullopt;
      }
      std::optional<std::uint32_t> app;
      if (reply->format == 32 && xcb_get_property_value_length(reply) >= 4) {
        std::uint32_t value;
        std::memcpy(&value, xcb_get_property_value(reply), 4);
        app = value;
      } else if (reply->type == XCB_ATOM_NONE || xcb_get_property_value_length(reply) == 0) {
        app = 0;  // Gamescope is up and shows nothing yet
      }
      std::free(reply);
      return app;
#else
      return std::nullopt;
#endif
    }

  private:
    void close() {
#ifdef SUNSHINE_BUILD_X11
      if (connection) {
        xcb_disconnect(connection);
        connection = nullptr;
      }
#endif
    }

#ifdef SUNSHINE_BUILD_X11
    xcb_connection_t *connection {};
    xcb_atom_t atom {};
    xcb_window_t root {};
#endif
    std::string connected_to;
  };
}  // namespace platf::gamescope
