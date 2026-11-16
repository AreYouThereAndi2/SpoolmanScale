#pragma once

// HTTP on port 80: the page at "/", the JSON API under /api/ and the firmware
// upload at /update. Served from the loop task; a print job answers when its
// bytes are out, so the loop waits that long.
void webBegin();
void webLoop();
