// Persistent flash ring buffer for unsent readings (spec §6).
//
// Design goals: no data loss across reboot, survive multi-day outages, and
// flush backdated when the link returns. We store fixed-size records in a
// LittleFS file with a small header (head/tail/count). Fixed record size keeps
// indexing O(1) and avoids fragmentation; a corrupt file re-initialises to
// empty rather than bricking (spec §6 "boot resilience").
#pragma once
#include "reading.h"

namespace ringbuf {

bool   begin();                 // mounts LittleFS, opens/creates the buffer
bool   push(const Reading& r);  // append; drops oldest if full (per config)
size_t count();                 // records currently buffered
bool   peek(size_t n, Reading& out); // read the n-th oldest without removing
void   popFront(size_t n);      // drop the n oldest (after a confirmed publish)
void   clear();

} // namespace ringbuf
