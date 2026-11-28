#include "desktop_launcher.h"

#include <stdio.h>
#include <string.h>

static int Check(int condition, const char *message) {
  if (condition) return 0;
  fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

int recomp_launcher_run_window(const char *window_title,
                               RecompLauncherCSettings *settings,
                               const RecompLauncherCGameInfo *game,
                               const char *assets_dir,
                               const char *initial_rom,
                               char *out_rom_path,
                               size_t out_rom_path_len) {
  (void)window_title;
  (void)settings;
  (void)game;
  (void)assets_dir;
  (void)initial_rom;
  (void)out_rom_path;
  (void)out_rom_path_len;
  return RECOMP_LAUNCHER_RESULT_QUIT;
}

int main(void) {
  const char *config = "launcher.cfg";
  (void)remove(config);

  RecompLauncherCSettings settings;
  Dkc3LauncherSettingsDefault(&settings);
  if (Check(Dkc3LauncherHaptics() == 1,
            "haptics did not default to enabled"))
    return 1;
  if (Check(Dkc3LauncherKongBadges() == 1,
            "KONG badges did not default to enabled"))
    return 1;
  if (Check(Dkc3LauncherPauseCrateSpawn() == 0,
            "pause crate spawn did not default to disabled"))
    return 1;
  if (Check(Dkc3LauncherShowHiddenObjects() == 0,
            "show hidden objects did not default to disabled"))
    return 1;
  Dkc3LauncherSetHaptics(0);
  Dkc3LauncherSetKongBadges(0);
  Dkc3LauncherSetPauseCrateSpawn(1);
  Dkc3LauncherSetShowHiddenObjects(1);
  if (Check(Dkc3LauncherSettingsSave(&settings),
            "launcher settings could not be saved"))
    return 1;

  Dkc3LauncherSetHaptics(1);
  Dkc3LauncherSetKongBadges(1);
  Dkc3LauncherSetPauseCrateSpawn(0);
  Dkc3LauncherSetShowHiddenObjects(0);
  Dkc3LauncherSettingsLoad(&settings);
  if (Check(Dkc3LauncherHaptics() == 0,
            "saved haptics setting was not restored") ||
      Check(Dkc3LauncherKongBadges() == 0,
            "saved KONG badge setting was not restored") ||
      Check(Dkc3LauncherPauseCrateSpawn() == 1,
            "saved pause crate spawn setting was not restored") ||
      Check(Dkc3LauncherShowHiddenObjects() == 1,
            "saved show hidden objects setting was not restored"))
    return 1;

  FILE *file = fopen(config, "r");
  if (Check(file != NULL, "saved launcher config could not be reopened"))
    return 1;
  char contents[4096] = {0};
  if (Check(fread(contents, 1, sizeof contents - 1, file) > 0,
            "saved launcher config was empty") ||
      Check(fclose(file) == 0, "saved launcher config could not be closed") ||
      Check(strstr(contents, "HapticsEnabled=0\n") != NULL,
            "saved launcher config omitted haptics") ||
      Check(strstr(contents, "KongBadges=0\n") != NULL,
            "saved launcher config omitted KONG badges") ||
      Check(strstr(contents, "PauseCrateSpawn=1\n") != NULL,
            "saved launcher config omitted pause crate spawn") ||
      Check(strstr(contents, "ShowHiddenObjects=1\n") != NULL,
            "saved launcher config omitted show hidden objects") ||
      Check(remove(config) == 0, "test launcher config could not be removed"))
    return 1;
  return 0;
}
