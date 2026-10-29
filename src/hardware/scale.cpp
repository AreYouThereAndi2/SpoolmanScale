#include "scale.h"

#include <Adafruit_NAU7802.h>

#include "app_config.h"
#include "hardware/pins.h"
#include "services/prefs_store.h"

enum ScaleAdc : uint8_t { ADC_NONE, ADC_NAU7802, ADC_HX711 };

static Adafruit_NAU7802 nau;
// Kept so the address can be probed without the caller handing the bus in
// again - see scaleHardwarePresent().
static TwoWire* nau_wire = nullptr;
static ScaleAdc s_adc = ADC_NONE;

// Give up on the internal calibration after roughly three seconds. This used
// to retry forever, which hung the whole boot on a defective load cell or a
// loose cable: no display, no WiFi, no web interface, nothing but a device that
// looks dead. Returning false instead lets app_boot.cpp carry on without the
// scale, so display, NFC and the filament manager still work and the red SCL!
// in the header says what is wrong. Do not turn this back into a bare while.
#define CAL_MAX_ATTEMPTS  30
#define CAL_RETRY_DELAY_MS 100

// ---- HX711 ---------------------------------------------------------------
// Data sheet: DOUT falls when a conversion is ready (10 or 80 per second, by
// the board's RATE pin). 24 clock pulses shift it out MSB first, a 25th
// selects channel A at gain 128 for the next one. A clock held high for more
// than 60 us powers the chip down, so the pulses must not be interrupted.
//
// Checked against bogde/HX711, the library most guides point to. The wire
// protocol is the same; this driver does not use it because it blocks until
// the chip is ready and hands back whatever came out, with no way to tell a
// sample from a glitch. Two things were taken over from it: DOUT is a plain
// input while the pulses run, and the clock is not interrupted.
#define HX_KEY             "hx711"
// Up to 400 ms settling after power-up, then one conversion at 10 per second.
// How long the search waits is a setting (HX_FIND_KEY), 1500 ms unless changed.
#define HX_FIND_KEY        "hx_find_ms"
#define ADC_CHOICE_KEY     "scale_adc"
// Without a finished conversion for this long the HX711 counts as gone.
#define HX_ALIVE_MS        1000
// High and low time of one clock pulse. The chip wants 0.2 us to 50 us.
#define HX_PULSE_US        5
// After the 25th pulse DOUT rises within a few us.
#define HX_RELEASE_US      50

static portMUX_TYPE s_hx_mux = portMUX_INITIALIZER_UNLOCKED;
static int8_t s_hx_fitted = -1;     // -1 not read from the preferences yet
static int8_t s_choice = -1;        // ScaleAdcChoice, -1 not read yet
static volatile uint32_t s_hx_ready_ms = 0;
// The last three plausible conversions; the median of them is what the scale
// sees. A single wrong bit is worth millions of counts, and a moving average
// downstream would carry it for seconds.
static int32_t  s_hx_hist[3];
static uint8_t  s_hx_have = 0;
static volatile int32_t  s_hx_last = 0;
static volatile int32_t  s_hx_newest = 0;    // the last conversion kept, unfiltered
static volatile uint32_t s_hx_seq = 0;      // conversions accepted so far
static uint32_t s_hx_seq_taken = 0;         // what scaleHardwareAvailable() has handed out
static volatile uint32_t s_hx_rejected = 0;

ScaleAdcChoice scaleAdcChoice() {
  if (s_choice < 0) {
    const int c = prefsGetInt(ADC_CHOICE_KEY, SCALE_ADC_AUTO);
    s_choice = (c == SCALE_ADC_HX711 || c == SCALE_ADC_NAU7802) ? c : SCALE_ADC_AUTO;
  }
  return (ScaleAdcChoice)s_choice;
}

ScaleAdcChoice scaleAdcChoiceSaved() {
  const int c = prefsGetInt(ADC_CHOICE_KEY, SCALE_ADC_AUTO);
  return (c == SCALE_ADC_HX711 || c == SCALE_ADC_NAU7802) ? (ScaleAdcChoice)c : SCALE_ADC_AUTO;
}

void scaleAdcSetChoice(ScaleAdcChoice c) {
  prefsPutInt(ADC_CHOICE_KEY, (int)c);
  // s_choice and s_hx_fitted stay as they are until the next start: the
  // reader's reset pin was picked from them at boot.
}

uint16_t scaleHxFindMs() {
  const uint32_t v = prefsGetUInt(HX_FIND_KEY, HX_FIND_MS_DEFAULT);
  if (v < HX_FIND_MS_MIN) return HX_FIND_MS_MIN;
  if (v > HX_FIND_MS_MAX) return HX_FIND_MS_MAX;
  return (uint16_t)v;
}

void scaleHxSetFindMs(uint16_t ms) {
  if (ms < HX_FIND_MS_MIN) ms = HX_FIND_MS_MIN;
  if (ms > HX_FIND_MS_MAX) ms = HX_FIND_MS_MAX;
  prefsPutUInt(HX_FIND_KEY, ms);
}

bool scaleHx711Fitted() {
  // A chip named in the settings is the answer, whatever an earlier boot found.
  if (scaleAdcChoice() == SCALE_ADC_HX711) return true;
  if (scaleAdcChoice() == SCALE_ADC_NAU7802) return false;
  if (s_hx_fitted < 0) s_hx_fitted = prefsGetBool(HX_KEY, false) ? 1 : 0;
  return s_hx_fitted == 1;
}

static void hxRemember(bool fitted) {
  // Only automatic detection remembers; a named chip is already the answer.
  if (scaleAdcChoice() != SCALE_ADC_AUTO) return;
  if (scaleHx711Fitted() == fitted) return;
  s_hx_fitted = fitted ? 1 : 0;
  prefsPutBool(HX_KEY, fitted);
}

static void hxPins() {
  pinMode(hw_pins::HX711_SCK, OUTPUT);
  // Low wakes the chip, and keeps it awake.
  digitalWrite(hw_pins::HX711_SCK, LOW);
  // Nothing on the line reads high: an absent HX711 never looks ready.
  pinMode(hw_pins::HX711_DOUT, INPUT_PULLUP);
}

static bool hxReady() {
  if (digitalRead(hw_pins::HX711_DOUT) != LOW) return false;
  s_hx_ready_ms = millis();
  return true;
}

enum HxPoll : uint8_t {
  HX_NONE,        // nothing ready
  HX_GOOD,        // a conversion, kept
  HX_SATURATED,   // the chip answered, but the input is open or overloaded
  HX_BAD          // the line does not behave like an HX711's
};

static int32_t hxMedian3(int32_t a, int32_t b, int32_t c) {
  if (a > b) { const int32_t t = a; a = b; b = t; }
  if (b > c) b = c;
  return a > b ? a : b;
}

// Takes one conversion if one is waiting. Looking, clocking and judging are
// one step, so no second caller (the main loop, the live page, the BamBuddy
// reporter) can clock out a chip that is already converting the next one.
static HxPoll hxPoll() {
  uint32_t v = 0;
  bool high_after = false;
  portENTER_CRITICAL(&s_hx_mux);
  if (digitalRead(hw_pins::HX711_DOUT) != LOW) {
    portEXIT_CRITICAL(&s_hx_mux);
    return HX_NONE;
  }
  // The pull-up is for telling an absent chip from a present one. While the
  // chip drives the line it only distorts it: bogde/HX711 notes that values
  // between 0x20000 and 0x30000 read wrong with DOUT pulled up.
  pinMode(hw_pins::HX711_DOUT, INPUT);
  for (int i = 0; i < 25; i++) {
    digitalWrite(hw_pins::HX711_SCK, HIGH);
    delayMicroseconds(HX_PULSE_US);
    if (i < 24) v = (v << 1) | (digitalRead(hw_pins::HX711_DOUT) ? 1 : 0);
    digitalWrite(hw_pins::HX711_SCK, LOW);
    delayMicroseconds(HX_PULSE_US);
  }
  delayMicroseconds(HX_RELEASE_US);
  high_after = digitalRead(hw_pins::HX711_DOUT) == HIGH;
  pinMode(hw_pins::HX711_DOUT, INPUT_PULLUP);
  portEXIT_CRITICAL(&s_hx_mux);

  s_hx_ready_ms = millis();
  // A real HX711 raises DOUT as soon as the 25th pulse ends. A line that is
  // merely held low (an old build's reset wire on EXT_IO5) shifts out zeros
  // and stays low; one that is high all the time (chip asleep, wire loose)
  // shifts out ones.
  if (!high_after || v == 0 || v == 0xFFFFFF) {
    s_hx_rejected++;
    return HX_BAD;
  }
  // Full scale either way: the bridge is open, shorted or overloaded.
  if (v == 0x7FFFFF || v == 0x800000) {
    s_hx_rejected++;
    return HX_SATURATED;
  }
  const int32_t raw = (v & 0x800000) ? (int32_t)(v | 0xFF000000u) : (int32_t)v;

  if (s_hx_have < 3) {
    s_hx_hist[s_hx_have++] = raw;
  } else {
    s_hx_hist[0] = s_hx_hist[1];
    s_hx_hist[1] = s_hx_hist[2];
    s_hx_hist[2] = raw;
  }
  s_hx_newest = raw;
  s_hx_last = (s_hx_have < 3) ? raw
                              : hxMedian3(s_hx_hist[0], s_hx_hist[1], s_hx_hist[2]);
  s_hx_seq++;
  return HX_GOOD;
}

// Two conversions that look like an HX711's. A chip with an open input still
// counts: it is there, and the live page shows what is wrong with it.
static bool hxFind(void (*wait_cb)()) {
  hxPins();
  s_hx_have = 0;
  int seen = 0;
  const uint32_t t0 = millis();
  const uint32_t find_ms = scaleHxFindMs();
  while (seen < 2 && millis() - t0 < find_ms) {
    const HxPoll r = hxPoll();
    if (r == HX_GOOD || r == HX_SATURATED) {
      seen++;
      continue;
    }
    // Nothing ready yet, or a glitch while the chip is still settling.
    if (wait_cb) wait_cb(); else delay(10);
  }
  return seen == 2;
}

// ---- both ----------------------------------------------------------------

static bool nauPresent() {
  if (!nau_wire) return false;
  nau_wire->beginTransmission(I2C_ADDR_NAU7802);
  return nau_wire->endTransmission() == 0;
}

static bool nauBegin(void (*calibration_wait_cb)()) {
  if (!nau.begin(nau_wire)) {
    Serial.println("NAU7802 answered but would not start");
    return false;
  }
  nau.setLDO(NAU7802_3V0);
  nau.setGain(NAU7802_GAIN_128);
  nau.setRate(NAU7802_RATE_10SPS);

  delay(300);
  for (int attempt = 0; attempt < CAL_MAX_ATTEMPTS; attempt++) {
    if (nau.calibrate(NAU7802_CALMOD_INTERNAL)) return true;
    if (calibration_wait_cb) {
      calibration_wait_cb();
    } else {
      Serial.println("Calibrate retry...");
      delay(CAL_RETRY_DELAY_MS);
    }
  }
  Serial.println("NAU7802 calibration timed out");
  return false;
}

bool scaleHardwareBegin(TwoWire* wire, void (*calibration_wait_cb)()) {
  nau_wire = wire;
  s_adc = ADC_NONE;

  // Asked separately so the two failure modes can be told apart. begin() also
  // returns false for a chip that answers and then refuses to start, and the
  // caller prints a different line for each.
  const ScaleAdcChoice choice = scaleAdcChoice();
  if (choice != SCALE_ADC_HX711 && nauPresent()) {
    if (!nauBegin(calibration_wait_cb)) return false;
    s_adc = ADC_NAU7802;
    hxRemember(false);
    return true;
  }
  if (choice != SCALE_ADC_NAU7802 && hxFind(calibration_wait_cb)) {
    s_adc = ADC_HX711;
    s_hx_ready_ms = millis();
    hxRemember(true);
    return true;
  }
  Serial.printf("No scale chip found (setting: %s; NAU7802 on 0x%02X, HX711 on GPIO%d/%d)\n",
                choice == SCALE_ADC_HX711 ? "HX711" : choice == SCALE_ADC_NAU7802 ? "NAU7802" : "auto",
                I2C_ADDR_NAU7802, hw_pins::HX711_SCK, hw_pins::HX711_DOUT);
  return false;
}

bool scaleHardwarePresent() {
  if (s_adc == ADC_HX711 || (s_adc == ADC_NONE && scaleHx711Fitted())) {
    if (s_adc == ADC_NONE) hxPins();
    // A stalled loop is not a silent chip. Nobody looked at the line while a
    // slow HTTP call held the loop (the field log shows "HX711 stopped
    // answering" one second after such a call), so after a gap the clock
    // starts again here instead of counting the stall against the chip.
    static uint32_t last_call_ms = 0;
    const uint32_t now = millis();
    if (now - last_call_ms > HX_ALIVE_MS) s_hx_ready_ms = now;
    last_call_ms = now;
    return hxReady() || now - s_hx_ready_ms < HX_ALIVE_MS;
  }
  return nauPresent();
}

// True once the ADC has finished a new conversion. Reading without checking
// returns the previous sample again, which then enters the moving average a
// second time and weights it twice.
//
// The presence probe comes first. Without it this answers true for a chip that
// is not on the bus at all, because the failed register read is all ones and
// the conversion ready bit is one of them.
bool scaleHardwareAvailable() {
  if (s_adc == ADC_HX711) {
    // Something else may have raised the clock (the PN532 library pulses its
    // reset pin, which was this one before the HX711 was known).
    digitalWrite(hw_pins::HX711_SCK, LOW);
    hxPoll();
    // One answer per conversion, whoever clocked it out.
    if (s_hx_seq == s_hx_seq_taken) return false;
    s_hx_seq_taken = s_hx_seq;
    return true;
  }
  return nauPresent() && nau.available();
}

// The NAU7802 hands back its last conversion whenever asked. The HX711 does
// not: clocked while it converts, it shifts out half a sample and starts the
// next one late. So a new sample is taken only when one is ready, and the
// last one is answered otherwise, as the NAU7802 would.
int32_t scaleHardwareReadRaw() {
  if (s_adc == ADC_HX711) {
    hxPoll();
    return s_hx_last;
  }
  return nau.read();
}

uint32_t scaleRejectedSamples() { return s_hx_rejected; }

bool scaleHardwareSampleRaw(uint32_t window_ms, int32_t* mean,
                            int32_t* spread, int* count) {
  int64_t sum = 0;
  int32_t lo = INT32_MAX, hi = INT32_MIN;
  int n = 0;
  uint32_t seq = s_hx_seq;
  const uint32_t t0 = millis();
  while (millis() - t0 < window_ms) {
    int32_t v = 0;
    bool fresh = false;
    if (s_adc == ADC_HX711) {
      hxPoll();
      if (s_hx_seq != seq) {
        seq = s_hx_seq;
        v = s_hx_newest;     // the conversion itself, not the median of three
        fresh = true;
      }
    } else if (s_adc == ADC_NAU7802 && nauPresent() && nau.available()) {
      v = nau.read();
      fresh = true;
    }
    if (fresh) {
      sum += v;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      n++;
    }
    delay(2);
  }
  if (count) *count = n;
  if (n < 3) return false;
  if (mean) *mean = (int32_t)(sum / n);
  if (spread) *spread = hi - lo;
  return true;
}

int32_t scaleHardwareTareReading() {
  int32_t mean = 0, spread = 0;
  int n = 0;
  if (scaleHardwareSampleRaw(1000, &mean, &spread, &n)) return mean;
  return scaleHardwareReadRaw();
}

const char* scaleHardwareName() {
  switch (s_adc) {
    case ADC_NAU7802: return "NAU7802";
    case ADC_HX711:   return "HX711";
    default:          return "-";
  }
}
