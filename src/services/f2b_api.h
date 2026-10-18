#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  FILAMENT_TO_BAMBUDDY
//
//  The companion web app that turns a box barcode into filament data: its
//  own cache of confirmed codes first, then the Open Filament Database and
//  SpoolmanDB-Community cross-referenced. The scale only asks it
//
//    GET <f2b>/api/lookup?barcode=<code>
//
//  and creates the spool itself through the backend, which knows whether
//  BamBuddy keeps its own inventory or a Spoolman server behind it. A code
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
};

// HTTP code; 200 with found false for a code nobody knows. -1 when no
// address is set.
int f2bLookup(const char* code, F2bProduct& out, uint32_t timeout_ms = 15000);
