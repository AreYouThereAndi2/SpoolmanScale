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
// "NAU7802", "HX711" or "-" for logs and the status page.
const char* scaleHardwareName();
