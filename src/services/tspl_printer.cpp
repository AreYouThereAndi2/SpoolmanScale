#include "services/tspl_printer.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>

#include "hardware/sd_logger.h"

// What the vendor app sent for the 50 x 30 mm roll of the capture: 6 mm
// between labels, heat 10 of 15, no mirroring.
#define TSPL_GAP_MM   6
#define TSPL_DENSITY  10

// TSPL ends a job when the last line is in, but the printer prints from its
// buffer and a link closed right after the data cost the M220 its label. No
// status characteristic is known for the PM240, so the session stays for a
// while instead: a 30 mm label is through in about a second.
static const BleDone TSPL_HOLD = { 0, nullptr, 0, 3000 };

BleWriteResult tsplPrint(const char* address, const LabelRaster& image,
                         uint16_t media_w_mm, uint16_t media_l_mm, BleProgressFn progress) {
  if (!labelRasterValid(image)) return BLE_WRITE_FAILED;
  // The head's part of each row: all of it when the row fits, else the middle.
  const uint16_t width = image.width > TSPL_HEAD_DOTS ? TSPL_HEAD_DOTS : image.width;
  const uint16_t row = width / 8;
  const uint16_t skip = (image.row_bytes - row) / 2;
  const size_t len = size_t(row) * image.height;
  uint8_t* bits = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!bits) bits = (uint8_t*)malloc(len);
  if (!bits) return BLE_WRITE_INIT_FAILED;
  // TSPL's BITMAP reads a 0 bit as a black dot, the raster a 1.
  for (uint16_t y = 0; y < image.height; ++y) {
    const uint8_t* src = image.pixels + size_t(y) * image.row_bytes + skip;
    uint8_t* dst = bits + size_t(y) * row;
    for (uint16_t x = 0; x < row; ++x) dst[x] = uint8_t(~src[x]);
  }

  char head[160];
  const int n = snprintf(head, sizeof(head),
                         "SIZE %u.0 mm,%u.0 mm\r\nGAP %u.0 mm,0 mm\r\nDIRECTION 0,0\r\n"
                         "DENSITY %u\r\nCLS\r\nBITMAP 0,0,%u,%u,1,",
                         (unsigned)media_w_mm, (unsigned)media_l_mm, (unsigned)TSPL_GAP_MM,
                         (unsigned)TSPL_DENSITY, (unsigned)row, (unsigned)image.height);
  static const char TAIL[] = "\r\nPRINT 1\r\n";
  logSDf("TSPL: %ux%u mm, %u bytes x %u rows (raster %u wide) to %s",
         (unsigned)media_w_mm, (unsigned)media_l_mm, (unsigned)row, (unsigned)image.height,
         (unsigned)image.width, address);

  const BleBlock blocks[] = {
    { (const uint8_t*)head, size_t(n) },
    { bits, len },
    { (const uint8_t*)TAIL, sizeof(TAIL) - 1 },
  };
  const BleWriteResult r = bleWriteBlocks(address, TSPL_SERVICE_UUID, TSPL_WRITE_UUID, blocks,
                                          sizeof(blocks) / sizeof(blocks[0]), progress, &TSPL_HOLD);
  free(bits);
  return r;
}
