#pragma once

#include <lvgl.h>

// ============================================================
//  THE QUICK PANEL
//
//  Pulled down from the top edge (or a tap on the handle in the header):
//  Wi-Fi and backend at a glance, the brightness, and the things done most
//  often - tare, the AMS view, the info screen, the settings. It slides in
//  over a dimmed scale and goes on a swipe up, a tap beside it, or a tap on
//  one of its buttons. Small on purpose: 480 x 200 is what the display bus
//  moves smoothly, a full-screen slide would stutter.
// ============================================================

void quickPanelOpen();
// The handle in the main header that says the panel is there; a tap on it
// opens the panel as the swipe does.
void quickPanelAddHandle(lv_obj_t* header);
// Gone at once, without the slide: before a screen change.
void quickPanelClose(bool animated = true);
bool quickPanelIsOpen();

// Once a loop pass: what a button in the panel asked for, run outside its
// event handler.
void quickPanelTick();

// A dialog's box slides up into place, as a sheet on a phone.
void uiSheetIn(lv_obj_t* box);
