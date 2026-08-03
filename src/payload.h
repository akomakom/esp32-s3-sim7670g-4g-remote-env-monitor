// Compact batch payload encoding (spec §5, hard bandwidth requirement).
//
// We use MessagePack (via ArduinoJson) with 1-char keys — self-describing like
// JSON but roughly 40-60% smaller on the wire, and trivially decoded by Home
// Assistant / a small server shim. See docs/payload-format.md for the schema.
#pragma once
#include "reading.h"
#include <stddef.h>

namespace payload {

// Encode up to `n` readings into `out` as MessagePack. Returns bytes written,
// or 0 on error. Values are rounded to 2 decimals to shave bytes.
size_t encodeBatch(const Reading* readings, size_t n, uint8_t* out, size_t out_cap);

} // namespace payload
