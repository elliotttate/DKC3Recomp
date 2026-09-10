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
  Dkc3LauncherSetHaptics(0);
  if (Check(Dkc3LauncherSettingsSave(&settings),
            "launcher settings could not be saved"))
    return 1;

  Dkc3LauncherSetHaptics(1);
  Dkc3LauncherSettingsLoad(&settings);
  if (Check(Dkc3LauncherHaptics() == 0,
            "saved haptics setting was not restored"))
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
      Check(remove(config) == 0, "test launcher config could not be removed"))
    return 1;
  return 0;
}
