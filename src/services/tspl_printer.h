#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/ble_service.h"
#include "services/label_raster.h"

// ============================================================
//  TSPL LABEL PRINTERS
//
//  The TYPONOS PM240 takes TSPL, the text command language of
//  TSC-style label printers: a few lines that set label size,
//  gap and heat, one BITMAP with the rows, PRINT. Read off a
//  Bluetooth capture of the vendor app (Labelnize, 17.10.2026).
//
//  The PM240 prints over classic Bluetooth SPP only. An HCI snoop
//  of the vendor app (Labelife, 16.11.2026) showed it opening
//  RFCOMM and sending plain TSPL; the GATT services the printer
//  also lists (ff00 with ff02/ff04, ae00/ae01, ae3a/ae3b) accept
//  the same job on every write characteristic and print nothing,
//  with or without the ff03 subscription. The ESP32-S3 has no
//  classic Bluetooth, so the scale hands the job to the print
//  bridge (services/print_bridge.h), an ESP32 that has it.
//
//  The job is built once, here, and goes either way: to the
//  bridge for the PM240, or over BLE for a TSPL printer of the
//  Phomemo PM241/PM245 class, which do print over that GATT pair.
// ============================================================

// The Phomemo GATT pair; ff02 is write without response (nRF Connect on the
// PM240, 17.10.2026). The BLE path below writes there.
#define TSPL_SERVICE_UUID 0xff00
#define TSPL_WRITE_UUID   0xff02

// The print row of the PM240's head: 48 mm, 384 dots at 203 dpi. The app
// sends 48 bytes a row for a 50 mm label, centred on it.
#define TSPL_HEAD_DOTS 384

// One TSPL job in one buffer: the header lines, the inverted bitmap, the
// PRINT line. In PSRAM where there is some, ~12 kB for a 50 x 30 mm label.
struct TsplJob {
  uint8_t* data;
  size_t   len;
};

// Builds the job for one raster on a label of media_w x media_l mm. A row
// wider than the head is cut to its middle 384 dots, the part the head sees.
// False when the raster is not valid or the buffer could not be had.
bool tsplBuildJob(const LabelRaster& image, uint16_t media_w_mm, uint16_t media_l_mm,
                  TsplJob* out);
void tsplFreeJob(TsplJob* job);

// Builds the job and sends it over BLE to the GATT pair above.
BleWriteResult tsplPrint(const char* address, const LabelRaster& image,
                         uint16_t media_w_mm, uint16_t media_l_mm, BleProgressFn progress);
