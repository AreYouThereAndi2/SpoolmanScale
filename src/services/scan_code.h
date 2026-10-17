#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  WHAT A SCANNED CODE MEANS
//
//  Pure text work, no network and no LVGL, so the rules can be read in one
//  place and checked on a desk.
//
//  Spool, by id:
//    web+spoolman:s-42        the QR code on a label Spoolman prints
//    http://host/spool/show/42  Spoolman's spool page, the scale's own label
//    http://host/spools/42    FilaMan's and BamBuddy's spool page
//    spool:42                 a hand made label
//    42                       one to six digits on their own
//  Filament, by id:
//    web+spoolman:f-7         Spoolman's filament label
//  Article number:
//    an EAN-8, UPC-A, EAN-13 or GTIN-14 with a valid check digit, which is
//    what the box of a spool carries. Matched against filament.article_number.
// ============================================================

enum ScanKind : uint8_t {
  SCAN_UNKNOWN = 0,
  SCAN_SPOOL_ID,
  SCAN_FILAMENT_ID,
  SCAN_ARTICLE
};

struct ScanCode {
  ScanKind kind;
  int      id;              // spool or filament id
  char     article[16];     // digits of the barcode, SCAN_ARTICLE only
};

ScanCode scanClassify(const char* code);

// Whether a string of digits ends in a valid GS1 check digit. Exposed for
// the check on the bench; scanClassify() uses it on 8, 12, 13 and 14 digits.
bool scanGtinValid(const char* digits);
