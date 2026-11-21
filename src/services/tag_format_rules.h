#pragma once

#include <stddef.h>

// The format a tag is linked with in Spoolman's own tag relation. Spoolman
// takes any name and only shows it, but a MIFARE card filed as "ntag" read as
// an NTAG in its list. Names from Spoolman's KNOWN_FORMATS where one fits
// ("bambu", "ntag"); "mifare" for any other 4 byte card. This fork reads no
// Creality tags, so upstream's "creality" case is left out.

// Hex digits of a normalised id: a Bambu tray uuid and a 4 byte UID.
constexpr size_t TAG_HEX_LEN_TRAY_UUID = 32;
constexpr size_t TAG_HEX_LEN_4_BYTE    = 8;

// hex_len: length of the normalised id the link is made with.
inline const char* tagFormatFor(size_t hex_len) {
  if (hex_len == TAG_HEX_LEN_TRAY_UUID) return "bambu";
  if (hex_len == TAG_HEX_LEN_4_BYTE) return "mifare";
  return "ntag";
}
