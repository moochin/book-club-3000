#include "bc_text.h"

#include <stdint.h>

namespace bc {

// U+00C0..U+00FF folded to ASCII ('?' where there is no sensible letter).
static const char kLatin1[65] =
    "AAAAAAACEEEEIIII"   // C0-CF
    "DNOOOOOxOUUUUYPs"   // D0-DF
    "aaaaaaaceeeeiiii"   // E0-EF
    "dnooooo/ouuuuypy";  // F0-FF

static char foldCodepoint(uint32_t cp) {
  if (cp < 0x80) return (char)cp;
  if (cp >= 0xC0 && cp <= 0xFF) return kLatin1[cp - 0xC0];
  switch (cp) {
    case 0xA0: return ' ';
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2026: return '~';
    case 0x0152: return 'O';  // OE
    case 0x0153: return 'o';
    case 0x0141: return 'L';
    case 0x0142: return 'l';
    case 0x0160: return 'S';
    case 0x0161: return 's';
    case 0x017D: return 'Z';
    case 0x017E: return 'z';
    default: return '?';
  }
}

void asciiFold(const char* src, char* dst, size_t cap) {
  if (!cap) return;
  size_t n = 0;
  const uint8_t* s = reinterpret_cast<const uint8_t*>(src ? src : "");
  while (*s && n + 1 < cap) {
    uint32_t cp;
    int extra;
    if (*s < 0x80) { cp = *s; extra = 0; }
    else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; extra = 1; }
    else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; extra = 2; }
    else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; extra = 3; }
    else { ++s; dst[n++] = '?'; continue; }
    ++s;
    bool ok = true;
    for (int i = 0; i < extra; ++i) {
      if ((*s & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (*s++ & 0x3F);
    }
    const char c = ok ? foldCodepoint(cp) : '?';
    if (c < 0x20 && c != 0) continue;  // drop control characters
    dst[n++] = c;
  }
  dst[n] = '\0';
}

void upperAscii(char* s) {
  for (; *s; ++s) {
    if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
  }
}

}  // namespace bc
