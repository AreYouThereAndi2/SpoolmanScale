#pragma once

#include <stdint.h>

// ============================================================
//  HOW FAR A WRITE MAY GO ON A TYPE 2 TAG
//
//  Pure arithmetic, no reader, so a host test can hold it
//  (test/test_ntag_pages.py).
//
//  Right behind a tag's user memory sit its lock and configuration pages.
//  On an NTAG213 page 40 holds the dynamic lock bits, 41 AUTH0, 42 ACCESS
//  and 43 the password. A smaller chip has them earlier: NTAG210 and MIFARE
//  Ultralight EV1 (MF0UL11, 48 bytes) keep AUTH0 on page 16, an MF0UL21 its
//  lock bits on page 36. A write past the user memory lands in them: zeros on
//  AUTH0 put every page behind a password, a lock bit once set stays set. A
//  tag the scale writes has to stay a tag anybody can write again, so no
//  write goes past what the tag says it holds.
// ============================================================

// The page every record starts on, once the four the chip keeps for itself
// (UID, lock bytes, capability container) are past.
#define NTAG_FIRST_USER_PAGE    4
// The last user page an NTAG2xx can have: the library refuses pages from 231
// on, and the largest tag this firmware writes, an NTAG216, ends at 225.
#define NTAG_PAGE_MAX         230
// A tag whose capability container names no size gets the user memory every
// Type 2 tag has: pages 4 to 15, the 48 bytes of a MIFARE Ultralight, an
// Ultralight EV1 or an NTAG210. It used to be NTAG213's page 39, which on
// those chips runs over AUTH0 and the password (and an Ultralight EV1 leaves
// the factory with a blank container). A record larger than 48 bytes is
// refused on such a tag with "format it as NDEF once" instead.
#define NTAG_FALLBACK_LAST_PG  15

// The user memory the capability container (page 3) names: E1, the version,
// the size in 8 byte blocks, the access byte. 0 when it names none. What it
// reports is the NDEF area, a little smaller than the chip: NTAG213 144 of
// 144, NTAG215 496 of 504, NTAG216 872 of 888.
inline uint16_t ntagUserBytesFromCc(const uint8_t cc[4]) {
  return (cc[0] == 0xE1 && cc[2]) ? (uint16_t)(cc[2] * 8) : 0;
}

// The last page a write may touch, from what ntagUserBytesFromCc() found.
// In 16 bits: byte 2 of the container is whatever the tag says, and from 0x7F
// up (an NTAG I2C plus 2k reports 0xEA) an 8 bit sum wrapped to page 1.
inline uint8_t ntagLastUserPage(uint16_t user_bytes) {
  if (!user_bytes) return NTAG_FALLBACK_LAST_PG;
  uint16_t last = NTAG_FIRST_USER_PAGE + user_bytes / 4 - 1;
  if (last > NTAG_PAGE_MAX) last = NTAG_PAGE_MAX;
  return (uint8_t)last;
}
