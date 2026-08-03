// Accurate UTC (spec §3) — required for correct + backdated timestamps.
#pragma once
#include <Arduino.h>

namespace timesync {

void     begin();      // kick off NTP over the cellular link
bool     isSynced();   // true once we have a trustworthy UTC clock
uint32_t nowUtc();     // epoch seconds, or 0 if not yet synced

} // namespace timesync
