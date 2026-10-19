#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>

// ============================================================
//  FILAMENT_TO_BAMBUDDY
//
//  The companion web app that turns a box barcode into filament data: its
//  own cache of confirmed codes first, then the Open Filament Database and
//  SpoolmanDB-Community cross-referenced. The scale only asks it
//
//    GET  <f2b>/api/lookup?barcode=<code>
//    POST <f2b>/api/spool               the spool, created by the app
//    GET  <f2b>/api/label/<id>.bits     a spool's label in the app's layout
//
//  The app creates the spool the way its own page does: on the user's
//  Spoolman filament, with the slicer profile for every printer model, the
//  bed temperature and price on the filament, and the code remembered. When
//  the app cannot be reached the scale creates it itself through BamBuddy. A code
//  the app does not know comes back with source "none" or "amazon" and no
//  material: there is nothing to create from then, and the phone app, with
//  its form and label OCR, is the place for it.
// ============================================================

extern char g_f2b_url[96];      // NVS "f2b_url", e.g. http://zimaboard:8088

void f2bLoadSettings();
void f2bSetUrl(const char* url);

struct F2bProduct {
  bool found;              // the app knew the code and has a material
  char source[24];         // cache, spoolman, tigertag, ofd, spoolmandb-community, upcitemdb, none, amazon
  char barcode[24];        // the code as the app canonicalised it
  char brand[32];
  char material[16];
  char subtype[24];
  char color_name[32];
  char rgba[12];           // RRGGBBAA
  int  label_weight;       // grams of filament on a full spool
  int  core_weight;        // the empty spool
  int  temp_min;
  int  temp_max;
  float price_avg;         // mean shop price of one spool, 0 when none known
  char  price_cur[8];      // its currency, e.g. EUR; may be empty
  int   price_offers;      // offers averaged, 0 for the recorded price range
  int   spoolman_filament_id;  // the user's own Spoolman filament it matched, 0 for none
  bool  core_from_filament;    // core_weight is that filament's own (or its maker's)
  char  slicer_filament[16];       // the slicer profile BamBuddy has imported, "" for none
  char  slicer_filament_name[96];
  String fields_json;          // the app's fields as it sent them, handed back to create
};

// HTTP code; 200 with found false for a code nobody knows. -1 when no
// address is set. Half a minute: the app may be rebuilding a filament
// database, and the lookup is all the scale waits for.
int f2bLookup(const char* code, F2bProduct& out, uint32_t timeout_ms = 30000);

// Creates the spool in the app from what f2bLookup() found: the app's own
// fields, the shop price per kg when the user took it (0 leaves the app's),
// and the user's Spoolman filament the code was found on. HTTP code; 200 with
// the new spool's id. `why` gets the app's or BamBuddy's reason on failure.
int f2bCreateSpool(const F2bProduct& p, float cost_per_kg, int* out_spool_id,
                   char* why, size_t why_n, uint32_t timeout_ms = 30000);

// A spool's label in the app's own layout (the one arranged on the phone),
// for a roll w_mm by l_mm: one bit a dot, rows of (width + 7) / 8 bytes, a 1
// bit black. The bits are in PSRAM and belong to the caller (heap_caps_free).
// HTTP code; 200 with the bits.
int f2bFetchLabel(int spool_id, uint16_t w_mm, uint16_t l_mm, uint8_t** out_bits,
                  size_t* out_len, uint16_t* out_w, uint16_t* out_h, uint32_t timeout_ms = 15000);
