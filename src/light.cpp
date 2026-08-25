#include "light.h"
#include "config.h"
#include "log.h"
#include "runtime_config.h"

namespace light {

#if PIN_LDR >= 0

static portMUX_TYPE   s_mux       = portMUX_INITIALIZER_UNLOCKED;
static volatile int   s_level     = 0;       // last ADC reading (0..4095)
static volatile bool  s_occupied  = false;   // level above threshold (with hysteresis)
static volatile bool  s_event     = false;   // an unpublished occupancy event

// Dedicated sampler: runs at LIGHT_POLL_MS regardless of what the main loop is
// blocked on, so a light change is caught within ~200 ms (interrupt-like latency).
static void task(void*) {
  analogSetAttenuation(ADC_11db);              // full ~0..3.1 V input range
  pinMode(PIN_LDR, INPUT);

  int  last = analogRead(PIN_LDR);             // reference for change detection
  bool occ  = last >= (int)rconfig::get().light_threshold;
  s_level = last; s_occupied = occ;

  for (;;) {
    int lvl = analogRead(PIN_LDR);
    const auto& cfg = rconfig::get();
    int thr = (int)cfg.light_threshold;
    int hys = thr / 20 + 1;                     // ~5% hysteresis to stop flapping

    bool now_occ = occ;
    if (!occ && lvl > thr + hys)      now_occ = true;
    else if (occ && lvl < thr - hys)  now_occ = false;

    bool crossed = (now_occ != occ);
    bool jumped  = abs(lvl - last) >= (int)cfg.light_delta;

    if (crossed || jumped) {
      if (crossed)
        LOGI("occupancy: %s (light=%d thr=%d)",
             now_occ ? "OCCUPIED (lit)" : "vacant (dark)", lvl, thr);
      else
        LOGI("occupancy: light changed %d -> %d (delta>=%d)", last, lvl, (int)cfg.light_delta);
      portENTER_CRITICAL(&s_mux);
      s_level = lvl; s_occupied = now_occ; s_event = true;
      portEXIT_CRITICAL(&s_mux);
      last = lvl;
      occ  = now_occ;
    } else {
      s_level = lvl;                            // keep the level fresh for health
    }
    vTaskDelay(pdMS_TO_TICKS(LIGHT_POLL_MS));
  }
}

void begin() {
  xTaskCreatePinnedToCore(task, "light", 2560, nullptr, 1, nullptr, 1);
  LOGI("occupancy: GL5528 on GPIO%d (ADC1), threshold=%u delta=%u",
       PIN_LDR, rconfig::get().light_threshold, rconfig::get().light_delta);
}

int  level()    { return s_level; }
bool occupied() { return s_occupied; }

bool takeEvent(bool& occ_out, int& lvl_out) {
  bool ev;
  portENTER_CRITICAL(&s_mux);
  ev = s_event; s_event = false;
  occ_out = s_occupied; lvl_out = s_level;
  portEXIT_CRITICAL(&s_mux);
  return ev;
}

#else  // PIN_LDR < 0 -> disabled

void begin() {}
int  level() { return -1; }
bool occupied() { return false; }
bool takeEvent(bool&, int&) { return false; }

#endif

} // namespace light
