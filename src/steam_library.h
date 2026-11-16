/**
 * @file src/steam_library.h
 * @brief The games of the local Steam client's library, for one app entry per game.
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace steam_library {

  struct game_t {
    std::uint32_t appid {};
    std::string name;
    bool installed {};
  };

  struct app_info_t {
    std::string name;
    std::string type;
  };

  /// Every value of a key in a text VDF (libraryfolders.vdf, appmanifest_*.acf), at any depth.
  inline std::vector<std::string> text_vdf_values(std::string_view text, std::string_view key) {
    std::vector<std::string> tokens;
    for (std::size_t pos = 0; pos < text.size();) {
      const auto open = text.find('"', pos);
      if (open == std::string_view::npos) {
        break;
      }
      std::string token;
      auto end = open + 1;
      for (; end < text.size() && text[end] != '"'; ++end) {
        if (text[end] == '\\' && end + 1 < text.size()) {
          ++end;
        }
        token += text[end];
      }
      tokens.push_back(std::move(token));
      pos = end + 1;
    }
    // Braces are skipped: a section name pairs with its first key, which no caller asks for.
    std::vector<std::string> values;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
      if (tokens[i] == key) {
        values.push_back(tokens[i + 1]);
        ++i;
      }
    }
    return values;
  }

  /**
   * The name and type in each app's "common" section of appcache/appinfo.vdf (versions 27 to 29).
   * Empty when the file is not one of those.
   */
  inline std::map<std::uint32_t, app_info_t> read_appinfo(std::string_view data) {
    std::map<std::uint32_t, app_info_t> apps;
    const auto u32 = [&](std::size_t at) {
      std::uint32_t value;
      std::memcpy(&value, data.data() + at, 4);
      return value;
    };
    if (data.size() < 8) {
      return apps;
    }
    const auto magic = u32(0);
    if (magic != 0x07564427 && magic != 0x07564428 && magic != 0x07564429) {
      return apps;
    }
    const auto version = magic & 0xff;
    std::size_t pos = 8;
    std::vector<std::string_view> strings;
    if (version >= 0x29) {
      if (data.size() < 16) {
        return apps;
      }
      std::uint64_t table;
      std::memcpy(&table, data.data() + 8, 8);
      pos = 16;
      if (table + 4 > data.size()) {
        return apps;
      }
      const auto count = u32(table);
      for (std::size_t at = table + 4, i = 0; i < count && at < data.size(); ++i) {
        const auto end = data.find('\0', at);
        if (end == std::string_view::npos) {
          break;
        }
        strings.push_back(data.substr(at, end - at));
        at = end + 1;
      }
    }
    const std::size_t entry_header = version >= 0x28 ? 60 : 40;

    while (pos + 8 <= data.size()) {
      const auto appid = u32(pos);
      const auto size = u32(pos + 4);
      if (appid == 0 || pos + 8 + size > data.size()) {
        break;
      }
      const auto end = pos + 8 + size;
      auto at = pos + 8 + entry_header;
      pos = end;

      // Binary KV: type byte, key (string table index or C string), value; 0x08 ends a section.
      std::vector<std::string_view> path;
      app_info_t info;
      const auto read_key = [&](std::string_view &key) {
        if (version >= 0x29) {
          if (at + 4 > end || u32(at) >= strings.size()) {
            return false;
          }
          key = strings[u32(at)];
          at += 4;
          return true;
        }
        const auto zero = data.find('\0', at);
        if (zero == std::string_view::npos || zero >= end) {
          return false;
        }
        key = data.substr(at, zero - at);
        at = zero + 1;
        return true;
      };
      bool ok = true;
      while (ok && at < end) {
        const auto type = static_cast<unsigned char>(data[at++]);
        if (type == 0x08) {
          if (path.empty()) {
            break;
          }
          path.pop_back();
          continue;
        }
        std::string_view key;
        if (!read_key(key)) {
          break;
        }
        switch (type) {
          case 0x00:
            path.push_back(key);
            break;
          case 0x01: {
            const auto zero = data.find('\0', at);
            if (zero == std::string_view::npos || zero >= end) {
              ok = false;
              break;
            }
            const auto value = data.substr(at, zero - at);
            at = zero + 1;
            if (path.size() == 2 && path[1] == "common") {
              if (key == "name") {
                info.name = value;
              } else if (key == "type") {
                info.type = value;
              }
            }
            break;
          }
          case 0x02:
          case 0x03:
          case 0x04:
          case 0x06:
            at += 4;
            break;
          case 0x07:
          case 0x0a:
            at += 8;
            break;
          default:
            ok = false;
        }
      }
      if (!info.name.empty() || !info.type.empty()) {
        apps[appid] = std::move(info);
      }
    }
    return apps;
  }

  /// Steam's own runtimes and tools, for installed apps appinfo.vdf says nothing about.
  inline bool looks_like_tool(std::string_view name) {
    for (const auto prefix : {"Proton", "Steam Linux Runtime", "Steamworks Common Redistributables", "Steamworks SDK Redist"}) {
      if (name.starts_with(prefix)) {
        return true;
      }
    }
    return false;
  }

  inline bool is_game_type(std::string_view type) {
    return type.size() == 4 && std::equal(type.begin(), type.end(), "game", [](char a, char b) {
             return std::tolower(static_cast<unsigned char>(a)) == b;
           });
  }

  /**
   * The games: installed ones (manifest name, or appinfo's) and owned ones (known to appinfo as a game).
   * appinfo's type decides when it knows the app; otherwise only installed non-tools count.
   */
  inline std::vector<game_t> games_from(
    const std::map<std::uint32_t, std::string> &installed,
    const std::set<std::uint32_t> &owned,
    const std::map<std::uint32_t, app_info_t> &appinfo
  ) {
    std::map<std::uint32_t, game_t> games;
    const auto add = [&](std::uint32_t appid, const std::string &manifest_name, bool is_installed) {
      const auto info = appinfo.find(appid);
      if (info != appinfo.end() && !info->second.type.empty()) {
        if (!is_game_type(info->second.type)) {
          return;
        }
      } else if (!is_installed || looks_like_tool(manifest_name)) {
        return;
      }
      auto name = info != appinfo.end() && !info->second.name.empty() ? info->second.name : manifest_name;
      if (name.empty()) {
        return;
      }
      auto &game = games[appid];
      game.appid = appid;
      game.name = std::move(name);
      game.installed = game.installed || is_installed;
    };
    for (const auto &[appid, name] : installed) {
      add(appid, name, true);
    }
    for (const auto appid : owned) {
      if (!installed.contains(appid)) {
        add(appid, {}, false);
      }
    }
    std::vector<game_t> sorted;
    for (auto &[_, game] : games) {
      sorted.push_back(std::move(game));
    }
    std::ranges::sort(sorted, [](const game_t &a, const game_t &b) {
      return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
      });
    });
    return sorted;
  }

  inline std::optional<std::uint32_t> parse_appid(std::string_view text) {
    if (text.empty() || text.size() > 10 || !std::ranges::all_of(text, [](unsigned char c) {
          return std::isdigit(c) != 0;
        })) {
      return std::nullopt;
    }
    const auto value = std::stoull(std::string {text});
    if (value == 0 || value > UINT32_MAX) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
  }

  inline std::string read_file(const std::filesystem::path &path) {
    std::ifstream in {path, std::ios::binary};
    return {std::istreambuf_iterator<char> {in}, {}};
  }

  /**
   * Scan a Steam root (~/.steam/steam): every library folder's manifests, with owned_too the apps each user's library
   * has cached details for (userdata/<id>/config/librarycache/<appid>.json: what they own), and appinfo.vdf.
   */
  inline std::vector<game_t> scan(const std::filesystem::path &root, bool owned_too) {
    std::error_code ec;
    std::map<std::uint32_t, std::string> installed;
    auto folders = text_vdf_values(read_file(root / "steamapps" / "libraryfolders.vdf"), "path");
    folders.push_back(root.string());
    std::set<std::filesystem::path> seen;
    for (const auto &folder : folders) {
      const auto steamapps = std::filesystem::path {folder} / "steamapps";
      if (!seen.insert(std::filesystem::weakly_canonical(steamapps, ec)).second) {
        continue;
      }
      for (const auto &entry : std::filesystem::directory_iterator {steamapps, ec}) {
        const auto file = entry.path().filename().string();
        if (!file.starts_with("appmanifest_") || !file.ends_with(".acf")) {
          continue;
        }
        const auto text = read_file(entry.path());
        const auto ids = text_vdf_values(text, "appid");
        const auto names = text_vdf_values(text, "name");
        if (const auto appid = ids.empty() ? std::nullopt : parse_appid(ids.front())) {
          installed[*appid] = names.empty() ? std::string {} : names.front();
        }
      }
    }

    std::set<std::uint32_t> owned;
    for (const auto &user : std::filesystem::directory_iterator {owned_too ? root / "userdata" : std::filesystem::path {}, ec}) {
      std::error_code user_ec;
      for (const auto &entry : std::filesystem::directory_iterator {user.path() / "config" / "librarycache", user_ec}) {
        if (entry.path().extension() == ".json") {
          if (const auto appid = parse_appid(entry.path().stem().string())) {
            owned.insert(*appid);
          }
        }
      }
    }

    return games_from(installed, owned, read_appinfo(read_file(root / "appcache" / "appinfo.vdf")));
  }

  /// The portrait capsule Steam's library shows, cached in appcache/librarycache; empty if none.
  inline std::string capsule_image(const std::filesystem::path &root, std::uint32_t appid) {
    std::error_code ec;
    const auto dir = root / "appcache" / "librarycache" / std::to_string(appid);
    if (std::filesystem::exists(dir / "library_600x900.jpg", ec)) {
      return (dir / "library_600x900.jpg").string();
    }
    for (const auto &entry : std::filesystem::directory_iterator {dir, ec}) {
      if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / "library_600x900.jpg", ec)) {
        return (entry.path() / "library_600x900.jpg").string();
      }
    }
    if (std::filesystem::exists(root / "appcache" / "librarycache" / (std::to_string(appid) + "_library_600x900.jpg"), ec)) {
      return (root / "appcache" / "librarycache" / (std::to_string(appid) + "_library_600x900.jpg")).string();
    }
    return {};
  }

  /// A UUID that stays the same for a game across scans, so a client's shortcut keeps working.
  inline std::string uuid_for(std::uint32_t appid) {
    char uuid[37];
    std::snprintf(uuid, sizeof(uuid), "5354454D-0000-4000-8000-%012X", appid);
    return uuid;
  }

  /// The template entry's command, asking its Steam to run the game.
  inline std::string launch_command(std::string_view template_cmd, std::uint32_t appid) {
    auto cmd = std::string {template_cmd};
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back()))) {
      cmd.pop_back();
    }
    return cmd + " steam://rungameid/" + std::to_string(appid);
  }

  /// What the library's entries depend on: every game with whether it is installed. Changes on install or uninstall.
  inline std::string signature(const std::filesystem::path &root) {
    std::string out;
    for (const auto &game : scan(root, true)) {
      out += std::to_string(game.appid) + (game.installed ? "i " : "o ");
    }
    return out;
  }

  /**
   * Whether Steam is running the game: its reaper, "reaper SteamLaunch AppId=<id> -- ...", waits for the game's whole
   * process tree, Proton or native, so it lives exactly as long as the game.
   */
  inline bool game_running(std::uint32_t appid, const std::filesystem::path &proc = "/proc") {
    const auto wanted = "AppId=" + std::to_string(appid);
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator {proc, ec}) {
      const auto pid = entry.path().filename().string();
      if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) { return std::isdigit(c); })) {
        continue;
      }
      std::ifstream file {entry.path() / "cmdline", std::ios::binary};
      const std::string cmdline {std::istreambuf_iterator<char> {file}, {}};
      bool launch = false;
      bool app = false;
      for (std::size_t at = 0; at < cmdline.size();) {
        const auto end = std::min(cmdline.find('\0', at), cmdline.size());
        const std::string_view arg {cmdline.data() + at, end - at};
        if (arg == "--") {
          break;  // the game's own arguments follow
        }
        launch = launch || arg == "SteamLaunch";
        app = app || arg == wanted;
        at = end + 1;
      }
      if (launch && app) {
        return true;
      }
    }
    return false;
  }

  /// Ends a session with its game: true once the game ran and has been gone for the grace period.
  class game_watch_t {
  public:
    using clock = std::chrono::steady_clock;

    explicit game_watch_t(std::chrono::milliseconds grace = std::chrono::seconds {3}):
        grace {grace} {}

    bool ended(bool running, clock::time_point now) {
      if (running) {
        seen = true;
        last_seen = now;
        return false;
      }
      return seen && now - last_seen >= grace;
    }

  private:
    std::chrono::milliseconds grace;
    bool seen {};
    clock::time_point last_seen {};
  };

}  // namespace steam_library
