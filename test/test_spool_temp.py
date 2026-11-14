"""Compile the rule that decides which print temperatures a tag the scale
writes gets, and check it against real spools (services/spool_temp_rules.h).
The backend's value goes first, the material family's range only where the
backend names none (upstream 89a33ed, the tag half)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/spool_temp_rules.h"
#include <cassert>

static bool same(SpoolTempRange r, int lo, int hi) { return r.lo == lo && r.hi == hi; }

int main() {
  const SpoolTempRange pla = spoolTempRange(200, 220);

  // Nonsense is dropped: no upper end, one past any filament, a lower end
  // that is not below the upper one.
  assert(same(spoolTempRange(0, 0), 0, 0));
  assert(same(spoolTempRange(190, 0), 0, 0));
  assert(same(spoolTempRange(0, 500), 0, 0));
  assert(same(spoolTempRange(0, -5), 0, 0));
  assert(same(spoolTempRange(240, 240), 0, 240));
  assert(same(spoolTempRange(250, 240), 0, 240));
  assert(same(spoolTempRange(190, 230), 190, 230));

  // Spoolman's one value wins over the family and goes on both ends.
  assert(same(spoolTempTagEnds(spoolTempRange(0, 215), pla), 215, 215));
  // A range the backend holds is written as it is.
  assert(same(spoolTempTagEnds(spoolTempRange(190, 230), pla), 190, 230));
  // Nothing in the backend: the family's textbook range, as before.
  assert(same(spoolTempTagEnds(spoolTempRange(0, 0), pla), 200, 220));
  // Neither: nothing, and the tag gets no temperature rather than a wrong one.
  assert(same(spoolTempTagEnds(spoolTempRange(0, 0), spoolTempRange(0, 0)), 0, 0));
  // A typo in the backend does not reach the tag.
  assert(same(spoolTempTagEnds(spoolTempRange(0, 2150), pla), 200, 220));
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
print("spool temperature on a written tag: ok")
