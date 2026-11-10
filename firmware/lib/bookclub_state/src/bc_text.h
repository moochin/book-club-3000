// Text helpers for the ASCII-only bitmap font.
#pragma once

#include <stddef.h>

namespace bc {

// Copies UTF-8 `src` into `dst` as plain ASCII: accented Latin letters lose
// their accents, curly quotes and dashes become straight ones, anything else
// outside ASCII becomes '?'. Always NUL-terminates.
void asciiFold(const char* src, char* dst, size_t cap);

// In-place upper-casing of ASCII letters.
void upperAscii(char* s);

}  // namespace bc
