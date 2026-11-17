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
#include <mutex>
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
    bool shortcut {};  ///< A non-Steam game added to Steam (userdata/<id>/config/shortcuts.vdf).
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
   * The non-Steam games in a shortcuts.vdf (binary KV with inline keys): "shortcuts" -> index -> fields, keys in any
   * case. Hidden ones are left out, as Steam's library leaves them out.
   */
  inline std::vector<game_t> read_shortcuts(std::string_view data) {
    std::vector<game_t> games;
    const auto lower = [](std::string_view text) {
      std::string out {text};
      for (auto &c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return out;
    };
    std::size_t at = 0;
    int depth = 0;
    game_t game;
    bool hidden = false;
    while (at < data.size()) {
      const auto type = static_cast<unsigned char>(data[at++]);
      if (type == 0x08) {
        if (depth == 2 && game.appid != 0 && !game.name.empty() && !hidden) {
          games.push_back(game);
        }
        if (--depth < 0) {
          break;
        }
        continue;
      }
      const auto zero = data.find('\0', at);
      if (zero == std::string_view::npos) {
        break;
      }
      const auto key = lower(data.substr(at, zero - at));
      at = zero + 1;
      if (type == 0x00) {
        if (++depth == 2) {
          game = {.installed = true, .shortcut = true};
          hidden = false;
        }
      } else if (type == 0x01) {
        const auto end = data.find('\0', at);
        if (end == std::string_view::npos) {
          break;
        }
        if (depth == 2 && key == "appname") {
          game.name = data.substr(at, end - at);
        }
        at = end + 1;
      } else if (type == 0x02 || type == 0x03 || type == 0x04 || type == 0x06) {
        if (at + 4 > data.size()) {
          break;
        }
        std::uint32_t value;
        std::memcpy(&value, data.data() + at, 4);
        at += 4;
        if (depth == 2 && key == "appid") {
          game.appid = value;
        } else if (depth == 2 && key == "ishidden") {
          hidden = value != 0;
        }
      } else if (type == 0x07 || type == 0x0a) {
        at += 8;
      } else {
        break;
      }
    }
    return games;
  }

  /// Case-insensitive by name, as Steam's library lists them.
  inline void sort_by_name(std::vector<game_t> &games) {
    std::ranges::sort(games, [](const game_t &a, const game_t &b) {
      return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
      });
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
    sort_by_name(sorted);
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
   * has cached details for (userdata/<id>/config/librarycache/<appid>.json: what they own), and appinfo.vdf; with
   * shortcuts the non-Steam games each user added (userdata/<id>/config/shortcuts.vdf).
   */
  inline std::vector<game_t> scan(const std::filesystem::path &root, bool owned_too, bool shortcuts = false) {
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

    auto games = games_from(installed, owned, read_appinfo(read_file(root / "appcache" / "appinfo.vdf")));
    if (shortcuts) {
      std::set<std::uint32_t> added;
      for (const auto &user : std::filesystem::directory_iterator {root / "userdata", ec}) {
        for (auto &game : read_shortcuts(read_file(user.path() / "config" / "shortcuts.vdf"))) {
          if (added.insert(game.appid).second) {
            games.push_back(std::move(game));
          }
        }
      }
      sort_by_name(games);
    }
    return games;
  }

  /// A non-Steam game's portrait art, as set in Steam's library (userdata/<id>/config/grid); empty if none.
  inline std::string shortcut_image(const std::filesystem::path &root, std::uint32_t appid) {
    std::error_code ec;
    for (const auto &user : std::filesystem::directory_iterator {root / "userdata", ec}) {
      for (const auto *suffix : {"p.png", "p.jpg", ".png", ".jpg"}) {
        const auto path = user.path() / "config" / "grid" / (std::to_string(appid) + suffix);
        if (std::filesystem::exists(path, ec)) {
          return path.string();
        }
      }
    }
    return {};
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

  inline std::string game_url(std::uint32_t appid, bool shortcut) {
    const auto id = shortcut ? (static_cast<std::uint64_t>(appid) << 32) | 0x02000000u : std::uint64_t {appid};
    return "steam://rungameid/" + std::to_string(id);
  }

  inline std::string trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
      text.remove_suffix(1);
    }
    return std::string {text};
  }

  /**
   * The template entry's command, asking its Steam to run the game. A non-Steam game gets the template's own: Steam's
   * UI drops a launch that arrives before it has loaded them (send_command, once ui_ready).
   */
  inline std::string launch_command(std::string_view template_cmd, std::uint32_t appid, bool shortcut = false) {
    return shortcut ? trimmed(template_cmd) : trimmed(template_cmd) + ' ' + game_url(appid, false);
  }

  /// Asks the running Steam of the template's command to run the game; never starts a second client.
  inline std::string send_command(std::string_view template_cmd, std::uint32_t appid, bool shortcut) {
    const auto begin = std::min(template_cmd.find_first_not_of(" \t"), template_cmd.size());
    const auto program = template_cmd.substr(begin, template_cmd.find_first_of(" \t", begin) - begin);
    return std::string {program} + " -ifrunning " + game_url(appid, shortcut);
  }

  /// A command without its steam:// argument: what entries of one template share, so their Steam can be reused.
  inline std::string template_command(std::string_view cmd) {
    auto text = trimmed(cmd);
    const auto last = text.find_last_of(" \t");
    if (last != std::string::npos && std::string_view {text}.substr(last + 1).starts_with("steam://")) {
      text = trimmed(std::string_view {text}.substr(0, last));
    }
    return text;
  }

  /// Steam's UI log; its size before Steam starts is where ui_ready reads from.
  inline std::filesystem::path ui_log(const std::filesystem::path &root) {
    return root / "logs" / "webhelper_js.txt";
  }

  inline std::uintmax_t file_size_or_zero(const std::filesystem::path &path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
  }

  /// Whether Steam's UI finished loading since the log was `from` bytes long (a shorter log was rotated).
  inline bool ui_ready(const std::filesystem::path &log, std::uintmax_t from) {
    std::ifstream in {log, std::ios::binary};
    if (file_size_or_zero(log) >= from) {
      in.seekg(static_cast<std::streamoff>(from));
    }
    const std::string text {std::istreambuf_iterator<char> {in}, {}};
    return text.find("SteamApp Init - After Login total time") != std::string::npos;
  }

  /// Steam plays this instead of Big Picture's startup movie; an empty one fails to play and is skipped at once.
  inline std::filesystem::path intro_override(const std::filesystem::path &root) {
    return root / "config" / "uioverrides" / "movies" / "bigpicture_startup.webm";
  }

  /// Skip the startup movie of the next Steam start, unless the user has an override of their own.
  inline void skip_intro(const std::filesystem::path &root) {
    std::error_code ec;
    const auto file = intro_override(root);
    if (!std::filesystem::exists(root / "config", ec) || std::filesystem::exists(file, ec)) {
      return;
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream {file};
  }

  /// Undo skip_intro: only an empty override is ours, and only folders it left empty go.
  inline void restore_intro(const std::filesystem::path &root) {
    std::error_code ec;
    const auto file = intro_override(root);
    if (!std::filesystem::is_regular_file(file, ec) || std::filesystem::file_size(file, ec) != 0) {
      return;
    }
    std::filesystem::remove(file, ec);
    std::filesystem::remove(file.parent_path(), ec);
    std::filesystem::remove(file.parent_path().parent_path(), ec);
  }

  /// What a scan reads, as paths with their change times: equal stamps mean an equal scan.
  inline std::string stamp(const std::filesystem::path &root) {
    std::string out;
    std::error_code ec;
    const auto add = [&](const std::filesystem::path &path) {
      const auto time = std::filesystem::last_write_time(path, ec);
      out += path.string() + ' ' + (ec ? "-" : std::to_string(time.time_since_epoch().count())) + '\n';
    };
    add(root / "steamapps" / "libraryfolders.vdf");
    add(root / "appcache" / "appinfo.vdf");
    auto folders = text_vdf_values(read_file(root / "steamapps" / "libraryfolders.vdf"), "path");
    folders.push_back(root.string());
    for (const auto &folder : folders) {
      add(std::filesystem::path {folder} / "steamapps");
    }
    for (const auto &user : std::filesystem::directory_iterator {root / "userdata", ec}) {
      add(user.path() / "config" / "librarycache");
      add(user.path() / "config" / "shortcuts.vdf");
    }
    return out;
  }

  /// The games of a full scan that a narrower one (installed only, without non-Steam games) would return.
  inline std::vector<game_t> select(std::vector<game_t> games, bool owned_too, bool shortcuts) {
    std::erase_if(games, [&](const game_t &game) {
      return game.shortcut ? !shortcuts : !owned_too && !game.installed;
    });
    return games;
  }

  /**
   * The full scan, repeated only when the stamp changed. While Steam is rewriting appinfo.vdf (changed under 5 s
   * ago) the last scan stands: a half-written file ends at its first bad record and would lose the games after it.
   */
  class cache_t {
  public:
    std::vector<game_t> games(const std::filesystem::path &root, std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now()) {
      std::lock_guard<std::mutex> lock {mutex};
      auto current = stamp(root);
      if (current == scanned) {
        return cached;
      }
      std::error_code ec;
      const auto written = std::filesystem::last_write_time(root / "appcache" / "appinfo.vdf", ec);
      if (!ec && !scanned.empty() && now - written < std::chrono::seconds {5}) {
        return cached;
      }
      cached = scan(root, true, true);
      scanned = std::move(current);
      ++scans;
      return cached;
    }

    int scans {};  ///< For tests.

  private:
    std::mutex mutex;
    std::string scanned;
    std::vector<game_t> cached;
  };

  /// What the library's entries depend on: every game, installed or not, and the non-Steam ones with their names.
  inline std::string signature(const std::vector<game_t> &games) {
    std::string out;
    for (const auto &game : games) {
      out += std::to_string(game.appid) + (game.shortcut ? "s" + game.name : game.installed ? "i" : "o") + '\n';
    }
    return out;
  }

  inline std::string signature(const std::filesystem::path &root) {
    return signature(scan(root, true, true));
  }

  /**
   * Whether a process is Steam's reaper of the game: "reaper SteamLaunch AppId=<id> -- <the game>"; with appid 0, of
   * any game. Not the one of its install script ("... AppId=<id> Install=1 -- ... iscriptevaluator.exe"), which Steam
   * runs and ends seconds before the game on a first launch after an update.
   */
  inline bool is_reaper(std::uint32_t appid, const std::filesystem::path &proc_pid) {
    const auto wanted = "AppId=" + std::to_string(appid);
    std::ifstream file {proc_pid / "cmdline", std::ios::binary};
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
      app = app || arg == wanted || (appid == 0 && arg.starts_with("AppId="));
      if (arg.starts_with("Install=")) {
        return false;
      }
      at = end + 1;
    }
    return launch && app;
  }

  /**
   * The pid of Steam's reaper of the game, 0 if none: it waits for the game's whole process tree, Proton or native,
   * so it lives exactly as long as the game.
   */
  inline long game_reaper(std::uint32_t appid, const std::filesystem::path &proc = "/proc") {
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator {proc, ec}) {
      const auto pid = entry.path().filename().string();
      if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) { return std::isdigit(c); })) {
        continue;
      }
      if (is_reaper(appid, entry.path())) {
        return std::stol(pid);
      }
    }
    return 0;
  }

  inline bool game_running(std::uint32_t appid, const std::filesystem::path &proc = "/proc") {
    return game_reaper(appid, proc) != 0;
  }

  /// Whether Steam runs any game at all.
  inline bool any_game_running(const std::filesystem::path &proc = "/proc") {
    return game_reaper(0, proc) != 0;
  }

  /**
   * Ends a session with its game: true once the game ran and has been gone for the grace period. A known reaper is
   * checked by pid on every poll, so the grace only spans the gap between scans, unless what ran was short-lived:
   * a step before the game, or a launcher handing over through Steam, is followed by the game within seconds.
   */
  class game_watch_t {
  public:
    using clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds SHORT_LIVED {10};
    static constexpr std::chrono::seconds SHORT_LIVED_GRACE {5};

    explicit game_watch_t(std::chrono::milliseconds grace = std::chrono::milliseconds {400}):
        grace {grace} {}

    bool ended(bool running, clock::time_point now) {
      if (running) {
        if (!seen || !was_running) {
          run_began = now;
        }
        seen = true;
        was_running = true;
        last_seen = now;
        return false;
      }
      was_running = false;
      const bool short_lived = last_seen - run_began < SHORT_LIVED;
      return seen && now - last_seen >= (short_lived ? std::max<std::chrono::milliseconds>(grace, SHORT_LIVED_GRACE) : grace);
    }

  private:
    std::chrono::milliseconds grace;
    bool seen {};
    bool was_running {};
    clock::time_point run_began {};
    clock::time_point last_seen {};
  };

  /**
   * Whether a game entry's stream is held back, so that nothing of Steam shows: until the game's window is in front
   * (the app Gamescope draws is neither none nor Steam, 769; its overlay over a game does not count), and again once Steam is back in front after it.
   * Never for ever: Steam is shown if no game window came for REVEAL_AFTER (it may be asking something), and if the
   * player went to Steam while the game runs. Without the focus (no X connection) the game running for a while counts.
   */
  class curtain_t {
  public:
    using clock = std::chrono::steady_clock;
    static constexpr std::uint32_t STEAM = 769;
    static constexpr std::chrono::seconds REVEAL_AFTER {30};
    static constexpr std::chrono::seconds STEAM_ON_PURPOSE {2};
    static constexpr std::chrono::seconds UNKNOWN_FOCUS_WAIT {3};

    bool hidden(std::optional<std::uint32_t> focused, bool game_running, clock::time_point now) {
      if (!begun) {
        begun = now;
      }
      if (!game_running) {
        running_since.reset();
      } else if (!running_since) {
        running_since = now;
      }
      const bool game_front = focused ? *focused != 0 && *focused != STEAM :
                                        running_since && now - *running_since >= UNKNOWN_FOCUS_WAIT;
      if (game_front) {
        seen = true;
        steam_since.reset();
        return false;
      }
      if (!seen) {
        return now - *begun < REVEAL_AFTER;
      }
      if (!game_running) {
        return true;  // the game quit: nothing of Steam until the session ends
      }
      if (!steam_since) {
        steam_since = now;
      }
      return now - *steam_since < STEAM_ON_PURPOSE;
    }

  private:
    std::optional<clock::time_point> begun;
    std::optional<clock::time_point> running_since;
    std::optional<clock::time_point> steam_since;
    bool seen {};
  };

}  // namespace steam_library
