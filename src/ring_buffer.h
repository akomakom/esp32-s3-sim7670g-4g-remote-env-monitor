// RAM-primary buffer for unsent readings, with flash as outage insurance (spec §6).
//
// Readings live in a RAM ring buffer (fast, no flash wear) and are published from
// there. Flash is written only occasionally: if data stays UNSENT longer than
// BUFFER_FLASH_INTERVAL_S (i.e. an outage), maintain() snapshots the buffer to a
// LittleFS file, at most once per interval, and clears it once everything drains.
// A reboot mid-outage restores the last snapshot (losing at most one interval).
// This trades strict per-reading durability (not required) for ~1000x fewer flash
// writes. A corrupt/missing snapshot restores empty rather than bricking.
#pragma once
#include "reading.h"

namespace ringbuf {

bool   begin();                 // mount FS, allocate RAM buffer, restore snapshot
bool   push(const Reading& r);  // append to RAM; drops oldest if full (per config)
size_t count();                 // records currently buffered (in RAM)
bool   peek(size_t n, Reading& out); // read the n-th oldest without removing
void   popFront(size_t n);      // drop the n oldest (after a confirmed publish)
void   clear();
void   maintain();              // call periodically from loop(): rate-limited flash snapshot
void   flushNow();              // force a snapshot now (e.g. before deep sleep)

} // namespace ringbuf
