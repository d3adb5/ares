/**
 * @file tests/unit/test_process_session_route.cpp
 * @brief Test host-vs-independent session routing.
 */
#include "../tests_common.h"

#include <src/process.h>

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
