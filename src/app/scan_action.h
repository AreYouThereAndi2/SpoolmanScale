#pragma once

// ============================================================
//  WHAT THE SCALE DOES WITH A SCANNED CODE
//
//  The barcode scanner (hardware/barcode_scanner.h) delivers text, the
//  classifier (services/scan_code.h) says what it is, and this decides what
//  it is for, from what is on the pad and on screen at that moment:
//
//  A spool id (Spoolman's QR label, a spool page link, a bare number):
//    - the link flow for an unknown tag is open, or an unknown tag lies on
//      the reader: the tag is linked to that spool, exactly as if the id had
//      been typed into the keypad - same checks, same warnings, same write.
//    - a known tag lies on the reader: the tag wins, the scan is ignored
//      with a note on the status line.
//    - nothing on the reader: the spool is loaded and taken on for
//      weighing until the pad is empty again - the same adoption a FilaMan
//      remote link gets when no tag turns up.
//
//  An EAN or a Spoolman filament label:
//    the active spools of that filament are asked of Spoolman (directly, or
//    the one behind BamBuddy). One spool is treated like its id. Several are
//    told apart by the weight on the pad when there is one; without a load
//    the status line asks for the spool to be put down first.
//
//  A box code no filament carries, or a manufacturer or Amazon number, is
//  looked up in filament_to_bambuddy (services/f2b_api.h). When that knows
//  the product, the scale offers to create the spool, creates it through
//  BamBuddy, writes the code into the Spoolman filament's article_number so
//  the next scan finds it, and goes on with it like a scanned spool id.
//
//  Everything here runs on the loop and may block for a request or two,
//  like every other lookup that follows a user action.
// ============================================================

void scanActionTick();

// The new-spool question for an unknown box code is on screen.
bool scanQuestionOpen();
