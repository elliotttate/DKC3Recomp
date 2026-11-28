#ifndef DKC3_DESKTOP_LAUNCHER_H
#define DKC3_DESKTOP_LAUNCHER_H

#include "recomp_launcher.h"

#include <stdbool.h>
#include <stddef.h>

#define DKC3_PRODUCT_TITLE "DKC3Recomp"

#ifdef __cplusplus
extern "C" {
#endif

/* Persisted widescreen edge policy (a Dkc3VideoEdgePolicy value). */
int Dkc3LauncherWidescreenEdge(void);
void Dkc3LauncherSetWidescreenEdge(int policy);
/* Upscaler choice (kDkc3Upscaler*), remembered with the launcher settings;
 * the Reconstruct experiment's mode (0..3) and strength (0..100). */
int Dkc3LauncherUpscaler(void);
void Dkc3LauncherSetUpscaler(int upscaler);
int Dkc3LauncherReconstructMode(void);
void Dkc3LauncherSetReconstructMode(int mode);
int Dkc3LauncherReconstructStrength(void);
void Dkc3LauncherSetReconstructStrength(int percent);
int Dkc3LauncherReconstructSoftness(void);
void Dkc3LauncherSetReconstructSoftness(int percent);
int Dkc3LauncherReconstructShading(void);
void Dkc3LauncherSetReconstructShading(int percent);
/* DKC3-specific enemy-stomp controller feedback, persisted alongside the
 * shared launcher settings. */
int Dkc3LauncherHaptics(void);
void Dkc3LauncherSetHaptics(int enabled);
/* Whether the map shows KONG letter badges (dkc3_kong_tracker.h); on by
 * default and persisted as KongBadges. Recording is unaffected. */
int Dkc3LauncherKongBadges(void);
void Dkc3LauncherSetKongBadges(int enabled);
/* Whether the Assist Tools / Cheats option to spawn animal buddy crates from
 * the in-game pause state (Up = Squawks, Down = Enguarde) is enabled; off by
 * default and persisted as PauseCrateSpawn. */
int Dkc3LauncherPauseCrateSpawn(void);
void Dkc3LauncherSetPauseCrateSpawn(int enabled);
/* Whether the Assist Tools / Cheats option that draws sprites the game keeps
 * invisible (such as hidden bear coins) is enabled; off by default and
 * persisted as ShowHiddenObjects. */
int Dkc3LauncherShowHiddenObjects(void);
void Dkc3LauncherSetShowHiddenObjects(int enabled);

void Dkc3LauncherSettingsDefault(RecompLauncherCSettings *settings);
void Dkc3LauncherSettingsLoad(RecompLauncherCSettings *settings);
bool Dkc3LauncherSettingsSave(const RecompLauncherCSettings *settings);

bool Dkc3LauncherReadRomCache(char *path, size_t capacity);
bool Dkc3LauncherWriteRomCache(const char *path);
void Dkc3LauncherSetAssetsPath(const char *path);

/* Runs the shared recomp-ui launcher with host-specific renderer labels.
 * Pass no labels to hide the renderer selector for a single-backend host. */
int Dkc3LauncherRun(RecompLauncherCSettings *settings,
                    const char *initial_rom, char *selected_rom,
                    size_t selected_capacity,
                    const char *const *renderer_labels,
                    size_t renderer_count);

#ifdef __cplusplus
}
#endif

#endif
