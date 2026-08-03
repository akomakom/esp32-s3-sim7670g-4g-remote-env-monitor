// Tiny leveled logger over USB serial. Compiled out below LOG_LEVEL.
#pragma once
#include <Arduino.h>
#include "config.h"

inline void logInit() { Serial.begin(LOG_BAUD); }

#define LOGE(fmt, ...) do { if (LOG_LEVEL >= 1) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGW(fmt, ...) do { if (LOG_LEVEL >= 2) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGI(fmt, ...) do { if (LOG_LEVEL >= 3) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGD(fmt, ...) do { if (LOG_LEVEL >= 4) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__); } while (0)
