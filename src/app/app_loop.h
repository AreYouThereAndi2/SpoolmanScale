#pragma once

void appLoop();

// Loads a spool that has no tag on the reader and takes it on for weighing,
// automatic weighing included, until the pad is empty again or a tag turns
// up. Used by a FilaMan remote link that found no tag, and by a scanned
// spool label. Blocks for the spool request; never from an LVGL handler.
// False when the spool could not be loaded; nothing is taken on then.
bool appAdoptSpoolForWeighing(int spool_id);

// With automatic weighing on, lets the spool on the pad be weighed once more
// although it was weighed already: a new empty weight makes the weighing
// before it stale, computed against the old one or refused for want of one
// (Spoolman answers 409, upstream b67c31a). `why` goes into the log. Does
// nothing with the automatic off or before the spool was weighed.
void appAutoWeighAgain(const char* why);
