#include "desktop_overlay_model.h"

static bool IsAssistAction(uint32_t action) {
  return (action &
          (kDkc3OverlayActionSaveState | kDkc3OverlayActionLoadState)) != 0;
}

void Dkc3DesktopOverlayModelInit(Dkc3DesktopOverlayModel *model,
                                 bool assist_tools) {
  if (!model) return;
  model->pending_actions = 0;
  model->selected_slot = 0;
  model->capture_player = 0;
  model->capture_index = 0;
  model->binding_capture = kDkc3OverlayCaptureNone;
  model->open = false;
  model->assist_tools = assist_tools;
  model->pad_capture_armed = false;
}

void Dkc3DesktopOverlayModelSetOpen(Dkc3DesktopOverlayModel *model,
                                    bool open) {
  if (!model) return;
  model->open = open;
  if (!open) Dkc3DesktopOverlayModelCancelBindingCapture(model);
}

void Dkc3DesktopOverlayModelToggle(Dkc3DesktopOverlayModel *model) {
  if (!model) return;
  model->open = !model->open;
  if (!model->open) Dkc3DesktopOverlayModelCancelBindingCapture(model);
}

void Dkc3DesktopOverlayModelSetAssistTools(Dkc3DesktopOverlayModel *model,
                                           bool enabled) {
  if (!model) return;
  model->assist_tools = enabled;
}

void Dkc3DesktopOverlayModelSetSlot(Dkc3DesktopOverlayModel *model, int slot) {
  if (!model) return;
  if (slot < 0) slot = 0;
  if (slot > 4) slot = 4;
  model->selected_slot = slot;
}

void Dkc3DesktopOverlayModelShiftSlot(Dkc3DesktopOverlayModel *model,
                                      int delta) {
  if (!model || delta == 0) return;
  int slot = (model->selected_slot + delta) % 5;
  if (slot < 0) slot += 5;
  model->selected_slot = slot;
}

bool Dkc3DesktopOverlayModelRequest(Dkc3DesktopOverlayModel *model,
                                    uint32_t action) {
  if (!model || action == kDkc3OverlayActionNone) return false;
  if (IsAssistAction(action) && !model->assist_tools) return false;
  model->pending_actions |= action;
  if (action & kDkc3OverlayActionResume) {
    model->open = false;
    Dkc3DesktopOverlayModelCancelBindingCapture(model);
  }
  return true;
}

uint32_t Dkc3DesktopOverlayModelTakeActions(Dkc3DesktopOverlayModel *model) {
  if (!model) return 0;
  uint32_t actions = model->pending_actions;
  model->pending_actions = 0;
  return actions;
}

bool Dkc3DesktopOverlayModelBeginBindingCapture(
    Dkc3DesktopOverlayModel *model, Dkc3OverlayBindingCapture capture,
    int player, int index) {
  if (!model || !model->open || capture <= kDkc3OverlayCaptureNone ||
      capture > kDkc3OverlayCaptureAssistPad)
    return false;
  bool player_capture = capture == kDkc3OverlayCapturePlayerKey ||
                        capture == kDkc3OverlayCapturePlayerPad;
  int maximum = player_capture ? 12 : 4;
  if ((player_capture && (player < 0 || player >= 2)) ||
      index < 0 || index >= maximum)
    return false;
  model->binding_capture = capture;
  model->capture_player = player_capture ? player : 0;
  model->capture_index = index;
  model->pad_capture_armed = false;
  return true;
}

void Dkc3DesktopOverlayModelCancelBindingCapture(
    Dkc3DesktopOverlayModel *model) {
  if (!model) return;
  model->binding_capture = kDkc3OverlayCaptureNone;
  model->capture_player = 0;
  model->capture_index = 0;
  model->pad_capture_armed = false;
}

bool Dkc3DesktopOverlayModelBindingCaptureIsPad(
    const Dkc3DesktopOverlayModel *model) {
  return model &&
         (model->binding_capture == kDkc3OverlayCapturePlayerPad ||
          model->binding_capture == kDkc3OverlayCaptureAssistPad);
}

bool Dkc3DesktopOverlayModelArmPadCapture(
    Dkc3DesktopOverlayModel *model, bool gamepad_neutral) {
  if (!Dkc3DesktopOverlayModelBindingCaptureIsPad(model)) return false;
  if (!model->pad_capture_armed && gamepad_neutral)
    model->pad_capture_armed = true;
  return model->pad_capture_armed;
}

bool Dkc3DesktopEscapeExitsFullscreen(bool fullscreen, bool overlay_open) {
  return fullscreen && !overlay_open;
}
