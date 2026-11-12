#include "audio.h"

#include <Arduino.h>
#include <driver/dac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <math.h>
#include <soc/rtc_io_reg.h>
#include <soc/sens_reg.h>

#include "audio_gen.h"
#include "config.h"
#include "sound_data.h"  // generated from ../art/sounds by tools/build_sounds.py

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
static bool g_dacMode = false;  // pin on the DAC for recorded clips (else on the LEDC)

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
  // Scale every generated sound: full duty pulled enough current through the
  // CYD's speaker amp to sag USB power. Keep at least 1 so quiet hums survive.
  uint32_t d = (uint32_t)duty * cfg::kToneLevel >> 8;
  ledcWrite(cfg::kLedcSpeaker, d ? d : 1);
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

static void stopClips(bool now);

static void blip(uint16_t freq, uint8_t duty, uint32_t us) {
  if (!canPlay()) return;
  Lock l;
  if (g_fg != Fg::None || g_dacMode) return;  // never cut into a tune or clip; the hum it may
  esp_timer_stop(g_timer);
  g_fg = Fg::Blip;
  tone(freq, duty);
  schedule(us);
}

static void play(const Note* seq, int len) {
  if (!canPlay()) return;
  Lock l;
  stopClips(true);
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
  stopClips(true);
  init(g_gen);
  g_fg = Fg::Gen;
  g_genStartUs = esp_timer_get_time();
  restart();
}


// ---- Recorded clips (art/sounds) --------------------------------------------
// The speaker pin is also DAC 2, so recorded sound plays as real 8-bit audio:
// a hardware timer interrupt writes one sample per tick. Two voices are mixed:
// one-shots, and the settings hum loop under them. While a clip plays, the pin
// belongs to the DAC; the generated sounds above need it back on the LEDC.

// Slot order: as SLOTS in tools/build_sounds.py.
enum class Clip : uint8_t {
  Boot, TabNext, TabPrev, SettingsOpen, SettingsHum, SettingsScroll, SettingsClose, Knob,
  Confirm, Nudge, NudgeSent, Error, Sync, MeetingDay, Birthday, Geiger, Count
};
static_assert((int)Clip::Count == sounddata::kClipCount, "Clip is out of step with build_sounds.py");

static bool has(Clip c) { return sounddata::kClips[(int)c].data != nullptr; }

// Who may cut in on whom: a knob tick never cuts off a nudge.
static uint8_t priority(Clip c) {
  switch (c) {
    case Clip::Knob:
    case Clip::Geiger: return 0;
    case Clip::Boot:
    case Clip::Nudge:
    case Clip::NudgeSent:
    case Clip::Error:
    case Clip::Sync:
    case Clip::MeetingDay:
    case Clip::Birthday: return 2;
    default: return 1;
  }
}

struct PcmVoice {
  const uint8_t* data = nullptr;
  uint32_t len = 0;
  uint32_t pos = 0;
  bool loops = false;
  bool active = false;
  uint32_t gain = 0;    // 0..65535
  uint32_t target = 0;  // gain glides here by `step` per sample
  uint32_t step = 0;
};

static portMUX_TYPE g_pcmMux = portMUX_INITIALIZER_UNLOCKED;
static PcmVoice g_pcm[2];  // [0] one-shots, [1] the hum loop
static bool g_dacWanted = false;  // the DAC is to sit at mid-scale (or play)
static uint16_t g_bias = 0;       // DAC level, x4, ramped to soften the thump
static hw_timer_t* g_sampleTimer = nullptr;
static Clip g_oneShot = Clip::Count;
static uint32_t g_dacLastActiveMs = 0;

constexpr uint16_t kBiasTop = cfg::kClipBias * 4;  // ramped up over ~15 ms at 11 kHz
constexpr uint32_t kGainMax = 65535;

static inline void IRAM_ATTR dacOut(uint32_t v) {
  SET_PERI_REG_BITS(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_DAC, v, RTC_IO_PDAC2_DAC_S);
}

// One output sample. The timer interrupt is not IRAM-only, so it is held off
// while flash is busy, and may read the clips straight from flash.
static void IRAM_ATTR onSample() {
  portENTER_CRITICAL_ISR(&g_pcmMux);
  if (!g_dacWanted || g_bias < kBiasTop) {
    if (g_dacWanted) ++g_bias;
    else if (g_bias) --g_bias;
    dacOut(g_bias >> 2);
    portEXIT_CRITICAL_ISR(&g_pcmMux);
    return;
  }
  int32_t acc = 0;
  for (PcmVoice& v : g_pcm) {
    if (!v.active) continue;
    if (v.gain < v.target) v.gain = v.target - v.gain > v.step ? v.gain + v.step : v.target;
    else if (v.gain > v.target) v.gain = v.gain - v.target > v.step ? v.gain - v.step : v.target;
    if (v.gain == 0 && v.target == 0) {
      v.active = false;
      continue;
    }
    acc += ((int32_t)v.data[v.pos] - 128) * (int32_t)(v.gain >> 8);
    if (++v.pos >= v.len) {
      if (v.loops) v.pos = 0;
      else v.active = false;
    }
  }
  int32_t out = cfg::kClipBias + (((acc >> 8) * cfg::kClipSwing) >> 7);
  dacOut(out < 0 ? 0 : out > 255 ? 255 : out);
  portEXIT_CRITICAL_ISR(&g_pcmMux);
}

static bool pcmActive(int i) {
  portENTER_CRITICAL(&g_pcmMux);
  const bool a = g_pcm[i].active;
  portEXIT_CRITICAL(&g_pcmMux);
  return a;
}

// Hands the pin to the DAC, stopping any generated sound. Lock held.
static void enterDac() {
  if (g_dacMode) return;
  esp_timer_stop(g_timer);
  g_fg = Fg::None;
  g_seq = nullptr;
  g_ambRunning = false;
  g_dueUs = INT64_MAX;
  silence();
  ledcDetachPin(cfg::kPinSpeaker);
  dac_output_enable(DAC_CHANNEL_2);
  // The DAC's cosine generator owns its output out of reset (SENS_DAC_CW_EN2
  // defaults to 1), so dacOut()'s raw writes went nowhere: clips played as
  // a hiss of exactly their length. Hand the output to the register.
  CLEAR_PERI_REG_MASK(SENS_SAR_DAC_CTRL2_REG, SENS_DAC_CW_EN2_M);
  dac_output_voltage(DAC_CHANNEL_2, 0);
  portENTER_CRITICAL(&g_pcmMux);
  g_bias = 0;
  portEXIT_CRITICAL(&g_pcmMux);
  timerAlarmEnable(g_sampleTimer);
  g_dacMode = true;
}

// Gives the pin back to the LEDC once the DAC has ramped down. Lock held.
static void finishDac() {
  timerAlarmDisable(g_sampleTimer);
  dac_output_disable(DAC_CHANNEL_2);
  ledcAttachPin(cfg::kPinSpeaker, cfg::kLedcSpeaker);
  silence();
  g_dacMode = false;
}

// Stops all clips. With `now`, waits out the ramp-down and hands the pin
// back at once (a generated sound wants it); else tick() finishes later.
static void stopClips(bool now) {
  if (!g_dacMode) return;
  portENTER_CRITICAL(&g_pcmMux);
  g_pcm[0].active = g_pcm[1].active = false;
  g_dacWanted = false;
  portEXIT_CRITICAL(&g_pcmMux);
  if (!now) return;
  for (int i = 0; i < 100; ++i) {
    portENTER_CRITICAL(&g_pcmMux);
    const bool down = g_bias == 0;
    portEXIT_CRITICAL(&g_pcmMux);
    if (down) break;
    delay(1);
  }
  finishDac();
}

static void startVoice(int i, Clip c, uint32_t gain, uint32_t step) {
  const sounddata::Clip& k = sounddata::kClips[(int)c];
  portENTER_CRITICAL(&g_pcmMux);
  PcmVoice& v = g_pcm[i];
  v.data = k.data;
  v.len = k.len;
  v.pos = 0;
  v.loops = k.loops;
  v.gain = gain;
  v.target = kGainMax;
  v.step = step;
  v.active = true;
  g_dacWanted = true;
  portEXIT_CRITICAL(&g_pcmMux);
}

// Plays the recorded clip for this slot. False when there is no file for it
// (the caller then plays the generated sound).
static bool playClip(Clip c) {
  if (!has(c)) return false;
  if (!canPlay()) return true;
  Lock l;
  const uint8_t pri = priority(c);
  if (pcmActive(0) && g_oneShot != Clip::Count && priority(g_oneShot) > pri) return true;
  if (!g_dacMode && g_fg != Fg::None && pri == 0) return true;  // a tick never cuts a tune
  enterDac();
  startVoice(0, c, kGainMax, 0);
  g_oneShot = c;
  g_dacLastActiveMs = millis();
  return true;
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
  // Sample clock for recorded clips: 1 MHz ticks, one alarm per sample.
  g_sampleTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(g_sampleTimer, onSample, true);
  timerAlarmWrite(g_sampleTimer, 1000000 / sounddata::kRate, true);
}

bool canPlay() { return g_enabled && !g_quiet; }

static void stopAll() {
  Lock l;
  stopClips(false);
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
  if (!g_dacMode) return;
  // Clips done: ramp the DAC down after a short pause (another may follow),
  // then hand the pin back.
  Lock l;
  const bool playing = pcmActive(0) || pcmActive(1);
  if (playing) g_dacLastActiveMs = now;
  portENTER_CRITICAL(&g_pcmMux);
  if (!playing && g_dacWanted && now - g_dacLastActiveMs > 300) g_dacWanted = false;
  const bool down = !g_dacWanted && g_bias == 0;
  portEXIT_CRITICAL(&g_pcmMux);
  if (down) finishDac();
}

void knobClick() {
  if (playClip(Clip::Knob)) return;
  blip(3200, 40, 1500);
}

void geigerClick() {
  if (playClip(Clip::Geiger)) return;
  // A short, slightly random-pitched pulse reads as a "tick".
  blip((uint16_t)(1800 + esp_random() % 2400), 128, 1200 + esp_random() % 1200);
}

void jingle() {
  if (playClip(Clip::MeetingDay)) return;
  play(kJingle, sizeof kJingle / sizeof kJingle[0]);
}
void birthday() {
  if (playClip(Clip::Birthday)) return;
  jingle();
}
void confirmBeep() {
  if (playClip(Clip::Confirm)) return;
  play(kConfirm, sizeof kConfirm / sizeof kConfirm[0]);
}
void nudgeChirp() {
  if (playClip(Clip::Nudge)) return;
  play(kNudge, sizeof kNudge / sizeof kNudge[0]);
}
// No generated versions of these: silent unless a clip is supplied.
void nudgeSent() { playClip(Clip::NudgeSent); }
void errorBuzz() { playClip(Clip::Error); }
void newData() { playClip(Clip::Sync); }
void settingsClose() { playClip(Clip::SettingsClose); }

void bootTape() {
  if (playClip(Clip::Boot)) return;
  playGen([](gen::Voice& v) { gen::bootTape(v); });
}

void tabAdvance(int dir) {
  if (dir < 0 && playClip(Clip::TabPrev)) return;
  if (playClip(Clip::TabNext)) return;
  playGen([dir](gen::Voice& v) { gen::tabAdvance(v, dir); });
}

void settingsWarmUp() {
  if (playClip(Clip::SettingsOpen)) return;
  playGen([](gen::Voice& v) { gen::settingsWarmUp(v); });
  // The warm-up already brought the hum up: carry on from full.
  Lock l;
  g_amb.level = 1;
}

void tunerScan(int fromPos, int toPos, int count) {
  if (playClip(Clip::SettingsScroll)) return;
  playGen([=](gen::Voice& v) { gen::tunerScan(v, fromPos, toPos, count); });
}

void setAmbient(bool on) {
  if (!canPlay()) return;
  Lock l;
  if (has(Clip::SettingsHum)) {
    // The recorded hum loops under the one-shots, fading in and out.
    const uint32_t fadeIn = kGainMax / (sounddata::kRate * 4 / 10);   // 0.4 s
    const uint32_t fadeOut = kGainMax / (sounddata::kRate * 6 / 10);  // 0.6 s
    if (!pcmActive(1)) {
      if (!on || g_fg != Fg::None) return;  // wait for a generated sound to finish
      enterDac();
      startVoice(1, Clip::SettingsHum, 0, fadeIn);
      g_dacLastActiveMs = millis();
      return;
    }
    portENTER_CRITICAL(&g_pcmMux);
    g_pcm[1].target = on ? kGainMax : 0;
    g_pcm[1].step = on ? fadeIn : fadeOut;
    portEXIT_CRITICAL(&g_pcmMux);
    return;
  }
  g_amb.on = on;
  if (!on || g_ambRunning || g_dacMode) return;  // in DAC mode: resume once clips end
  gen::ambient(g_amb);
  g_ambRunning = true;
  g_ambStartUs = esp_timer_get_time();
  if (g_fg == Fg::None) restart();
}

}  // namespace audio
