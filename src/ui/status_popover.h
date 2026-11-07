#pragma once

// The status line's message in full. The line holds about forty characters
// and cuts the rest off with "...", so a tap on it opens this: the whole text,
// wrapped, and for a code nobody knew a QR code that opens
// filament_to_bambuddy on the phone with that code filled in.
//
// code: the scanned code the message is about, or nullptr. With it and an
// app address set, the popover adds the explanation and the QR code.
void showStatusPopover(const char* text, const char* code);
bool isStatusPopoverOpen();
void closeStatusPopover();
