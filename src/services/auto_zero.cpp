#include "services/auto_zero.h"

#include <Arduino.h>
#include <math.h>

#include "app/app_state.h"
#include "app_config.h"
#include "hardware/scale_state.h"
#include "hardware/sd_logger.h"
#include "services/prefs_store.h"

#define AZ_KEY          "auto_zero"
// What still counts as an empty pad, and how still it has to be. A spool,
// even an empty one, weighs well over a hundred grams.
#define AZ_BAND_G       2.0f
#define AZ_SPREAD_G     1.0f
#define AZ_HOLD_MS      10000UL
// Stored no more often than this. In between the offset lives in RAM, which
// is all the next reading needs.
#define AZ_SAVE_EVERY_MS (10UL * 60UL * 1000UL)

bool g_auto_zero = true;

static uint32_t s_still_since = 0;
static uint32_t s_saved_ms = 0;
static int32_t  s_saved_offset = 0;
static bool     s_unsaved = false;

void autoZeroLoad() {
  g_auto_zero = prefsGetBool(AZ_KEY, true);
  s_saved_offset = zero_offset;
}

void autoZeroSet(bool on) {
  g_auto_zero = on;
  prefsPutBool(AZ_KEY, on);
  s_still_since = 0;
}

void autoZeroNote(float weight_g, float spread_g, bool full, bool tag_on_reader) {
  const uint32_t now = millis();
  // A pending correction reaches the flash even after tracking was switched
  // off or the pad got loaded: it is the zero the scale is using.
  if (s_unsaved && now - s_saved_ms >= AZ_SAVE_EVERY_MS) {
    saveTareOffset(zero_offset);
    s_saved_ms = now;
    s_saved_offset = zero_offset;
    s_unsaved = false;
  }

  const bool empty = g_auto_zero && full && !tag_on_reader &&
                     cal_factor != CAL_FACTOR_DEFAULT &&
                     fabsf(weight_g) <= AZ_BAND_G && spread_g <= AZ_SPREAD_G;
  if (!empty) {
    s_still_since = 0;
    return;
  }
  if (s_still_since == 0) {
    s_still_since = now;
    return;
  }
  if (now - s_still_since < AZ_HOLD_MS) return;
  s_still_since = now;

  // Below a tenth of a gram there is nothing to see and nothing to store.
  if (fabsf(weight_g) < 0.1f) return;

  zero_offset += (int32_t)lroundf(weight_g * cal_factor);
  // The window holds readings taken against the old zero; moved with it, the
  // display does not jump and the window stays full.
  for (int i = 0; i < SCALE_FILTER_SIZE; i++) scale_filter_buf[i] -= weight_g;
  scale_weight_g -= weight_g;
  if (zero_offset != s_saved_offset) s_unsaved = true;
  logSDf("Auto zero: %.1f g drift taken out", weight_g);
}
