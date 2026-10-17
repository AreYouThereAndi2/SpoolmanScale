#include "services/spool_extras.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/app_state.h"
#include "services/prefs_store.h"

// NVS: "xtr_n<i>" and "xtr_g<i>" per place, "xtr_def" the default, and
// "xs<id>" a spool's own choice. 0xFF there means "the default": NVS here
// has no erase, and a spool back on the default must follow it when the
// default changes later.
#define XS_FOLLOW_DEFAULT 0xFF

static SpoolExtra s_list[SPOOL_EXTRAS_MAX];
static uint8_t    s_default = 0;
// The last spool asked about. The main screen asks on every loop pass, and
// each NVS read opens the namespace.
static int        s_cached_id   = -1;
static uint8_t    s_cached_mask = 0;

static void keyFor(char* out, size_t n, const char* prefix, int i) {
  snprintf(out, n, "%s%d", prefix, i);
}

static void trimInto(char* dst, size_t n, const char* src) {
  while (*src == ' ') src++;
  snprintf(dst, n, "%s", src);
  size_t len = strlen(dst);
  while (len && dst[len - 1] == ' ') dst[--len] = '\0';
}

static float clampGrams(float g) {
  if (!isfinite(g) || g < 0.0f) return 0.0f;
  return g > SPOOL_EXTRA_MAX_G ? SPOOL_EXTRA_MAX_G : g;
}

void spoolExtrasLoad() {
  char key[12];
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    keyFor(key, sizeof(key), "xtr_n", i);
    trimInto(s_list[i].name, sizeof(s_list[i].name), prefsGetString(key, "").c_str());
    keyFor(key, sizeof(key), "xtr_g", i);
    s_list[i].grams = clampGrams(prefsGetFloat(key, 0.0f));
  }
  s_default = spoolExtrasClamp(prefsGetUChar("xtr_def", 0));
}

const SpoolExtra& spoolExtraAt(int i) {
  static const SpoolExtra none{};
  return (i >= 0 && i < SPOOL_EXTRAS_MAX) ? s_list[i] : none;
}

bool spoolExtraUsed(int i) {
  return i >= 0 && i < SPOOL_EXTRAS_MAX && s_list[i].name[0] != '\0';
}

bool spoolExtrasAny() {
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++)
    if (spoolExtraUsed(i)) return true;
  return false;
}

uint8_t spoolExtrasClamp(uint8_t mask) {
  uint8_t out = 0;
  int picked = 0;
  for (int i = 0; i < SPOOL_EXTRAS_MAX && picked < SPOOL_EXTRAS_PICK; i++) {
    if ((mask & (1u << i)) && spoolExtraUsed(i)) {
      out |= (uint8_t)(1u << i);
      picked++;
    }
  }
  return out;
}

bool spoolExtrasSave(const SpoolExtra* list, uint8_t default_mask) {
  bool ok = true;
  char key[12];
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    trimInto(s_list[i].name, sizeof(s_list[i].name), list[i].name);
    s_list[i].grams = s_list[i].name[0] ? clampGrams(list[i].grams) : 0.0f;
    keyFor(key, sizeof(key), "xtr_n", i);
    ok &= prefsPutString(key, s_list[i].name);
    keyFor(key, sizeof(key), "xtr_g", i);
    ok &= prefsPutFloat(key, s_list[i].grams);
  }
  s_default = spoolExtrasClamp(default_mask);
  s_cached_id = -1;
  ok &= prefsPutUChar("xtr_def", s_default);
  return ok;
}

uint8_t spoolExtrasDefault() {
  return s_default;
}

uint8_t spoolExtrasFor(int spool_id) {
  if (spool_id <= 0) return s_default;
  if (spool_id == s_cached_id) return s_cached_mask;
  char key[16];
  keyFor(key, sizeof(key), "xs", spool_id);
  const uint8_t own = prefsGetUChar(key, XS_FOLLOW_DEFAULT);
  s_cached_id = spool_id;
  s_cached_mask = own == XS_FOLLOW_DEFAULT ? s_default : spoolExtrasClamp(own);
  return s_cached_mask;
}

bool spoolExtrasSetFor(int spool_id, uint8_t mask) {
  if (spool_id <= 0) return false;
  mask = spoolExtrasClamp(mask);
  s_cached_id = -1;
  char key[16];
  keyFor(key, sizeof(key), "xs", spool_id);
  // Nothing is written for a spool that never left the default.
  if (mask == s_default && !prefsHasKey(key)) return true;
  return prefsPutUChar(key, mask == s_default ? XS_FOLLOW_DEFAULT : mask);
}

float spoolExtrasGrams(uint8_t mask) {
  mask = spoolExtrasClamp(mask);
  float g = 0.0f;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++)
    if (mask & (1u << i)) g += s_list[i].grams;
  return g;
}

void spoolExtrasNames(uint8_t mask, char* out, size_t n) {
  if (!out || !n) return;
  out[0] = '\0';
  mask = spoolExtrasClamp(mask);
  size_t used = 0;
  for (int i = 0; i < SPOOL_EXTRAS_MAX && used < n; i++) {
    if (!(mask & (1u << i))) continue;
    used += snprintf(out + used, n - used, "%s%s", used ? " + " : "", s_list[i].name);
  }
}

float spoolExtrasNow() {
  return spoolExtrasGrams(spoolExtrasFor(sm_found ? sm_id : 0));
}

float spoolExtrasBareReading() {
  const float g = scale_weight_g - spoolExtrasNow();
  return g > 0.0f ? g : 0.0f;
}
