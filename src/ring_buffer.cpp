#include "ring_buffer.h"
#include "config.h"
#include "log.h"
#include <LittleFS.h>

namespace ringbuf {

// On-disk layout:  [Header][record 0][record 1]...[record CAP-1]
// The record slots form a circular buffer indexed by head/tail.
struct Header {
  uint32_t magic;    // integrity check
  uint32_t cap;      // capacity in records
  uint32_t head;     // index of oldest record
  uint32_t count;    // number of valid records
};
static const uint32_t MAGIC = 0x524D4231; // "RMB1"
static const uint32_t CAP   = BUFFER_MAX_RECORDS;
static const size_t   REC_SZ = sizeof(Reading);
static const size_t   HDR_SZ = sizeof(Header);

static Header g_hdr;
static bool   g_ready = false;

static bool writeHeader() {
  File f = LittleFS.open(BUFFER_FILE_PATH, "r+");
  if (!f) return false;
  f.seek(0);
  bool ok = f.write((uint8_t*)&g_hdr, HDR_SZ) == HDR_SZ;
  f.close();
  return ok;
}

static void formatEmpty() {
  g_hdr = { MAGIC, CAP, 0, 0 };
  // Pre-size the file so record slots exist. Sparse write of the last byte.
  File f = LittleFS.open(BUFFER_FILE_PATH, "w");
  if (!f) { LOGE("buffer: create failed"); return; }
  f.write((uint8_t*)&g_hdr, HDR_SZ);
  // grow to full size
  size_t total = HDR_SZ + (size_t)CAP * REC_SZ;
  uint8_t zero = 0;
  f.seek(total - 1);
  f.write(&zero, 1);
  f.close();
  LOGW("buffer: (re)initialised empty, cap=%u rec=%uB", CAP, (unsigned)REC_SZ);
}

bool begin() {
  // The partition is NAMED "littlefs" (subtype spiffs); LittleFS.begin() defaults
  // to looking for a partition labeled "spiffs", so the label must be passed.
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    LOGE("LittleFS mount failed"); return false;
  }

  File f = LittleFS.open(BUFFER_FILE_PATH, "r");
  bool valid = false;
  if (f && f.size() >= (int)HDR_SZ) {
    f.read((uint8_t*)&g_hdr, HDR_SZ);
    valid = (g_hdr.magic == MAGIC && g_hdr.cap == CAP &&
             g_hdr.head < CAP && g_hdr.count <= CAP &&
             f.size() >= (int)(HDR_SZ + (size_t)CAP * REC_SZ));
  }
  if (f) f.close();

  if (!valid) formatEmpty();       // corrupt/first-boot -> safe empty
  g_ready = true;
  LOGI("buffer ready: %u/%u records", g_hdr.count, CAP);
  return true;
}

static size_t slotOffset(uint32_t logical) {
  uint32_t slot = (g_hdr.head + logical) % CAP;
  return HDR_SZ + (size_t)slot * REC_SZ;
}

bool push(const Reading& r) {
  if (!g_ready) return false;
  if (g_hdr.count >= CAP) {
    if (!BUFFER_DROP_OLDEST) { LOGW("buffer full, dropping new reading"); return false; }
    g_hdr.head = (g_hdr.head + 1) % CAP;   // evict oldest
    g_hdr.count--;
    LOGW("buffer full, dropped oldest");
  }
  uint32_t tail = (g_hdr.head + g_hdr.count) % CAP;
  File f = LittleFS.open(BUFFER_FILE_PATH, "r+");
  if (!f) return false;
  f.seek(HDR_SZ + (size_t)tail * REC_SZ);
  bool ok = f.write((uint8_t*)&r, REC_SZ) == REC_SZ;
  f.close();
  if (ok) { g_hdr.count++; writeHeader(); }
  return ok;
}

size_t count() { return g_ready ? g_hdr.count : 0; }

bool peek(size_t n, Reading& out) {
  if (!g_ready || n >= g_hdr.count) return false;
  File f = LittleFS.open(BUFFER_FILE_PATH, "r");
  if (!f) return false;
  f.seek(slotOffset(n));
  bool ok = f.read((uint8_t*)&out, REC_SZ) == REC_SZ;
  f.close();
  return ok;
}

void popFront(size_t n) {
  if (!g_ready) return;
  if (n >= g_hdr.count) { g_hdr.head = 0; g_hdr.count = 0; }
  else { g_hdr.head = (g_hdr.head + n) % CAP; g_hdr.count -= n; }
  writeHeader();
}

void clear() { g_hdr.head = 0; g_hdr.count = 0; writeHeader(); }

} // namespace ringbuf
