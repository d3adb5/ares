/**
 * @file src/platform/linux/gamescope_session.h
 * @brief The Gamescope command line an isolated application session runs.
 *
 * Kept free of process and device state, so the parts that decide what
 * Gamescope is told - which scanout option a build understands, what Steam
 * mode changes - can be checked without a Hermes-KMS card or a Gamescope
 * binary.
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace platf::gamescope {

  /**
   * How a Gamescope build is told which DRM device to scan out on.
   *
   * Upstream derives its KMS device from the Vulkan device it renders with.
   * An isolated session renders on the real GPU and scans out on its own
   * Hermes-KMS card, which has no Vulkan driver, so it needs a build that
   * takes the two separately. Without one, Gamescope opens the primary node of
   * the GPU it renders with - the card the host's desktop is on.
   */
  enum class drm_selector_e {
    none,  ///< No option: the build scans out on the device it renders with.
    prefer_drm,  ///< --prefer-drm, ValveSoftware/gamescope#2220; also read from GAMESCOPE_PREFER_DRM.
    drm_device,  ///< --drm-device, the shorter downstream patch reported in Hermes#57.
  };

  inline std::string_view drm_selector_flag(drm_selector_e selector) {
    switch (selector) {
      case drm_selector_e::prefer_drm:
        return "--prefer-drm";
      case drm_selector_e::drm_device:
        return "--drm-device";
      case drm_selector_e::none:
        break;
    }
    return {};
  }

  /**
   * Whether help text names an option on its own rather than as the start of
   * a longer one, so "--prefer-drm" is not found inside "--prefer-drm-node".
   */
  inline bool help_lists_option(std::string_view help, std::string_view option) {
    const auto is_option_char = [](char ch) {
      return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_';
    };
    for (auto pos = help.find(option); pos != std::string_view::npos; pos = help.find(option, pos + 1)) {
      const auto end = pos + option.size();
      const bool starts_alone = pos == 0 || !is_option_char(help[pos - 1]);
      const bool ends_alone = end == help.size() || !is_option_char(help[end]);
      if (starts_alone && ends_alone) {
        return true;
      }
    }
    return false;
  }

  /**
   * The scanout option a build offers, judged from its --help output.
   *
   * Gamescope prints its usage to stderr, so the caller has to have captured
   * both streams. --prefer-drm wins when a build lists both: it is the one
   * proposed upstream, and the one that validates the device before starting.
   */
  inline std::optional<drm_selector_e> drm_selector_from_help(std::string_view help) {
    if (help_lists_option(help, "--prefer-drm")) {
      return drm_selector_e::prefer_drm;
    }
    if (help_lists_option(help, "--drm-device")) {
      return drm_selector_e::drm_device;
    }
    return std::nullopt;
  }

  /**
   * ARES_ISOLATED_GAMESCOPE_DRM_FLAG, for a build whose --help does not
   * list the option it accepts. "none" launches without one, which is only
   * useful to reproduce what an unpatched Gamescope does.
   */
  inline std::optional<drm_selector_e> drm_selector_from_override(std::string_view value) {
    if (value == "--prefer-drm" || value == "prefer-drm") {
      return drm_selector_e::prefer_drm;
    }
    if (value == "--drm-device" || value == "drm-device") {
      return drm_selector_e::drm_device;
    }
    if (value == "none") {
      return drm_selector_e::none;
    }
    return std::nullopt;
  }

  /**
   * Whether path is a DRM primary node, /dev/dri/cardN, which is what both
   * options take. A render node cannot scan out, and anything else would be
   * split into several arguments by run_command().
   */
  inline bool is_primary_card_path(std::string_view path) {
    constexpr std::string_view prefix {"/dev/dri/card"};
    if (!path.starts_with(prefix) || path.size() == prefix.size()) {
      return false;
    }
    for (const char ch : path.substr(prefix.size())) {
      if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
        return false;
      }
    }
    return true;
  }

  /**
   * Whether an application runs in Gamescope's Steam mode.
   *
   * mode is the app's gamescope-steam-mode: "on" and "off" decide outright;
   * anything else is auto, which turns it on when the command's program is
   * steam. The program is the first whitespace-separated word, as
   * run_command() splits it.
   */
  inline bool steam_mode_for(std::string_view mode, std::string_view command) {
    if (mode == "on") {
      return true;
    }
    if (mode == "off") {
      return false;
    }
    const auto begin = command.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
      return false;
    }
    auto program = command.substr(begin, command.find_first_of(" \t", begin) - begin);
    if (const auto slash = program.rfind('/'); slash != std::string_view::npos) {
      program.remove_prefix(slash + 1);
    }
    return program == "steam";
  }

  /**
   * The command asking the Steam an application started to shut down. Its
   * program is the one that command runs, so a path keeps working.
   */
  inline std::string steam_shutdown_command(std::string_view command) {
    const auto begin = command.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
      return {};
    }
    return std::string {command.substr(begin, command.find_first_of(" \t", begin) - begin)} + " -shutdown";
  }

  /**
   * Whether a /dev/shm name is a shared memory segment of uid's Steam
   * (u<uid>-Shm_<hex>). Its web helper leaves them behind on every exit.
   */
  inline bool steam_shm_segment(std::string_view name, unsigned uid) {
    const auto prefix = std::format("u{}-Shm_", uid);
    if (!name.starts_with(prefix) || name.size() == prefix.size()) {
      return false;
    }
    name.remove_prefix(prefix.size());
    return std::ranges::all_of(name, [](unsigned char c) {
      return std::isxdigit(c) != 0;
    });
  }

  /**
   * The pid of the Steam that made a /tmp socket of uid's
   * (steam_chrome_shmem_uid<uid>_spid<pid>), none for any other name.
   */
  inline std::optional<int> steam_shmem_socket_pid(std::string_view name, unsigned uid) {
    const auto prefix = std::format("steam_chrome_shmem_uid{}_spid", uid);
    if (!name.starts_with(prefix)) {
      return std::nullopt;
    }
    name.remove_prefix(prefix.size());
    int pid = 0;
    const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), pid);
    if (ec != std::errc {} || end != name.data() + name.size() || pid <= 0) {
      return std::nullopt;
    }
    return pid;
  }

  /**
   * The first error of Steam's last start, from its console log
   * (~/.local/share/Steam/logs/console-linux.txt): "component: message",
   * without timestamp or pid. Empty when that start logged none.
   */
  inline std::string steam_startup_error(std::string_view log) {
    // Each start begins with steam.sh's own "Starting ... Steam" line.
    std::size_t start = 0;
    for (auto at = log.find("steam.sh["); at != std::string_view::npos; at = log.find("steam.sh[", at + 1)) {
      const auto eol = log.find('\n', at);
      if (log.substr(at, eol - at).find("]: Starting ") != std::string_view::npos) {
        start = log.rfind('\n', at);
        start = start == std::string_view::npos ? 0 : start + 1;
      }
    }
    for (auto line_start = start; line_start < log.size();) {
      const auto eol = std::min(log.find('\n', line_start), log.size());
      auto line = log.substr(line_start, eol - line_start);
      line_start = eol + 1;
      const auto marker = line.find("]: E: ");
      if (marker == std::string_view::npos) {
        continue;
      }
      // "[time] component[pid]: E: message"
      const auto pid = line.rfind('[', marker);
      const auto time_end = line.find("] ");
      if (pid == std::string_view::npos || time_end == std::string_view::npos || time_end + 2 > pid) {
        continue;
      }
      return std::string {line.substr(time_end + 2, pid - time_end - 2)} + ": " + std::string {line.substr(marker + 6)};
    }
    return {};
  }

  /// The optional flags an application session gets when its app sets none.
  constexpr std::string_view default_args {"--expose-wayland --force-windows-fullscreen"};

  // Call with the same parsed argument tokens run_command() will receive.
  // A per-app option must not redirect an isolated compositor to the host's
  // KMS card/backend, or replace the negotiated mode and connector.
  inline std::optional<std::string> session_option_override(std::span<const std::string> arguments) {
    constexpr std::string_view reserved[] {
      "--backend",
      "--ares-socket",
      "--prefer-drm",
      "--drm-device",
      "--output-width",
      "--output-height",
      "--nested-width",
      "--nested-height",
      "--nested-refresh",
      "--prefer-output"
    };
    for (const auto &argument : arguments) {
      const std::string_view option = std::string_view {argument}.substr(0, argument.find('='));
      if (option == "--") {
        return argument;
      }
      if (option.starts_with("--")) {
        // getopt_long accepts a unique prefix as well as the full option.
        for (const auto name : reserved) {
          if (name.starts_with(option)) {
            return argument;
          }
        }
      } else if (argument.starts_with('-')) {
        // GNU short options can be clustered (-eW800). Once an option takes
        // a value the rest of that token is its value, e.g. -Ffsr, not flags.
        for (const char flag : std::string_view {argument}.substr(1)) {
          if (std::string_view {"WHwhrO"}.find(flag) != std::string_view::npos) {
            return argument;
          }
          if (std::string_view {"mSFsoRTC"}.find(flag) != std::string_view::npos) {
            break;
          }
        }
      }
    }
    return std::nullopt;
  }

  struct launch_t {
    std::string binary {"gamescope"};  ///< The executable; an absolute path for Hermes' own build.
    int width = 1920;
    int height = 1080;
    int refresh_hz = 60;
    std::string connector;  ///< The session card's connector, e.g. Virtual-2.
    drm_selector_e selector = drm_selector_e::none;
    std::string scanout_card;  ///< The session card's primary node.
    std::string stream_socket;  ///< Stream frames over this socket instead: no card, no connector.
    bool hdr = false;  ///< Offer the stream's games HDR10 from the start; stream only.
    std::string vk_device;  ///< The GPU to render on, as vendor:device; empty lets Gamescope pick.
    bool steam_mode = false;
    std::string args;  ///< The app's gamescope-args; empty means default_args.
    std::string command;  ///< What runs inside Gamescope, after --.
  };

  /**
   * The Gamescope command line for one isolated application session.
   *
   * It is handed to run_command(), which splits on whitespace and never
   * involves a shell, so every value Hermes places here is a number, a
   * generated connector name, a /dev/dri path or a binary path checked to
   * hold no whitespace; the app's own flags and command are split the same
   * way and quotes are not interpreted.
   *
   * Steam mode is Gamescope's -e: without it every window's app id is its X
   * id, Steam's base-layer app id never matches, and the overlay stays
   * invisible while it holds the controller. It also gets a second Xwayland,
   * as SteamOS runs it, so the overlay cannot take a game's X focus - Wine
   * games minimize when it does.
   *
   * The app's flags come after Hermes' Steam-mode flags, so Gamescope's
   * last-one-wins options (--xwayland-count) can override them. The backend,
   * the card, the mode and the connector stay Hermes'.
   */
  inline std::string launch_command(const launch_t &launch) {
    auto command = std::format(
      "{} {} -W {} -H {} -w {} -h {} -r {}",
      launch.binary.empty() ? std::string {"gamescope"} : launch.binary,
      launch.stream_socket.empty() ? std::string {"--backend=drm"} : "--backend=ares --ares-socket " + launch.stream_socket,
      launch.width,
      launch.height,
      launch.width,
      launch.height,
      launch.refresh_hz
    );
    if (launch.hdr && !launch.stream_socket.empty()) {
      command += " --ares-hdr --hdr-enabled";
    }
    if (!launch.vk_device.empty()) {
      command += " --prefer-vk-device " + launch.vk_device;
    }
    if (launch.selector != drm_selector_e::none && !launch.scanout_card.empty()) {
      command += std::format(" {} {}", drm_selector_flag(launch.selector), launch.scanout_card);
    }
    command += launch.steam_mode ? " -e --xwayland-count 2" : " --xwayland-count 1";
    command += " ";
    command += launch.args.empty() ? std::string {default_args} : launch.args;
    if (!launch.connector.empty()) {
      command += " -O " + launch.connector;
    }
    command += " -- " + launch.command;
    return command;
  }

}  // namespace platf::gamescope
