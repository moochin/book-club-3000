#include "audio.h"

#include <Arduino.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <math.h>

#include "audio_gen.h"
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
static const Note kNudge[] = {{1568, 45, 70}, {0, 40, 0}, {1568, 45, 70}, {0, 60, 0},
                              {1047, 60, 80}, {1397, 60, 80}, {2093, 160, 90}};

// What the one voice is doing. The foreground (a note sequence, a generated
// sound or a single blip) wins; the ambient hum fills the gaps.
enum class Fg : uint8_t { None, Seq, Gen, Blip };

// The timer callback runs in the esp_timer task; the public calls come from
// the main loop. g_lock keeps them apart.
static SemaphoreHandle_t g_lock;
static esp_timer_handle_t g_timer;
static int64_t g_dueUs = INT64_MAX;  // when the pending step is due

static Fg g_fg = Fg::None;
static const Note* g_seq = nullptr;
static int g_seqLen = 0;
static int g_seqPos = 0;
static gen::Voice g_gen;
static int64_t g_genStartUs = 0;
static gen::Voice g_amb;
static bool g_ambRunning = false;
static int64_t g_ambStartUs = 0;

static bool g_enabled = true;
static bool g_quiet = false;
static int g_geigerCpm = 0;
static uint32_t g_nextGeigerMs = 0;
static uint16_t g_curFreq = 0;

static void silence() {
  ledcWrite(cfg::kLedcSpeaker, 0);  // pin held low: no amp hiss
}

static void tone(uint16_t freq, uint8_t duty) {
  if (freq == 0 || duty == 0) {
    silence();
    return;
  }
  // Retiming the LEDC timer is the slow part; skip it when the pitch holds.
  if (freq != g_curFreq) {
    ledcChangeFrequency(cfg::kLedcSpeaker, freq, 8);
    g_curFreq = freq;
  }
  ledcWrite(cfg::kLedcSpeaker, duty);
}

static void schedule(uint32_t us) {
  g_dueUs = esp_timer_get_time() + us;
  esp_timer_start_once(g_timer, us);
}

static bool runVoice(gen::Voice& v, int64_t startUs) {
  gen::Out o;
  if (!v.step || !v.step(v, (uint32_t)(esp_timer_get_time() - startUs), o)) return false;
  tone(o.freq, o.duty);
  schedule(o.us);
  return true;
}

// Plays the next step of whatever is sounding, or goes quiet. Lock held.
static void step() {
  switch (g_fg) {
    case Fg::Seq:
      if (g_seq && g_seqPos < g_seqLen) {
        const Note& n = g_seq[g_seqPos++];
        tone(n.freq, n.duty);
        schedule((uint32_t)n.ms * 1000);
        return;
      }
      break;
    case Fg::Gen:
      if (runVoice(g_gen, g_genStartUs)) return;
      break;
    case Fg::Blip:
    case Fg::None:
      break;
  }
  g_fg = Fg::None;
  g_seq = nullptr;
  if (g_ambRunning) {
    if (runVoice(g_amb, g_ambStartUs)) return;
    g_ambRunning = false;
  }
  silence();
  g_dueUs = INT64_MAX;
}

static void onTimer(void*) {
  xSemaphoreTake(g_lock, portMAX_DELAY);
  // A call from the loop may have restarted the timer while this callback
  // was already on its way; then this one is stale.
  if (esp_timer_get_time() + 50 >= g_dueUs) step();
  xSemaphoreGive(g_lock);
}

struct Lock {
  Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_lock); }
};

static void restart() {
  esp_timer_stop(g_timer);
  step();
}

static void blip(uint16_t freq, uint8_t duty, uint32_t us) {
  if (!canPlay()) return;
  Lock l;
  if (g_fg != Fg::None) return;  // never cut into a tune; the hum it may
  esp_timer_stop(g_timer);
  g_fg = Fg::Blip;
  tone(freq, duty);
  schedule(us);
}

static void play(const Note* seq, int len) {
  if (!canPlay()) return;
  Lock l;
  g_fg = Fg::Seq;
  g_seq = seq;
  g_seqLen = len;
  g_seqPos = 0;
  restart();
}

// Starts a generated sound; `init` sets up g_gen.
template <typename F>
static void playGen(F init) {
  if (!canPlay()) return;
  Lock l;
  init(g_gen);
  g_fg = Fg::Gen;
  g_genStartUs = esp_timer_get_time();
  restart();
}

void begin() {
  pinMode(cfg::kPinSpeaker, OUTPUT);
  digitalWrite(cfg::kPinSpeaker, LOW);
  ledcSetup(cfg::kLedcSpeaker, 2000, 8);
  ledcAttachPin(cfg::kPinSpeaker, cfg::kLedcSpeaker);
  g_curFreq = 2000;
  silence();
  g_lock = xSemaphoreCreateMutex();
  esp_timer_create_args_t args = {};
  args.callback = onTimer;
  args.name = "audio";
  esp_timer_create(&args, &g_timer);
}

bool canPlay() { return g_enabled && !g_quiet; }

static void stopAll() {
  Lock l;
  esp_timer_stop(g_timer);
  g_fg = Fg::None;
  g_seq = nullptr;
  g_ambRunning = false;
  g_amb.level = 0;
  silence();
  g_dueUs = INT64_MAX;
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
void nudgeChirp() { play(kNudge, sizeof kNudge / sizeof kNudge[0]); }

void bootTape() {
  playGen([](gen::Voice& v) { gen::bootTape(v); });
}

void tabAdvance(int dir) {
  playGen([dir](gen::Voice& v) { gen::tabAdvance(v, dir); });
}

void settingsWarmUp() {
  playGen([](gen::Voice& v) { gen::settingsWarmUp(v); });
  // The warm-up already brought the hum up: carry on from full.
  Lock l;
  g_amb.level = 1;
}

void tunerScan(int fromPos, int toPos, int count) {
  playGen([=](gen::Voice& v) { gen::tunerScan(v, fromPos, toPos, count); });
}

void setAmbient(bool on) {
  if (!canPlay()) return;
  Lock l;
  g_amb.on = on;
  if (!on || g_ambRunning) return;
  gen::ambient(g_amb);
  g_ambRunning = true;
  g_ambStartUs = esp_timer_get_time();
  if (g_fg == Fg::None) restart();
}

}  // namespace audio
