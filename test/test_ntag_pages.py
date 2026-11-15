"""Compile the rule that decides how far a tag write may go
(services/ntag_pages.h) and check it against the capability containers of
real chips: no write may reach the lock or configuration pages behind the
user memory, which would put the tag behind a password or lock it for good.
Then check that every write in services/tag_write.cpp asks the rule first."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/ntag_pages.h"
#include <cassert>
#include <cstdint>

static uint8_t lastFor(uint8_t c0, uint8_t c2) {
  const uint8_t cc[4] = {c0, 0x10, c2, 0x00};
  return ntagLastUserPage(ntagUserBytesFromCc(cc));
}

int main() {
  // The containers the chips leave the factory with, and the last user page
  // each has; the next page is lock bits or AUTH0.
  assert(lastFor(0xE1, 0x06) == 15);    // NTAG210, Ultralight EV1 MF0UL11: CFG0 on 16
  assert(lastFor(0xE1, 0x10) == 35);    // NTAG212, MF0UL21: lock bits on 36
  assert(lastFor(0xE1, 0x12) == 39);    // NTAG213: lock bits on 40
  assert(lastFor(0xE1, 0x3E) == 127);   // NTAG215: user memory to 129
  assert(lastFor(0xE1, 0x6D) == 221);   // NTAG216: user memory to 225
  // A size past what the library writes is cut to it, never wrapped.
  assert(lastFor(0xE1, 0xEA) == NTAG_PAGE_MAX);
  assert(lastFor(0xE1, 0xFF) == NTAG_PAGE_MAX);

  // A blank container (an Ultralight EV1 from the factory), one that is not
  // NDEF, or one with no size: the user memory every Type 2 tag has.
  assert(lastFor(0x00, 0x00) == 15);
  assert(lastFor(0xE1, 0x00) == 15);
  assert(lastFor(0x00, 0x3E) == 15);
  assert(NTAG_FALLBACK_LAST_PG < 16);   // below AUTH0 of the smallest chips
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

# Every way onto the tag asks first: the ACE map, the NDEF record and the erase.
tw = (root / "src" / "services" / "tag_write.cpp").read_text(encoding="utf-8")


def body(name):
    m = re.search(r"static bool " + name + r"\([^)]*\)\s*\{", tw)
    assert m, name + "() not found in tag_write.cpp"
    depth, i = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(tw[i], 0)
        i += 1
    return tw[m.end():i]


ace = body("writeAce")
assert ace.find("tagHolds(ACE_P_WEIGHT") >= 0, "writeAce() writes without asking tagHolds()"
assert ace.find("tagHolds(") < ace.find("wrPage("), "writeAce() writes before tagHolds()"
ndef = body("writeNdefJson")
assert ndef.find("tagHolds(") >= 0, "writeNdefJson() writes without asking tagHolds()"
assert ndef.find("tagHolds(") < ndef.find("wrPage("), "writeNdefJson() writes before tagHolds()"
assert "lastUserPage()" in body("eraseTag"), "eraseTag() does not stop at lastUserPage()"
# The ACE map has to fit the smallest tag that names its size, NTAG213.
ace_last = int(re.search(r"#define ACE_P_WEIGHT\s+(\d+)", tw).group(1))
assert 15 < ace_last <= 39, "the ACE map no longer sits between the fallback and NTAG213"
print("tag write stays inside the user memory: ok")
