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
  std::filesystem::remove_all(root);
}
