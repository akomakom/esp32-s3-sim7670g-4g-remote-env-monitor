#include "timesync.h"
#include "log.h"
#include <time.h>

namespace timesync {

// A plausible lower bound: 2023-01-01. Below this, the clock is unset.
static const uint32_t SANE_EPOCH = 1672531200UL;

void begin() {
  // NTP over the cellular PPP link. All timestamps are UTC (no TZ offset).
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  LOGI("timesync: NTP requested");
}

bool isSynced() { return nowUtc() != 0; }

uint32_t nowUtc() {
  time_t t = time(nullptr);
  return (t > (time_t)SANE_EPOCH) ? (uint32_t)t : 0;
}

} // namespace timesync
