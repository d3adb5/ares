/**
 * @file tests/unit/test_process_session_route.cpp
 * @brief Test host-vs-independent session routing.
 */
#include "../tests_common.h"

#include <src/process.h>

#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(ProcessSessionRoute, DisabledCapabilityAlwaysUsesHost) {
  proc::ctx_t app {};
  app.isolation_method = "kwin_virtual";

  EXPECT_EQ(
    proc::resolve_session_route(app, false),
    proc::session_route_e::shared
  );
}

TEST(ProcessSessionRoute, SharedAlwaysUsesHost) {
  proc::ctx_t app {};
  app.host_session = true;

  EXPECT_EQ(
    proc::resolve_session_route(app, true),
    proc::session_route_e::shared
  );
}

TEST(ProcessSessionRoute, EveryLaunchIsIsolatedByDefault) {
  proc::ctx_t app {};
  app.isolation_method = "default";
  app.cmd = "game";

  EXPECT_EQ(
    proc::resolve_session_route(app, true),
    proc::session_route_e::isolated_application
  );
}

TEST(ProcessSessionRoute, ExplicitGamescopeStreamIsAnIsolatedApplication) {
  proc::ctx_t app {};
  app.isolation_method = "gamescope_stream";

  EXPECT_EQ(
    proc::resolve_session_route(app, true),
    proc::session_route_e::isolated_application
  );
}

TEST(ProcessSessionRoute, ExplicitDesktopForcesIsolatedDesktop) {
  proc::ctx_t app {};
  app.isolation_method = "kwin_virtual";

  EXPECT_EQ(
    proc::resolve_session_route(app, true),
    proc::session_route_e::isolated_desktop
  );
}

TEST(ProcessSessionRoute, ExplicitDesktopWithVirtualDisplayIsIsolated) {
  proc::ctx_t app {};
  app.isolation_method = "kwin_virtual";

  EXPECT_EQ(
    proc::resolve_session_route(app, true),
    proc::session_route_e::isolated_desktop
  );
}

TEST(ProcessSessionRoute, ExplicitApplicationWithVirtualDisplayIsIsolated) {
  proc::ctx_t app {};
  app.isolation_method = "gamescope";
  app.cmd = "game";

  EXPECT_EQ(
    proc::resolve_session_route(app, true),
    proc::session_route_e::isolated_application
  );
}

TEST(ProcessSessionRoute, DetachedLayoutFollowsGlobalMethod) {
  proc::ctx_t desktop {};
  desktop.isolation_method = "default";
  desktop.virtual_display_layout = "detached";

  EXPECT_EQ(
    proc::resolve_session_route(desktop, true),
    proc::session_route_e::isolated_desktop
  );

  proc::ctx_t application {};
  application.isolation_method = "default";
  application.virtual_display_layout = "detached";
  application.cmd = "game";

  EXPECT_EQ(
    proc::resolve_session_route(application, true),
    proc::session_route_e::isolated_application
  );
}

TEST(ProcessSessionRoute, GlobalGamescopeRunsCommandsAndLeavesDesktopsToKWin) {
  proc::ctx_t game {};
  game.isolation_method = "default";
  game.cmd = "game";

  EXPECT_EQ(
    proc::resolve_session_route(game, true, "gamescope"),
    proc::session_route_e::isolated_application
  );

  proc::ctx_t desktop {};
  desktop.isolation_method = "default";

  EXPECT_EQ(
    proc::resolve_session_route(desktop, true, "gamescope"),
    proc::session_route_e::isolated_desktop
  );
}

TEST(ProcessSessionRoute, GlobalKWinRunsCommandsInTheDesktop) {
  proc::ctx_t game {};
  game.isolation_method = "default";
  game.virtual_display_layout = "auto";
  game.cmd = "game";

  EXPECT_EQ(
    proc::resolve_session_route(game, true, "kwin_virtual"),
    proc::session_route_e::isolated_desktop
  );
}

TEST(ProcessSessionRoute, GlobalMethodLeavesHostLayoutsOnTheHost) {
  for (const char *layout : {"extend", "mirror", "exclusive"}) {
    proc::ctx_t app {};
    app.isolation_method = "default";
    app.virtual_display_layout = layout;
    app.cmd = "game";

    EXPECT_EQ(
      proc::resolve_session_route(app, true, "gamescope"),
      proc::session_route_e::shared
    ) << layout;
  }
}

TEST(ProcessSessionRoute, AppMethodOverridesGlobalMethod) {
  proc::ctx_t app {};
  app.cmd = "game";

  app.isolation_method = "kwin_virtual";
  EXPECT_EQ(
    proc::resolve_session_route(app, true, "gamescope"),
    proc::session_route_e::isolated_desktop
  );

  app.isolation_method = "gamescope";
  EXPECT_EQ(
    proc::resolve_session_route(app, true, "kwin_virtual"),
    proc::session_route_e::isolated_application
  );
}

TEST(ProcessSessionRoute, HostSessionOverridesEveryMethod) {
  proc::ctx_t app {};
  app.host_session = true;
  app.cmd = "game";

  for (const char *method : {"default", "gamescope", "kwin_virtual"}) {
    app.isolation_method = method;
    EXPECT_EQ(
      proc::resolve_session_route(app, true, "kwin_virtual"),
      proc::session_route_e::shared
    ) << method;
  }
}

TEST(ProcessSessionRoute, GlobalMethodNeedsIsolatedSessionsEnabled) {
  proc::ctx_t app {};
  app.isolation_method = "default";
  app.cmd = "game";

  EXPECT_EQ(
    proc::resolve_session_route(app, false, "gamescope"),
    proc::session_route_e::shared
  );
}

TEST(ProcessSessionRoute, MigrationRenamesSessionType) {
  nlohmann::json tree = {{"apps", {
    {{"name", "a"}, {"session-type", "shared"}},
    {{"name", "b"}, {"session-type", "application"}},
    {{"name", "c"}, {"session-type", "desktop"}},
    {{"name", "d"}, {"session-type", "auto"}},
    {{"name", "e"}},
    {{"name", "Detached Desktop"}, {"uuid", DETACHED_DESKTOP_UUID}, {"session-type", "desktop"}},
    {{"name", "Detached Desktop"}, {"uuid", "mine"}},
  }}};

  proc::migration_v7(tree);

  const auto &apps = tree["apps"];
  EXPECT_FALSE(apps[0].contains("isolation-method"));
  EXPECT_EQ(apps[0]["host-session"], true);
  EXPECT_EQ(apps[1]["isolation-method"], "gamescope");
  EXPECT_EQ(apps[2]["isolation-method"], "kwin_virtual");
  EXPECT_FALSE(apps[3].contains("isolation-method"));
  EXPECT_FALSE(apps[4].contains("isolation-method"));
  EXPECT_EQ(apps[5]["name"], "Desktop (KWin virtual)");
  EXPECT_EQ(apps[6]["name"], "Detached Desktop");
  for (const auto &app : apps) {
    EXPECT_FALSE(app.contains("session-type"));
  }
  EXPECT_EQ(tree["version"], 7);
}

TEST(ProcessSteamLibrarySettings, BelongOnlyToEntriesWithTheLibrary) {
  const auto file = std::filesystem::temp_directory_path() / ("apps-steam-" + std::to_string(::getpid()) + ".json");
  std::ofstream {file} << R"({"version": 2, "apps": [
    {"name": "Emulator", "cmd": "/usr/bin/retroarch", "steam-library": "all",
     "steam-library-keep-running": true, "steam-library-keep-warm": 10, "steam-library-hide-steam": true},
    {"name": "Steam", "cmd": "steam -gamepadui", "steam-library": "off",
     "steam-library-keep-running": true, "steam-library-keep-warm": 10, "steam-library-keep-running-mode": "1280x720x60"}
  ]})";
  const auto proc = proc::parse(file.string());
  std::filesystem::remove(file);
  ASSERT_TRUE(proc);
  int found = 0;
  for (const auto &app : proc->get_apps()) {
    EXPECT_TRUE(app.steam_appid.empty()) << app.name;  // no game entries
    if (app.name != "Emulator" && app.name != "Steam") {
      continue;
    }
    ++found;
    EXPECT_FALSE(app.steam_keep_running) << app.name;
    EXPECT_EQ(app.steam_keep_warm, 0) << app.name;
    EXPECT_FALSE(app.steam_hide) << app.name;
    EXPECT_TRUE(app.steam_keep_running_mode.empty()) << app.name;
  }
  EXPECT_EQ(found, 2);
}
