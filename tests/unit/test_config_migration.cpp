/**
 * @file tests/unit/test_config_migration.cpp
 * @brief Test config::migrate_option_names: Hermes' option names in an ares.conf.
 */
#include "../tests_common.h"

#include <src/config.h>

TEST(ConfigMigration, RenamesOptionsThatNeverNeededHermesKms) {
  EXPECT_EQ(
    config::migrate_option_names("gamepad = switch\nhermes_kms_isolated_sessions = true\n  hermes_kms_session_compositor=weston\n"),
    "gamepad = switch\nisolated_sessions = true\n  session_compositor=weston\n"
  );
}

TEST(ConfigMigration, KeepsWhatStillNeedsHermesKmsAndComments) {
  constexpr std::string_view conf = "hermes_kms_multi_output = true\n# hermes_kms_isolated_sessions = false\nvirtual_display_backend = hermes_kms";
  EXPECT_EQ(config::migrate_option_names(conf), conf);
}

TEST(ConfigMigration, TheNewNameWins) {
  EXPECT_EQ(
    config::migrate_option_names("hermes_kms_isolated_sessions = false\nisolated_sessions = true\n"),
    "isolated_sessions = true\n"
  );
}

TEST(ConfigMigration, MigratedConfigParsesUnderTheNewName) {
  const auto vars = config::parse_config(config::migrate_option_names("hermes_kms_isolated_sessions = false\n"));
  EXPECT_EQ(vars.count("hermes_kms_isolated_sessions"), 0);
  EXPECT_EQ(vars.at("isolated_sessions"), "false");
}
