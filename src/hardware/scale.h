#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

// The load cell's converter: a NAU7802 on the I2C bus, or an HX711 on two
// pins of the I/O connector (hw_pins::HX711_SCK, HX711_DOUT). The NAU7802 is
// asked first; without it the HX711 is looked for, and once found it is
// remembered, so the next boot keeps its pins away from the NFC reader.
bool scaleHardwareBegin(TwoWire* wire, void (*calibration_wait_cb)() = nullptr);

// Does the ADC still answer? Ask before trusting a reading: Adafruit_BusIO
// answers a failed transfer with -1, so every register of an absent NAU7802
// reads back as all ones. available() sees the conversion ready bit set
// forever and read() returns a sample of -1, and neither can tell the caller
// that the chip is gone. An HX711 answers by finishing conversions.
bool scaleHardwarePresent();

bool scaleHardwareAvailable();
int32_t scaleHardwareReadRaw();
// HX711 conversions thrown away since boot: a line stuck high or low, a clock
// that did not finish, or an input at full scale (open bridge, overload).
// Zero on a healthy build, and always zero with a NAU7802.
uint32_t scaleRejectedSamples();

// Fresh conversions for window_ms (the HX711 delivers ten a second), as their
// mean and peak-to-peak spread. For tare and calibration, which used to take
// the one sample that happened to be there when the button was pressed: on a
// wandering signal that decides between a good factor and a refused one.
// Blocks for the window. False when fewer than three conversions arrived.
bool scaleHardwareSampleRaw(uint32_t window_ms, int32_t* mean,
                            int32_t* spread, int* count);

// What a tare button stores as the zero point: the mean of one second of
// conversions, or the single current sample when too few arrive.
int32_t scaleHardwareTareReading();

// An HX711 was found on this device, now or on an earlier boot. Its pins are
// then the scale's: the PN532 gets no reset line and the reset probe is off.
bool scaleHx711Fitted();
// Which chip the scale looks for. Automatic is the normal case: the NAU7802
// is probed on the bus, then the HX711 on its two pins. The other two exist for
// a build where that fails - an HX711 that starts slowly, say - and skip the
// probe of the other chip entirely. Read at start; a change applies after the
// next restart, because the reader's reset pin depends on it.
enum ScaleAdcChoice : uint8_t { SCALE_ADC_AUTO = 0, SCALE_ADC_HX711 = 1, SCALE_ADC_NAU7802 = 2 };
ScaleAdcChoice scaleAdcChoice();        // the one in force since the last start
ScaleAdcChoice scaleAdcChoiceSaved();   // what the settings say now
void scaleAdcSetChoice(ScaleAdcChoice c);
// How long the start-up search waits for the HX711's first conversions.
#define HX_FIND_MS_DEFAULT 1500
#define HX_FIND_MS_MIN      500
#define HX_FIND_MS_MAX     5000
uint16_t scaleHxFindMs();
void scaleHxSetFindMs(uint16_t ms);
// "NAU7802", "HX711" or "-" for logs and the status page.
const char* scaleHardwareName();
