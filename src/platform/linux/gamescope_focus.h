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
#include <vector>

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

    struct focus_t {
      std::uint32_t front {};  ///< The app drawn as the base layer (769 is Steam itself, 0 none).
      bool game_window {};  ///< Some app other than Steam has a window it could show (GAMESCOPE_FOCUSABLE_APPS).
    };

    /// What Gamescope shows; nothing when the display cannot be asked.
    std::optional<focus_t> focus(const std::string &display) {
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
        front_atom = intern("GAMESCOPE_FOCUSED_APP_GFX");
        apps_atom = intern("GAMESCOPE_FOCUSABLE_APPS");
        root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
      }
      const auto front = cardinals(front_atom);
      const auto apps = cardinals(apps_atom);
      if (!front || !apps) {
        return std::nullopt;
      }
      focus_t focus;
      focus.front = front->empty() ? 0 : front->front();
      for (const auto app : *apps) {
        focus.game_window = focus.game_window || (app != 0 && app != 769);
      }
      return focus;
#else
      return std::nullopt;
#endif
    }

  private:
#ifdef SUNSHINE_BUILD_X11
    xcb_atom_t intern(const char *name) {
      auto *reply = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, std::strlen(name), name), nullptr);
      const auto atom = reply ? reply->atom : XCB_ATOM_NONE;
      std::free(reply);
      return atom;
    }

    /// A CARDINAL list property of the root window: empty when unset, nothing when the server did not answer.
    std::optional<std::vector<std::uint32_t>> cardinals(xcb_atom_t atom) {
      auto *reply = xcb_get_property_reply(connection, xcb_get_property(connection, 0, root, atom, XCB_ATOM_CARDINAL, 0, 256), nullptr);
      if (!reply) {
        return std::nullopt;
      }
      std::vector<std::uint32_t> values;
      if (reply->format == 32) {
        values.resize(xcb_get_property_value_length(reply) / 4);
        std::memcpy(values.data(), xcb_get_property_value(reply), values.size() * 4);
      }
      std::free(reply);
      return values;
    }
#endif

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
    xcb_atom_t front_atom {};
    xcb_atom_t apps_atom {};
    xcb_window_t root {};
#endif
    std::string connected_to;
  };
}  // namespace platf::gamescope
