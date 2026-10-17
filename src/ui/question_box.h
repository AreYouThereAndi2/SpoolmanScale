#pragma once

#include <lvgl.h>
#include <stdint.h>

// ============================================================
//  QUESTION BOX
//
//  A modal box over the whole screen: a title line, a question, an optional
//  hint, and up to three answers - green on the left, neutral in the middle,
//  red on the right. The same shape as the AMS question, for the printer and
//  new-spool questions that do not need a countdown.
//
//  A tap only writes the answer into the caller's byte; the caller reads it
//  on its next loop pass and closes the box from there. Nothing is deleted
//  inside the event.
// ============================================================

enum QbAnswer : uint8_t { QB_NONE = 0, QB_LEFT, QB_MID, QB_RIGHT };

// Table strings for the buttons; 0 leaves that slot empty. hint may be null.
// The returned object is the full-screen scrim; free it with releaseScreen().
lv_obj_t* questionBoxCreate(const char* title, const char* question, const char* hint,
                            int left_str, int mid_str, int right_str,
                            volatile uint8_t* answer_out);
