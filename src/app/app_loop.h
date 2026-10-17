#pragma once

void appLoop();

// Loads a spool that has no tag on the reader and takes it on for weighing,
// automatic weighing included, until the pad is empty again or a tag turns
// up. Used by a FilaMan remote link that found no tag, and by a scanned
// spool label. Blocks for the spool request; never from an LVGL handler.
// False when the spool could not be loaded; nothing is taken on then.
bool appAdoptSpoolForWeighing(int spool_id);
