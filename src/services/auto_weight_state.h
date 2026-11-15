#pragma once

extern bool g_auto_weight;
extern bool g_auto_loc_popup;
extern float auto_weight_last_val;
extern unsigned long auto_weight_stable_ms;

#define AUTO_WEIGHT_STABLE_MS 3000
#define AUTO_WEIGHT_THRESH_G 0.5f

// Below this much nothing is on the pad, whatever the spool: a tag held to
// the reader alone reads a few grams of drift at most (upstream ee77f0e).
#define AUTO_WEIGHT_EMPTY_PAD_G 20.0f

// True while the pad holds too little for any spool to be on it: less than
// AUTO_WEIGHT_EMPTY_PAD_G. Nothing is written while this holds. The spool's
// stored empty weight is not used: it is often a default rather than measured
// (BamBuddy keeps 250 g for every spool without its own), and a lighter
// cardboard spool with little filament left would read as missing.
bool autoWeightPadTooLight(float grams);
