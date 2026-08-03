#include "psychrometrics.h"
#include <math.h>

// Magnus coefficients (over water).
static const float A = 17.62f;
static const float B = 243.12f; // °C

float dewPointC(float temp_c, float rh) {
  if (rh <= 0.0f) rh = 0.01f;
  if (rh > 100.0f) rh = 100.0f;
  float gamma = (A * temp_c) / (B + temp_c) + logf(rh / 100.0f);
  return (B * gamma) / (A - gamma);
}

float absoluteHumidity(float temp_c, float rh) {
  if (rh < 0.0f) rh = 0.0f;
  if (rh > 100.0f) rh = 100.0f;
  // Saturation vapor pressure (hPa) via Magnus, then ideal-gas density.
  float es = 6.112f * expf((A * temp_c) / (B + temp_c)); // hPa
  float e  = es * (rh / 100.0f);                          // actual vapor press
  // AH (g/m^3) = 216.7 * e / (T[K]); e in hPa.
  return 216.7f * e / (temp_c + 273.15f);
}
