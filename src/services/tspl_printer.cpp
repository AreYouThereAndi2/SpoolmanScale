#include "services/tspl_printer.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sd_logger.h"

// What the vendor app sent for the 50 x 30 mm roll of the capture: 6 mm
// between labels, heat 10 of 15, no mirroring.
#define TSPL_GAP_MM   6
#define TSPL_DENSITY  10

// The BLE session subscribes to ff03 and stays a few seconds after the last
// chunk. The ff03 subscription was the guess of 15.11.2026 for why the PM240
// printed nothing over BLE; the snoop of 16.11.2026 showed the real reason
// (it prints over SPP only), so on the PM240 this changes nothing. It stays
// for the Phomemo PM241/PM245 class: their driver in the original project
// subscribes the same way, and a printer drops a job whose link closes
// before it has taken it all. What the notes mean is not known, so none
// counts as "done" (a pattern that never comes); the session logs them.
#define TSPL_STATUS_UUID 0xff03
static const uint8_t TSPL_NEVER[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
static const BleDone TSPL_HOLD = { TSPL_STATUS_UUID, TSPL_NEVER, sizeof(TSPL_NEVER), 6000 };

static const char TSPL_TAIL[] = "\r\nPRINT 1\r\n";

bool tsplBuildJob(const LabelRaster& image, uint16_t media_w_mm, uint16_t media_l_mm,
                  TsplJob* out) {
  if (!out) return false;
  out->data = nullptr;
  out->len = 0;
  if (!labelRasterValid(image)) return false;
  // The head's part of each row: all of it when the row fits, else the middle.
  const uint16_t width = image.width > TSPL_HEAD_DOTS ? TSPL_HEAD_DOTS : image.width;
  const uint16_t row = width / 8;
  const uint16_t skip = (image.row_bytes - row) / 2;
  const size_t bits = size_t(row) * image.height;

  char head[160];
  const int n = snprintf(head, sizeof(head),
                         "SIZE %u.0 mm,%u.0 mm\r\nGAP %u.0 mm,0 mm\r\nDIRECTION 0,0\r\n"
                         "DENSITY %u\r\nCLS\r\nBITMAP 0,0,%u,%u,0,",
                         (unsigned)media_w_mm, (unsigned)media_l_mm, (unsigned)TSPL_GAP_MM,
                         (unsigned)TSPL_DENSITY, (unsigned)row, (unsigned)image.height);
  if (n <= 0 || n >= (int)sizeof(head)) return false;
  const size_t tail = sizeof(TSPL_TAIL) - 1;
  const size_t len = size_t(n) + bits + tail;
  // PSRAM first: the internal heap is what the TLS handshakes and the BLE
  // stack live on, and 12 kB in one piece is a lot of it.
  uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (uint8_t*)malloc(len);
  if (!buf) return false;

  memcpy(buf, head, size_t(n));
  // TSPL's BITMAP reads a 0 bit as a black dot, the raster a 1.
  uint8_t* dst = buf + n;
  for (uint16_t y = 0; y < image.height; ++y) {
    const uint8_t* src = image.pixels + size_t(y) * image.row_bytes + skip;
    for (uint16_t x = 0; x < row; ++x) *dst++ = uint8_t(~src[x]);
  }
  memcpy(dst, TSPL_TAIL, tail);

  out->data = buf;
  out->len = len;
  logSDf("TSPL: %ux%u mm, %u bytes x %u rows (raster %u wide), job %u bytes",
         (unsigned)media_w_mm, (unsigned)media_l_mm, (unsigned)row, (unsigned)image.height,
         (unsigned)image.width, (unsigned)len);
  return true;
}

void tsplFreeJob(TsplJob* job) {
  if (!job) return;
  free(job->data);   // heap_caps_malloc memory goes back through free() too
  job->data = nullptr;
  job->len = 0;
}

BleWriteResult tsplPrint(const char* address, const LabelRaster& image,
                         uint16_t media_w_mm, uint16_t media_l_mm, BleProgressFn progress) {
  if (!labelRasterValid(image)) return BLE_WRITE_FAILED;
  TsplJob job{};
  if (!tsplBuildJob(image, media_w_mm, media_l_mm, &job)) return BLE_WRITE_INIT_FAILED;
  logSDf("TSPL: over BLE to %s", address);
  const BleBlock blocks[] = { { job.data, job.len } };
  const BleWriteResult r = bleWriteBlocks(address, TSPL_SERVICE_UUID, TSPL_WRITE_UUID, blocks,
                                          1, progress, &TSPL_HOLD);
  tsplFreeJob(&job);
  // No done signal is known: "sent" is the whole truth here.
  return r == BLE_WRITE_SENT_UNCONFIRMED ? BLE_WRITE_OK : r;
}
