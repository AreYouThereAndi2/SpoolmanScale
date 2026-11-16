#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/ble_service.h"
#include "services/label_raster.h"

// ============================================================
//  THE LABEL PRINTER
//
//  Which printer the scale has, what label stock is in it, and
//  the one call that puts a raster on paper. The printer is a
//  BLE device picked on the device card, or, for a printer that
//  only speaks classic Bluetooth, the print bridge on the WLAN
//  (services/print_bridge.h) whose address is typed in the
//  browser. The model and the loaded label size are set on the
//  printer screen or in the browser. Models are profiles: head
//  width, the label sizes they take, whether anyone has printed
//  on one yet, and which way the job goes.
//
//  Millimetres are what a person reads off the label roll; dots
//  are what the head wants, at 203 dpi. The conversions live
//  here so the renderer and the screens agree on them.
// ============================================================

// Stored in NVS as the number: a new model is appended, an old one never
// renumbered.
enum LabelPrinterModel : uint8_t {
  LP_MODEL_NONE = 0, LP_MODEL_M220 = 1, LP_MODEL_M110 = 2, LP_MODEL_M100 = 3,
  LP_MODEL_PM240 = 4,
  // The same PM240, reached through the print bridge: it prints over classic
  // Bluetooth only, which the ESP32-S3 does not have (16.11.2026).
  LP_MODEL_PM240_BRIDGE = 5
};

// The bridge's address as typed: "printbridge.local", an IP, either with
// ":port". Its API key as set on the bridge's own page, empty for none.
#define LP_BRIDGE_HOST_LEN 64
#define LP_BRIDGE_KEY_LEN  48

struct LabelPrinterProfile {
  LabelPrinterModel model;
  const char* name;
  uint16_t default_width_mm, default_length_mm;
  uint16_t min_width_mm, max_width_mm;
  uint16_t min_length_mm, max_length_mm;
  uint16_t base_raster_width, max_raster_width;   // dots in one print row
  bool     experimental;
  // Prints by heat on heat-sensitive paper, no ribbon: the label turns black
  // wherever it gets hot enough, a spool in the dryer included.
  bool     direct_thermal;
  const char* vendor;      // shown before the name where models are listed
  // The job goes to the print bridge over HTTP, not over BLE: no BLE device,
  // no Bluetooth switch, the bridge's address instead.
  bool     via_bridge;
  // A StringID shown after the name where models are listed, -1 for none:
  // which way the job goes, where two entries share a printer.
  int      note;
};

struct LabelPrinterConfig {
  LabelPrinterModel model;
  char     name[BLE_NAME_LEN];
  char     address[BLE_ADDR_LEN];
  uint16_t media_width_mm, media_length_mm;      // across the head, along the feed
  // Where the stock runs under the head, in dots from the middle of the print
  // row, positive to the right as the label reads. Stored as set and clamped
  // to the row when used, so LP_OFFSET_RIGHT keeps a roll against the right
  // wall there for every label width.
  int16_t  x_offset;
  // Only used by a via_bridge model. Kept apart from the BLE device, so
  // switching between the two ways keeps both.
  char     bridge_host[LP_BRIDGE_HOST_LEN];
  char     bridge_key[LP_BRIDGE_KEY_LEN];
};

// "Against the wall": beyond any head, clamped to the row's edge when used.
#define LP_OFFSET_RIGHT  LABEL_RASTER_MAX_W
#define LP_OFFSET_LEFT  (-LABEL_RASTER_MAX_W)

// The label sizes offered on the printer screen: width across the head by
// length along the feed, in mm. What the vendors sell for these printers.
struct LabelMediaSize { uint8_t width_mm, length_mm; };
extern const LabelMediaSize LABEL_MEDIA_SIZES[];
extern const int LABEL_MEDIA_SIZE_COUNT;

enum LabelPrintResult : uint8_t {
  LP_OK = 0,
  LP_NO_PRINTER,        // no device picked, or no model
  LP_BAD_RASTER,        // shape or padding wrong
  LP_TOO_WIDE,          // wider than the head
  LP_MEDIA_MISMATCH,    // not the loaded label size
  LP_BLE_OFF,
  LP_BLE_INIT,
  LP_BLE_CONNECT,
  LP_BLE_CHARACTERISTIC,
  LP_BLE_WRITE,
  LP_BLE_STUCK,
  LP_SENT_UNCONFIRMED,  // all sent, the printer never said done
  // The print bridge's answers (services/print_bridge.h).
  LP_BRIDGE_UNREACHABLE,  // no answer from its address
  LP_BRIDGE_NO_PRINTER,   // 412: no printer chosen on the bridge
  LP_BRIDGE_PRINTER,      // 502: the bridge could not reach the printer
  LP_BRIDGE_AUTH,         // 401: the API key is wrong or missing
  LP_BRIDGE_BUSY,         // 409: another job is running
  LP_BRIDGE_REFUSED,      // any other answer
};

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model);
// The models in the order the screens list them.
extern const LabelPrinterModel LABEL_PRINTER_MODELS[];
extern const int LABEL_PRINTER_MODEL_COUNT;
// True when this model prints through the bridge.
bool labelPrinterViaBridge(LabelPrinterModel model);
// "PM240 (WLAN-Brücke)": the name and its note, for a list or a row.
void labelPrinterModelLabel(LabelPrinterModel model, char* out, size_t n);

// Cached after the first read; every write goes through the save.
LabelPrinterConfig labelPrinterLoadConfig();
bool labelPrinterSaveConfig(const LabelPrinterConfig& config);
// A BLE device picked, or for a bridge model, the bridge's address set.
bool labelPrinterConfigured(const LabelPrinterConfig& config);
// Drops the device (or the bridge's address and key), keeps model and label
// size.
bool labelPrinterForget();
// True when this address is the picked printer.
bool labelPrinterIsDevice(const char* address);

uint16_t labelPrinterDotsForMm(uint16_t mm);
// The print row for this model and label width: the head's base width or
// the label, whichever is wider, rounded up to whole bytes, capped at the head.
uint16_t labelPrinterRasterWidth(LabelPrinterModel model, uint16_t media_width_mm);
// The first dot of the label in the print row: the middle plus the offset,
// clamped so the label stays in the row.
uint16_t labelPrinterContentX(const LabelPrinterConfig& config);
// The offsets the loaded stock allows: the label at the row's left edge and
// at its right edge. Both 0 when the label fills the row.
void labelPrinterOffsetRange(const LabelPrinterConfig& config, int16_t* min, int16_t* max);
// The offset in effect: the stored one, clamped to that range.
int16_t labelPrinterOffset(const LabelPrinterConfig& config);
bool labelPrinterRasterFits(LabelPrinterModel model, const LabelRaster& image,
                            uint16_t media_width_mm, uint16_t media_length_mm);

// Blocking, from appLoop() only: checks the raster against the printer and
// the loaded label, then prints. progress keeps an overlay moving.
LabelPrintResult labelPrinterPrint(const LabelPrinterConfig& config,
                                   const LabelRaster& image, BleProgressFn progress);
// The StringID that says what a result means, for a popup or a status line.
int labelPrintResultString(LabelPrintResult result);
