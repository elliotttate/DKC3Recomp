#include "desktop_overlay_model.h"
#include <stdio.h>

static int Expect(bool condition, const char *message) {
  if (condition) return 0;
  fprintf(stderr, "%s\n", message);
  return 1;
}

int main(void) {
  Dkc3DesktopOverlayModel model;
  Dkc3DesktopOverlayModelInit(&model, false);
  if (Expect(Dkc3DesktopEscapeExitsFullscreen(true, false) &&
                 !Dkc3DesktopEscapeExitsFullscreen(false, false) &&
                 !Dkc3DesktopEscapeExitsFullscreen(true, true),
             "Escape fullscreen priority"))
    return 1;
  if (Expect(!model.open && !model.assist_tools,
             "overlay defaults must be closed with assists disabled"))
    return 1;
  if (Expect(model.selected_slot == 0, "save slots must default to slot 0"))
    return 1;
  Dkc3DesktopOverlayModelShiftSlot(&model, -1);
  if (Expect(model.selected_slot == 4,
             "save-slot decrement did not wrap from 0 to 4"))
    return 1;
  Dkc3DesktopOverlayModelShiftSlot(&model, 2);
  if (Expect(model.selected_slot == 1,
             "save-slot increment did not wrap across slot 4"))
    return 1;
  Dkc3DesktopOverlayModelSetSlot(&model, 99);
  if (Expect(model.selected_slot == 4,
             "save-slot setter did not clamp to the five-slot maximum"))
    return 1;
  Dkc3DesktopOverlayModelToggle(&model);
  if (Expect(model.open, "overlay toggle did not open the menu")) return 1;
  if (Expect(Dkc3DesktopOverlayModelBeginBindingCapture(
                 &model, kDkc3OverlayCapturePlayerKey, 1, 11),
             "valid player binding capture was rejected"))
    return 1;
  if (Expect(model.binding_capture == kDkc3OverlayCapturePlayerKey &&
                 model.capture_player == 1 && model.capture_index == 11,
             "player binding capture target was not retained"))
    return 1;
  if (Expect(!Dkc3DesktopOverlayModelBeginBindingCapture(
                  &model, kDkc3OverlayCapturePlayerPad, 2, 0),
             "out-of-range player binding capture was accepted"))
    return 1;
  Dkc3DesktopOverlayModelCancelBindingCapture(&model);
  if (Expect(model.binding_capture == kDkc3OverlayCaptureNone,
             "binding capture cancellation did not clear the target"))
    return 1;
  if (Expect(Dkc3DesktopOverlayModelBeginBindingCapture(
                 &model, kDkc3OverlayCaptureAssistPad, 0, 3),
             "valid assist binding capture was rejected"))
    return 1;
  if (Expect(!Dkc3DesktopOverlayModelArmPadCapture(&model, false),
             "held controller input armed capture without a neutral frame"))
    return 1;
  if (Expect(Dkc3DesktopOverlayModelArmPadCapture(&model, true),
             "neutral controller frame did not arm capture"))
    return 1;
  Dkc3DesktopOverlayModelSetOpen(&model, false);
  if (Expect(model.binding_capture == kDkc3OverlayCaptureNone &&
                 !model.pad_capture_armed,
             "closing the overlay did not cancel binding capture"))
    return 1;
  Dkc3DesktopOverlayModelSetOpen(&model, true);
  if (Expect(!Dkc3DesktopOverlayModelRequest(
                  &model, kDkc3OverlayActionSaveState),
             "save state bypassed the assist-tools gate"))
    return 1;
  if (Expect(Dkc3DesktopOverlayModelTakeActions(&model) == 0,
             "a rejected action leaked into the host"))
    return 1;

  Dkc3DesktopOverlayModelSetAssistTools(&model, true);
  if (Expect(Dkc3DesktopOverlayModelRequest(
                  &model, kDkc3OverlayActionSaveState),
             "enabled save state was rejected"))
    return 1;
  if (Expect(Dkc3DesktopOverlayModelTakeActions(&model) ==
                 kDkc3OverlayActionSaveState,
             "enabled save-state action was not delivered exactly once"))
    return 1;
  if (Expect(Dkc3DesktopOverlayModelTakeActions(&model) == 0,
             "overlay action was delivered more than once"))
    return 1;

  if (Expect(Dkc3DesktopOverlayModelRequest(
                  &model, kDkc3OverlayActionResume),
             "resume action was rejected"))
    return 1;
  if (Expect(!model.open, "resume did not close the overlay")) return 1;

  puts("desktop overlay model tests passed");
  return 0;
}
