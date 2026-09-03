#ifndef DKC3_DESKTOP_OVERLAY_MODEL_H
#define DKC3_DESKTOP_OVERLAY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  kDkc3OverlayActionNone = 0,
  kDkc3OverlayActionResume = 1u << 0,
  kDkc3OverlayActionQuit = 1u << 1,
  kDkc3OverlayActionSaveState = 1u << 2,
  kDkc3OverlayActionLoadState = 1u << 3,
};

typedef enum Dkc3OverlayBindingCapture {
  kDkc3OverlayCaptureNone = 0,
  kDkc3OverlayCapturePlayerKey,
  kDkc3OverlayCapturePlayerPad,
  kDkc3OverlayCaptureAssistKey,
  kDkc3OverlayCaptureAssistPad,
} Dkc3OverlayBindingCapture;

typedef struct Dkc3DesktopOverlayModel {
  uint32_t pending_actions;
  int selected_slot;
  int capture_player;
  int capture_index;
  Dkc3OverlayBindingCapture binding_capture;
  bool open;
  bool assist_tools;
  bool pad_capture_armed;
} Dkc3DesktopOverlayModel;

void Dkc3DesktopOverlayModelInit(Dkc3DesktopOverlayModel *model,
                                 bool assist_tools);
void Dkc3DesktopOverlayModelSetOpen(Dkc3DesktopOverlayModel *model,
                                    bool open);
void Dkc3DesktopOverlayModelToggle(Dkc3DesktopOverlayModel *model);
void Dkc3DesktopOverlayModelSetAssistTools(Dkc3DesktopOverlayModel *model,
                                           bool enabled);
void Dkc3DesktopOverlayModelSetSlot(Dkc3DesktopOverlayModel *model, int slot);
void Dkc3DesktopOverlayModelShiftSlot(Dkc3DesktopOverlayModel *model,
                                      int delta);
bool Dkc3DesktopOverlayModelRequest(Dkc3DesktopOverlayModel *model,
                                    uint32_t action);
uint32_t Dkc3DesktopOverlayModelTakeActions(Dkc3DesktopOverlayModel *model);
bool Dkc3DesktopOverlayModelBeginBindingCapture(
    Dkc3DesktopOverlayModel *model, Dkc3OverlayBindingCapture capture,
    int player, int index);
void Dkc3DesktopOverlayModelCancelBindingCapture(
    Dkc3DesktopOverlayModel *model);
bool Dkc3DesktopOverlayModelBindingCaptureIsPad(
    const Dkc3DesktopOverlayModel *model);
bool Dkc3DesktopOverlayModelArmPadCapture(
    Dkc3DesktopOverlayModel *model, bool gamepad_neutral);

/* Escape leaves fullscreen before it is offered to the closed pause menu.
 * Once windowed, or while the menu is already open, Escape retains its
 * normal overlay/capture behavior. */
bool Dkc3DesktopEscapeExitsFullscreen(bool fullscreen, bool overlay_open);

#ifdef __cplusplus
}
#endif

#endif
