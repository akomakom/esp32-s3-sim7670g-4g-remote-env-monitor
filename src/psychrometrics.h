// Derived moisture metrics — computed on-device, "free" (spec §2).
// These carry the moisture-attribution story for the mold dispute.
#pragma once

// Dew point in °C (Magnus/Arden-Buck approximation, good -40..60 °C).
float dewPointC(float temp_c, float rh);

// Absolute humidity in g/m^3.
float absoluteHumidity(float temp_c, float rh);
