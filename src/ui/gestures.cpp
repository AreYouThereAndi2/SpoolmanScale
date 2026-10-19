#include "gestures.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdlib.h>

#include "hardware/sd_logger.h"
#include "ui/quick_panel.h"
#include "ui/ui_common.h"

// Where a swipe has to start: the outermost strip of the glass. Wide enough
// for a thumb on the bezel, narrow enough that a list row is not taken for it.
#define EDGE_PX          22
// How far it has to go, and how far it may stray the other way.
#define SWIPE_MIN_PX     70
#define SWIPE_STRAY_PX   60
// A slow drag is someone moving a slider, not a swipe.
#define SWIPE_MAX_MS     700

enum GestureStart : uint8_t { START_NONE, START_LEFT, START_TOP, START_PANEL };

static GestureStart s_start = START_NONE;
static int16_t s_x0 = 0, s_y0 = 0;
static uint32_t s_t0 = 0;
static bool s_was_down = false;
static bool s_back_pending = false;
static bool s_panel_pending = false;
static bool s_panel_close_pending = false;

void gesturesTouch(bool pressed, int16_t x, int16_t y) {
  if (!pressed) {
    s_was_down = false;
    s_start = START_NONE;
    return;
  }
  if (!s_was_down) {
    s_was_down = true;
    s_x0 = x;
    s_y0 = y;
    s_t0 = millis();
    if (quickPanelIsOpen()) s_start = START_PANEL;
    else if (x <= EDGE_PX) s_start = START_LEFT;
    else if (y <= EDGE_PX) s_start = START_TOP;
    else s_start = START_NONE;
    return;
  }
  if (s_start == START_NONE) return;
  if (millis() - s_t0 > SWIPE_MAX_MS) {
    s_start = START_NONE;
    return;
  }
  const int16_t dx = x - s_x0, dy = y - s_y0;
  bool done = false;
  if (s_start == START_LEFT && dx >= SWIPE_MIN_PX && abs(dy) <= SWIPE_STRAY_PX) {
    s_back_pending = done = true;
  } else if (s_start == START_TOP && dy >= SWIPE_MIN_PX / 2 + 20 && abs(dx) <= SWIPE_STRAY_PX) {
    s_panel_pending = done = true;
  } else if (s_start == START_PANEL && -dy >= SWIPE_MIN_PX / 2 + 20 && abs(dx) <= SWIPE_STRAY_PX) {
    s_panel_close_pending = done = true;
  }
  if (done) {
    s_start = START_NONE;
    // What the finger started on is not pressed any more: no tap on it when
    // the finger lifts.
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) lv_indev_wait_release(indev);
  }
}

// The back button inside `obj`, searched a few levels down: the one
// addBackButton() marks, else the close button addCloseButton() marks.
static lv_obj_t* findMarked(lv_obj_t* obj, lv_obj_flag_t flag, int depth) {
  const uint32_t n = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t* c = lv_obj_get_child(obj, i);
    if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
    if (lv_obj_has_flag(c, flag)) return c;
    if (depth > 0) {
      lv_obj_t* f = findMarked(c, flag, depth - 1);
      if (f) return f;
    }
  }
  return nullptr;
}

// The screen on top: the last visible full-size child of the active screen.
// None means the main screen itself is what shows.
static lv_obj_t* topOverlay() {
  lv_obj_t* scr = lv_scr_act();
  for (int32_t i = (int32_t)lv_obj_get_child_cnt(scr) - 1; i >= 0; i--) {
    lv_obj_t* c = lv_obj_get_child(scr, i);
    if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
    if (lv_obj_get_width(c) >= LV_HOR_RES && lv_obj_get_height(c) >= LV_VER_RES) return c;
  }
  return nullptr;
}

static void goBack() {
  lv_obj_t* top = topOverlay();
  if (!top) return;
  lv_obj_t* btn = findMarked(top, UI_FLAG_BACK_BTN, 3);
  if (!btn) btn = findMarked(top, UI_FLAG_CLOSE_BTN, 3);
  if (!btn) {
    logSD("Gesture: back, but the screen on top has no back button");
    return;
  }
  logSD("Gesture: back");
  lv_event_send(btn, LV_EVENT_CLICKED, nullptr);
}

void gesturesTick() {
  if (s_back_pending) {
    s_back_pending = false;
    // The panel first, as on a phone: back closes what slid in last.
    if (quickPanelIsOpen()) quickPanelClose();
    else goBack();
  }
  if (s_panel_close_pending) {
    s_panel_close_pending = false;
    quickPanelClose();
  }
  if (s_panel_pending) {
    s_panel_pending = false;
    // Not over a question waiting for its answer.
    if (!uiModalWaiting()) quickPanelOpen();
  }
  quickPanelTick();
}
