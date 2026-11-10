#include "audio.h"

#include <Arduino.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <math.h>

#include "config.h"

namespace audio {

struct Note {
  uint16_t freq;  // 0 = rest
  uint16_t ms;
  uint8_t duty;   // 0..255 (rough volume)
};

static const Note kJingle[] = {
    {523, 110, 90}, {0, 20, 0},  {659, 110, 90}, {0, 20, 0},   {784, 110, 90},
    {0, 20, 0},     {1047, 220, 90}, {0, 60, 0}, {784, 110, 90}, {0, 20, 0},
    {1047, 420, 90},
};
static const Note kConfirm[] = {{880, 50, 60}, {0, 20, 0}, {1320, 70, 60}};

static esp_timer_handle_t g_timer;
static volatile bool g_busy = false;
static const Note* volatile g_seq = nullptr;
static volatile int g_seqLen = 0;
static volatile int g_seqPos = 0;

static bool g_enabled = true;
static bool g_quiet = false;
static int g_geigerCpm = 0;
static uint32_t g_nextGeigerMs = 0;

static void silence() {
  ledcWrite(cfg::kLedcSpeaker, 0);  // pin held low: no amp hiss
}

static void tone(uint16_t freq, uint8_t duty) {
  if (freq == 0 || duty == 0) {
    silence();
    return;
  }
  ledcChangeFrequency(cfg::kLedcSpeaker, freq, 8);
  ledcWrite(cfg::kLedcSpeaker, duty);
}

// Plays the next note of the current sequence, or stops.
static void onTimer(void*) {
  if (g_seq && g_seqPos < g_seqLen) {
    const Note& n = g_seq[g_seqPos++];
    tone(n.freq, n.duty);
    esp_timer_start_once(g_timer, (uint64_t)n.ms * 1000);
    return;
  }
  g_seq = nullptr;
  silence();
  g_busy = false;
}

static void blip(uint16_t freq, uint8_t duty, uint32_t us) {
  if (!canPlay() || g_busy) return;
  g_busy = true;
  tone(freq, duty);
  esp_timer_start_once(g_timer, us);
}

static void play(const Note* seq, int len) {
  if (!canPlay()) return;
  esp_timer_stop(g_timer);
  g_seq = seq;
  g_seqLen = len;
  g_seqPos = 0;
  g_busy = true;
  onTimer(nullptr);
}

void begin() {
  pinMode(cfg::kPinSpeaker, OUTPUT);
  digitalWrite(cfg::kPinSpeaker, LOW);
  ledcSetup(cfg::kLedcSpeaker, 2000, 8);
  ledcAttachPin(cfg::kPinSpeaker, cfg::kLedcSpeaker);
  silence();
  esp_timer_create_args_t args = {};
  args.callback = onTimer;
  args.name = "audio";
  esp_timer_create(&args, &g_timer);
}

bool canPlay() { return g_enabled && !g_quiet; }

static void stopAll() {
  esp_timer_stop(g_timer);
  g_seq = nullptr;
  silence();
  g_busy = false;
}

void setEnabled(bool on) {
  if (g_enabled && !on) stopAll();
  g_enabled = on;
}

void setQuietHours(bool quiet) {
  if (!g_quiet && quiet) stopAll();
  g_quiet = quiet;
}

static uint32_t nextGeigerGapMs() {
  // Exponential spacing gives the random clustering of a real counter.
  const double mean = 60000.0 / g_geigerCpm;
  const double u = (esp_random() + 1.0) / 4294967297.0;
  double gap = -log(u) * mean;
  if (gap < 25) gap = 25;
  return (uint32_t)gap;
}

void setGeigerRate(int cpm) {
  if (cpm == g_geigerCpm) return;
  g_geigerCpm = cpm;
  g_nextGeigerMs = millis() + (cpm > 0 ? nextGeigerGapMs() : 0);
}

void tick(uint32_t now) {
  if (g_geigerCpm > 0 && (int32_t)(now - g_nextGeigerMs) >= 0) {
    geigerClick();
    g_nextGeigerMs = now + nextGeigerGapMs();
  }
}

void knobClick() { blip(3200, 40, 1500); }

void geigerClick() {
  // A short, slightly random-pitched pulse reads as a "tick".
  blip((uint16_t)(1800 + esp_random() % 2400), 128, 1200 + esp_random() % 1200);
}

void jingle() { play(kJingle, sizeof kJingle / sizeof kJingle[0]); }
void confirmBeep() { play(kConfirm, sizeof kConfirm / sizeof kConfirm[0]); }

}  // namespace audio
