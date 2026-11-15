#include "diag.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace diag {

namespace {

constexpr uint32_t kMagic = 0xB00C3000;

// RTC memory that a watchdog restart doesn't clear (a power cut does).
RTC_NOINIT_ATTR uint32_t r_magic;
RTC_NOINIT_ATTR char r_task[2][16];  // running on core 0 and core 1 when it fired
RTC_NOINIT_ATTR uint32_t r_upSecs;

char g_started[48] = "UNKNOWN";
bool g_bad = false;

void copyName(char* dst, TaskHandle_t t) {
  const char* n = t ? pcTaskGetName(t) : nullptr;
  size_t i = 0;
  for (; n && n[i] && i < 15; ++i) dst[i] = n[i];
  dst[i] = 0;
}

}  // namespace

}  // namespace diag

// Called by ESP-IDF's task watchdog interrupt just before it restarts the
// unit (a weak symbol in esp_system). The task found running on core 0 is
// usually the one that starved the idle task there.
extern "C" void esp_task_wdt_isr_user_handler(void) {
  using namespace diag;
  copyName(r_task[0], xTaskGetCurrentTaskHandleForCPU(0));
  copyName(r_task[1], xTaskGetCurrentTaskHandleForCPU(1));
  r_upSecs = (uint32_t)(esp_timer_get_time() / 1000000);
  r_magic = kMagic;
}

namespace diag {

void begin() {
  const esp_reset_reason_t why = esp_reset_reason();
  const bool recorded = r_magic == kMagic;
  r_magic = 0;
  switch (why) {
    case ESP_RST_POWERON: strlcpy(g_started, "POWER ON", sizeof g_started); break;
    case ESP_RST_EXT: strlcpy(g_started, "RESET BUTTON", sizeof g_started); break;
    case ESP_RST_SW: strlcpy(g_started, "RESTART (UPDATE OR SETTINGS)", sizeof g_started); break;
    case ESP_RST_PANIC: strlcpy(g_started, "CRASH", sizeof g_started); g_bad = true; break;
    case ESP_RST_BROWNOUT: strlcpy(g_started, "BROWNOUT (POWER DIP)", sizeof g_started); g_bad = true; break;
    case ESP_RST_INT_WDT: strlcpy(g_started, "WATCHDOG (INTERRUPT)", sizeof g_started); g_bad = true; break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      g_bad = true;
      if (recorded) {
        snprintf(g_started, sizeof g_started, "WATCHDOG: %s (UP %lu MIN)", r_task[0][0] ? r_task[0] : "?",
                 (unsigned long)(r_upSecs / 60));
      } else {
        strlcpy(g_started, "WATCHDOG (FROZE)", sizeof g_started);
      }
      break;
    default: break;
  }
  Serial.printf("[diag] started by %s", g_started);
  if (recorded) Serial.printf(" (core 0: %s, core 1: %s)", r_task[0], r_task[1]);
  Serial.println();
}

const char* startedBy(bool& bad) {
  bad = g_bad;
  return g_started;
}

void memoryText(char* buf, size_t cap) {
  snprintf(buf, cap, "%uK FREE, LOW %uK, BLOCK %uK", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(esp_get_minimum_free_heap_size() / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
}

}  // namespace diag
