/**
 * @file tests/unit/test_steam_library.cpp
 * @brief Test src/steam_library.h: the local Steam library's games.
 */
#include "../tests_common.h"

#include <src/steam_library.h>

#include <unistd.h>

namespace {
  void put32(std::string &out, std::uint32_t value) {
    out.append(reinterpret_cast<const char *>(&value), 4);
  }

  /// appinfo.vdf v29: entries whose binary KV keys index a string table at the end.
  std::string appinfo_v29(const std::vector<std::tuple<std::uint32_t, std::string, std::string>> &apps) {
    const std::vector<std::string> strings {"appinfo", "common", "name", "type", "gameid"};
    std::string body;
    for (const auto &[appid, name, type] : apps) {
      std::string kv;
      kv += '\0';
      put32(kv, 0);  // appinfo
      kv += '\0';
      put32(kv, 1);  // common
      kv += '\x01';
      put32(kv, 2);
      kv += name + '\0';
      kv += '\x02';
      put32(kv, 4);  // an int to skip
      put32(kv, appid);
      kv += '\x01';
      put32(kv, 3);
      kv += type + '\0';
      kv += "\x08\x08\x08";
      put32(body, appid);
      put32(body, static_cast<std::uint32_t>(60 + kv.size()));
      body += std::string(60, '\0');
      body += kv;
    }
    put32(body, 0);
    std::string file;
    put32(file, 0x07564429);
    put32(file, 1);
    const std::uint64_t table = 16 + body.size();
    file.append(reinterpret_cast<const char *>(&table), 8);
    file += body;
    put32(file, static_cast<std::uint32_t>(strings.size()));
    for (const auto &string : strings) {
      file += string + '\0';
    }
    return file;
  }
}  // namespace

TEST(SteamLibrary, ReadsTextVdfValues) {
  const auto text = R"("libraryfolders"
{
	"0"
	{
		"path"		"/home/u/.local/share/Steam"
		"apps" { "620" "1" }
	}
	"1" { "path" "/mnt/games/Steam" }
})";
  EXPECT_EQ(steam_library::text_vdf_values(text, "path"), (std::vector<std::string> {"/home/u/.local/share/Steam", "/mnt/games/Steam"}));
}

TEST(SteamLibrary, ReadsNamesAndTypesFromAppinfo) {
  const auto apps = steam_library::read_appinfo(appinfo_v29({{620, "Portal 2", "Game"}, {1493710, "Proton Experimental", "Tool"}}));
  ASSERT_EQ(apps.size(), 2u);
  EXPECT_EQ(apps.at(620).name, "Portal 2");
  EXPECT_EQ(apps.at(620).type, "Game");
  EXPECT_EQ(apps.at(1493710).type, "Tool");
}

TEST(SteamLibrary, RefusesOtherFiles) {
  EXPECT_TRUE(steam_library::read_appinfo("").empty());
  EXPECT_TRUE(steam_library::read_appinfo("not an appinfo file at all").empty());
  auto truncated = appinfo_v29({{620, "Portal 2", "Game"}});
  truncated.resize(40);
  EXPECT_TRUE(steam_library::read_appinfo(truncated).empty());
}

TEST(SteamLibrary, KeepsGamesOnly) {
  const std::map<std::uint32_t, std::string> installed {{620, "Portal 2"}, {1493710, "Proton Experimental"}, {1628350, "Steam Linux Runtime 3.0 (sniper)"}, {9, "zeta"}};
  const std::set<std::uint32_t> owned {620, 400, 228980, 7};
  const std::map<std::uint32_t, steam_library::app_info_t> appinfo {
    {620, {"Portal 2", "Game"}},
    {400, {"Portal", "game"}},
    {1493710, {"Proton Experimental", "Tool"}},
    {228980, {"Steamworks Common Redistributables", "Config"}},
  };
  const auto games = steam_library::games_from(installed, owned, appinfo);
  ASSERT_EQ(games.size(), 3u);
  EXPECT_EQ(games[0].name, "Portal");
  EXPECT_FALSE(games[0].installed);
  EXPECT_EQ(games[1].name, "Portal 2");
  EXPECT_TRUE(games[1].installed);
  EXPECT_EQ(games[2].name, "zeta");  // installed, unknown to appinfo, not a tool
}

TEST(SteamLibrary, StableIdentityAndCommand) {
  EXPECT_EQ(steam_library::uuid_for(620), "5354454D-0000-4000-8000-00000000026C");
  EXPECT_EQ(steam_library::launch_command("steam -gamepadui ", 620), "steam -gamepadui steam://rungameid/620");
  EXPECT_FALSE(steam_library::parse_appid("12a"));
  EXPECT_FALSE(steam_library::parse_appid("0"));
  EXPECT_EQ(steam_library::parse_appid("620"), 620u);
}

TEST(SteamLibrary, ScansInstalledOnlyOrOwnedToo) {
  const auto root = std::filesystem::temp_directory_path() / ("steam-library-test-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "steamapps");
  std::filesystem::create_directories(root / "userdata" / "1" / "config" / "librarycache");
  std::ofstream {root / "steamapps" / "libraryfolders.vdf"} << "\"libraryfolders\" { \"0\" { \"path\" \"" << root.string() << "\" } }";
  std::ofstream {root / "steamapps" / "appmanifest_620.acf"} << "\"AppState\" { \"appid\" \"620\" \"name\" \"Portal 2\" }";
  std::ofstream {root / "steamapps" / "appmanifest_1493710.acf"} << "\"AppState\" { \"appid\" \"1493710\" \"name\" \"Proton Experimental\" }";
  std::ofstream {root / "userdata" / "1" / "config" / "librarycache" / "400.json"} << "[]";
  std::filesystem::create_directories(root / "appcache");
  {
    std::ofstream out {root / "appcache" / "appinfo.vdf", std::ios::binary};
    out << appinfo_v29({{400, "Portal", "Game"}});
  }

  const auto installed = steam_library::scan(root, false);
  ASSERT_EQ(installed.size(), 1u);
  EXPECT_EQ(installed[0].name, "Portal 2");
  const auto all = steam_library::scan(root, true);
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].name, "Portal");
  EXPECT_FALSE(all[0].installed);

  const auto before = steam_library::signature(root);
  EXPECT_EQ(before, "400o\n620i\n");
  std::filesystem::remove(root / "steamapps" / "appmanifest_620.acf");  // uninstalled
  EXPECT_NE(steam_library::signature(root), before);
  std::filesystem::remove_all(root);
}

TEST(SteamLibrary, FindsTheGameByItsReaper) {
  using namespace std::string_literals;
  const auto proc = std::filesystem::temp_directory_path() / ("steam-proc-test-" + std::to_string(::getpid()));
  const auto process = [&](const std::string &pid, const std::string &cmdline) {
    std::filesystem::create_directories(proc / pid);
    std::ofstream {proc / pid / "cmdline", std::ios::binary} << cmdline;
  };
  process("self", "reaper\0SteamLaunch\0AppId=620\0"s);
  process("10", "steam\0-gamepadui\0steam://rungameid/620\0"s);
  process("11", "reaper\0SteamLaunch\0AppId=6200\0--\0game\0"s);
  process("12", "wrapper\0--\0SteamLaunch\0AppId=620\0"s);
  EXPECT_FALSE(steam_library::game_running(620, proc));
  process("13", "/s/ubuntu12_32/reaper\0SteamLaunch\0AppId=620\0--\0/s/proton\0"s);
  EXPECT_TRUE(steam_library::game_running(620, proc));
  EXPECT_EQ(steam_library::game_reaper(620, proc), 13);
  EXPECT_TRUE(steam_library::is_reaper(620, proc / "13"));
  EXPECT_FALSE(steam_library::is_reaper(620, proc / "11"));
  EXPECT_FALSE(steam_library::is_reaper(620, proc / "999"));  // gone
  std::filesystem::remove_all(proc);
}

TEST(SteamLibrary, GameWatchEndsOnlyAfterTheGameRanAndLeft) {
  using namespace std::chrono_literals;
  steam_library::game_watch_t watch {3s};
  const steam_library::game_watch_t::clock::time_point t0 {};
  EXPECT_FALSE(watch.ended(false, t0 + 60s));  // still in Big Picture or shader processing
  EXPECT_FALSE(watch.ended(true, t0 + 61s));
  EXPECT_FALSE(watch.ended(false, t0 + 62s));  // a launcher handing over
  EXPECT_FALSE(watch.ended(true, t0 + 63s));
  EXPECT_FALSE(watch.ended(false, t0 + 65s));
  EXPECT_TRUE(watch.ended(false, t0 + 66s));
}

namespace {
  /// shortcuts.vdf: binary KV with inline keys, as Steam writes it.
  std::string shortcuts_vdf(const std::vector<std::tuple<std::uint32_t, std::string, bool>> &games) {
    std::string out;
    out += '\0';
    out += "shortcuts";
    out += '\0';
    int index = 0;
    for (const auto &[appid, name, hidden] : games) {
      out += '\0';
      out += std::to_string(index++) + '\0';
      out += '\x02';
      out += "appid";
      out += '\0';
      put32(out, appid);
      out += '\x01';
      out += "AppName";
      out += '\0';
      out += name + '\0';
      out += '\x02';
      out += "IsHidden";
      out += '\0';
      put32(out, hidden ? 1 : 0);
      out += '\0';
      out += "tags";
      out += '\0';
      out += '\x01';
      out += "0";
      out += '\0';
      out += "favorite";
      out += '\0';
      out += '\x08';
      out += '\x08';
    }
    out += "\x08\x08";
    return out;
  }
}  // namespace

TEST(SteamLibrary, ReadsNonSteamShortcuts) {
  const auto games = steam_library::read_shortcuts(shortcuts_vdf({
    {3927187888u, "The Wind Waker HD", false},
    {2284676767u, "Hidden one", true},
    {4139005981u, "Super Mario Odyssey", false},
  }));
  ASSERT_EQ(games.size(), 2u);
  EXPECT_EQ(games[0].appid, 3927187888u);
  EXPECT_EQ(games[0].name, "The Wind Waker HD");
  EXPECT_TRUE(games[0].shortcut);
  EXPECT_TRUE(games[0].installed);
  EXPECT_EQ(games[1].name, "Super Mario Odyssey");
  EXPECT_TRUE(steam_library::read_shortcuts("garbage").empty());
}

TEST(SteamLibrary, LaunchesNonSteamGamesByTheirGameId) {
  // (appid << 32) | 0x02000000, as Steam's own "Create desktop shortcut" writes it.
  EXPECT_EQ(steam_library::game_url(3927187888u, true), "steam://rungameid/16867143544240865280");
  // Steam starts without it and is asked once its UI is up.
  EXPECT_EQ(steam_library::launch_command("steam -gamepadui ", 3927187888u, true), "steam -gamepadui");
  EXPECT_EQ(steam_library::send_command(" /usr/bin/steam -gamepadui", 3927187888u, true),
            "/usr/bin/steam -ifrunning steam://rungameid/16867143544240865280");
}

TEST(SteamLibrary, UiIsReadyOnceItsLogSaysSoAfterTheStart) {
  const auto log = std::filesystem::temp_directory_path() / ("steam-ui-log-test-" + std::to_string(::getpid()));
  const std::string old_run = "SteamUI: VERBOSE: SteamApp Init - After Login total time: 208 ms\n";
  std::ofstream {log} << old_run;
  const auto from = steam_library::file_size_or_zero(log);
  EXPECT_FALSE(steam_library::ui_ready(log, from));
  std::ofstream {log, std::ios::app} << "SteamUI: INFO: Login\n";
  EXPECT_FALSE(steam_library::ui_ready(log, from));
  std::ofstream {log, std::ios::app} << old_run;
  EXPECT_TRUE(steam_library::ui_ready(log, from));
  std::ofstream {log} << old_run;  // rotated: shorter than at the start
  EXPECT_TRUE(steam_library::ui_ready(log, from + 100));
  std::filesystem::remove(log);
  EXPECT_FALSE(steam_library::ui_ready(log, 0));
  EXPECT_EQ(steam_library::file_size_or_zero(log), 0u);
}

TEST(SteamLibrary, SkipsTheIntroWithAnEmptyOverrideAndTakesOnlyItsOwnBack) {
  const auto root = std::filesystem::temp_directory_path() / ("steam-intro-test-" + std::to_string(::getpid()));
  const auto file = steam_library::intro_override(root);
  steam_library::skip_intro(root);  // no Steam there
  EXPECT_FALSE(std::filesystem::exists(file));
  std::filesystem::create_directories(root / "config");
  steam_library::skip_intro(root);
  EXPECT_TRUE(std::filesystem::exists(file));
  EXPECT_EQ(std::filesystem::file_size(file), 0u);
  steam_library::restore_intro(root);
  EXPECT_FALSE(std::filesystem::exists(root / "config" / "uioverrides"));
  EXPECT_TRUE(std::filesystem::exists(root / "config"));

  // The user's own override stays, through both.
  std::filesystem::create_directories(file.parent_path());
  std::ofstream {file} << "webm";
  steam_library::skip_intro(root);
  steam_library::restore_intro(root);
  EXPECT_EQ(std::filesystem::file_size(file), 4u);
  std::filesystem::remove_all(root);
}

TEST(SteamLibrary, ScansNonSteamGamesWithTheirArt) {
  const auto root = std::filesystem::temp_directory_path() / ("steam-shortcuts-test-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "steamapps");
  std::filesystem::create_directories(root / "userdata" / "1" / "config" / "grid");
  {
    std::ofstream out {root / "userdata" / "1" / "config" / "shortcuts.vdf", std::ios::binary};
    out << shortcuts_vdf({{3927187888u, "Wind Waker", false}});
  }
  std::ofstream {root / "userdata" / "1" / "config" / "grid" / "3927187888p.png"} << "png";
  std::ofstream {root / "steamapps" / "appmanifest_620.acf"} << "\"AppState\" { \"appid\" \"620\" \"name\" \"Portal 2\" }";

  EXPECT_EQ(steam_library::scan(root, false).size(), 1u);
  const auto games = steam_library::scan(root, false, true);
  ASSERT_EQ(games.size(), 2u);
  EXPECT_EQ(games[0].name, "Portal 2");
  EXPECT_TRUE(games[1].shortcut);
  EXPECT_EQ(steam_library::shortcut_image(root, 3927187888u),
            (root / "userdata" / "1" / "config" / "grid" / "3927187888p.png").string());
  EXPECT_NE(steam_library::signature(root).find("3927187888sWind Waker"), std::string::npos);
  std::filesystem::remove_all(root);
}
