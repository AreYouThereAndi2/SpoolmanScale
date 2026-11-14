#pragma once

// Which print temperatures a tag the scale writes gets. Pure logic: no
// Arduino, no JSON, so a host test can run it (test/test_spool_temp.py).
//
// Taken from upstream 89a33ed, the tag half only: people enter the
// temperature they print at in the backend, so its value goes on the tag
// before the material family's textbook range. The display half of that
// commit (and its later rework, ef24114 and 690747b) is not in this fork.

// Above any filament's nozzle or bed: a larger value is a typo or a unit mix-up.
#define SPOOL_TEMP_MAX_C  500

// A temperature range in °C. hi is 0 when nothing is known, lo is 0 when only
// one value is known.
struct SpoolTempRange {
  int lo;
  int hi;
};

// A range from two raw values with nonsense dropped. An upper end outside
// 1..499 leaves the range empty; a lower end counts only below the upper one,
// so 240/240 is the single value 240.
inline SpoolTempRange spoolTempRange(int lo, int hi) {
  SpoolTempRange r = {0, 0};
  if (hi <= 0 || hi >= SPOOL_TEMP_MAX_C) return r;
  r.hi = hi;
  if (lo > 0 && lo < hi) r.lo = lo;
  return r;
}

// The two ends a tag gets: the backend's value where there is one, the
// material family's range otherwise. A single value is written as both ends,
// a tag format has no field for "one temperature".
inline SpoolTempRange spoolTempTagEnds(SpoolTempRange backend, SpoolTempRange family) {
  const SpoolTempRange r = backend.hi ? backend : family;
  const SpoolTempRange ends = {r.lo ? r.lo : r.hi, r.hi};
  return ends;
}
