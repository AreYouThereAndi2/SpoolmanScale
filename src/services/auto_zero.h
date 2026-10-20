#pragma once

#include <stdint.h>

// Zero tracking: a bare pad that drifts a few grams away from zero over the
// day - temperature, the creep of a load cell under its own platform - is
// pulled back to 0 g without anyone pressing TARE.
//
// Only a pad that is empty by every measure is touched: within a couple of
// grams of zero, steady, no tag on the reader, for long enough that nothing
// is being put down or picked up. Anything heavier than the band is a load,
// and a load is never tared away.
//
// On by default. The offset is stored at most every few minutes, so tracking
// does not wear the flash it lives in.
extern bool g_auto_zero;

void autoZeroLoad();
void autoZeroSet(bool on);

// Once per new reading from the scale loop, after the moving average.
// `weight_g` is the average, `spread_g` its peak to peak, `full` whether the
// window has filled since the last tare.
void autoZeroNote(float weight_g, float spread_g, bool full, bool tag_on_reader);
