// Plain-words help for the sync warning (the triangle by the clock): what the
// last fetch's error means and what to do about it. Pure, so it's tested on
// the host.
#pragma once

namespace bc {

// `error` is the sync module's last error ("" if none); `stale` means no good
// sync for 7+ days. Returns a sentence or two of advice, never null.
const char* syncAdvice(const char* error, bool stale);

}  // namespace bc
