#include "scan_code.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Case insensitive "does s start with prefix".
static bool startsWithNoCase(const char* s, const char* prefix) {
  for (; *prefix; s++, prefix++) {
    if (!*s || tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
  }
  return true;
}

// A positive id of at most six digits at p, followed by nothing, a slash, a
// query or a fragment. 0 for anything else.
static int parseIdAt(const char* p) {
  int n = 0, digits = 0;
  while (isdigit((unsigned char)*p)) {
    if (++digits > 6) return 0;
    n = n * 10 + (*p - '0');
    p++;
  }
  if (digits == 0) return 0;
  if (*p && *p != '/' && *p != '?' && *p != '#') return 0;
  return n;
}

static bool allDigits(const char* s, size_t* out_len) {
  size_t n = 0;
  for (; s[n]; n++) {
    if (!isdigit((unsigned char)s[n])) return false;
  }
  *out_len = n;
  return n > 0;
}

bool scanGtinValid(const char* digits) {
  size_t n = 0;
  if (!digits || !allDigits(digits, &n) || n < 8) return false;
  // GS1: from the right, excluding the check digit, weights 3,1,3,1...
  int sum = 0;
  for (size_t i = 0; i + 1 < n; i++) {
    const int d = digits[n - 2 - i] - '0';
    sum += (i % 2 == 0) ? d * 3 : d;
  }
  const int check = (10 - (sum % 10)) % 10;
  return check == digits[n - 1] - '0';
}

// The last path segment after a marker such as "/spool/show/", if the URL
// has one. Searched case insensitively, anywhere in the string.
static int idAfterMarker(const char* code, const char* marker) {
  const size_t ml = strlen(marker);
  for (const char* p = code; *p; p++) {
    if (startsWithNoCase(p, marker)) return parseIdAt(p + ml);
  }
  return 0;
}

ScanCode scanClassify(const char* code) {
  ScanCode r;
  r.kind = SCAN_UNKNOWN;
  r.id = 0;
  r.article[0] = '\0';
  if (!code || !code[0]) return r;

  if (startsWithNoCase(code, "web+spoolman:s-")) {
    r.id = parseIdAt(code + 15);
    if (r.id > 0) r.kind = SCAN_SPOOL_ID;
    return r;
  }
  if (startsWithNoCase(code, "web+spoolman:f-")) {
    r.id = parseIdAt(code + 15);
    if (r.id > 0) r.kind = SCAN_FILAMENT_ID;
    return r;
  }
  if (startsWithNoCase(code, "spool:")) {
    const char* p = code + 6;
    while (*p == ' ') p++;
    r.id = parseIdAt(p);
    if (r.id > 0) r.kind = SCAN_SPOOL_ID;
    return r;
  }
  if (startsWithNoCase(code, "http://") || startsWithNoCase(code, "https://")) {
    int id = idAfterMarker(code, "/spool/show/");
    if (id <= 0) id = idAfterMarker(code, "/spools/");
    if (id > 0) { r.kind = SCAN_SPOOL_ID; r.id = id; }
    return r;
  }

  size_t n = 0;
  if (allDigits(code, &n)) {
    if ((n == 8 || n == 12 || n == 13 || n == 14) && scanGtinValid(code)) {
      r.kind = SCAN_ARTICLE;
      snprintf(r.article, sizeof(r.article), "%s", code);
      return r;
    }
    if (n <= 6) {
      r.id = atoi(code);
      if (r.id > 0) r.kind = SCAN_SPOOL_ID;
    }
  }
  return r;
}
