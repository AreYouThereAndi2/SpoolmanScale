"""Compile the production rule for the format a tag is linked with in
Spoolman's tag relation, and check that a MIFARE card is no longer filed as an
NTAG (services/tag_format_rules.h). From upstream a0e1885, without the
Creality case this fork does not read."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/tag_format_rules.h"
#include <cassert>
#include <cstring>

static bool is(const char* got, const char* want) { return strcmp(got, want) == 0; }

int main() {
  // A Bambu tray uuid.
  assert(is(tagFormatFor(32), "bambu"));

  // A 7 byte UID is an NTAG.
  assert(is(tagFormatFor(14), "ntag"));

  // A 4 byte card is a plain MIFARE card. This was "ntag" before.
  assert(is(tagFormatFor(8), "mifare"));

  // Anything else keeps the old name.
  assert(is(tagFormatFor(0), "ntag"));
  assert(is(tagFormatFor(20), "ntag"));
  return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("tag format rules: ok")
