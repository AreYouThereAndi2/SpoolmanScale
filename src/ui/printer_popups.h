#pragma once

#include <lvgl.h>

// ============================================================
//  PRINTER QUESTIONS (BamBuddy)
//
//  Two things on screen that belong to the printer rather than to a spool:
//
//  The power chip in the AMS view's header. It shows the state of the
//  printer's smart plug and switches it: on at once, off either at once or
//  once the printer has cooled down.
//
//  The question after a print, raised by services/printer_watch.h when
//  BamBuddy's archive has a finished job nobody has judged yet. Three steps
//  in one box: did it come out well (written to BamBuddy as the job's
//  verdict), is a spool empty (the next spool on the pad is then offered for
//  archiving) or does it need weighing, and should the printer go off.
//
//  Buttons only raise flags; every request runs from printerPopupsTick().
// ============================================================

// The chip, created by the AMS view in its header. Painted from the cached
// plug state; printerPopupsTick() reads the plug when the chip appears.
lv_obj_t* printerPowerChipCreate(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                 lv_coord_t w, lv_coord_t h);
// The AMS view lets go of its screen: the chip goes with it.
void printerPowerChipForget();

bool isPrinterPopupOpen();
void closePrinterPopup();

// From appLoop().
void printerPopupsTick();
