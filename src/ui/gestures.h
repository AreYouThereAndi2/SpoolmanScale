#pragma once

#include <stdint.h>

// ============================================================
//  GESTURES, AS ON A PHONE
//
//  A finger that starts at the left edge and swipes right goes back: it
//  taps the back button of the screen on top, so every screen goes back
//  exactly where its own button goes. One that starts at the top edge and
//  swipes down pulls out the quick panel (quick_panel.h).
//
//  Seen in the touch read itself, before LVGL hands the press to a widget:
//  a swipe that starts on a button would otherwise be a press on it, and
//  the slide-off guard (touch_feedback) drops it before LVGL ever calls it
//  a gesture. The action runs from gesturesTick() in the loop, never inside
//  the touch read or an event handler.
// ============================================================

// Every touch read: whether a finger is down, and where.
void gesturesTouch(bool pressed, int16_t x, int16_t y);

// Once a loop pass: runs what a finished gesture asked for.
void gesturesTick();
