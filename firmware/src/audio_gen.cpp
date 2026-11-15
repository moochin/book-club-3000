#include "audio_gen.h"

#include <math.h>

namespace audio {
namespace gen {

namespace {

constexpr uint32_t ms(uint32_t x) { return x * 1000; }
constexpr float kTau = 6.2831853f;

uint32_t g_rng = 0x2545F491u;

void set(Out& o, float freq, float duty, uint32_t us) {
  o.freq = freq < 1 ? 0 : (uint16_t)freq;
  o.duty = duty < 0 ? 0 : duty > 255 ? 255 : (uint8_t)duty;
  o.us = us < kMinStepUs ? kMinStepUs : us;
}

void silence(Out& o, uint32_t us) { set(o, 0, 0, us); }

// White-ish noise: a random pitch for a fraction of a millisecond.
void hiss(Out& o, uint16_t lo, uint16_t hi, float duty) {
  set(o, (float)(lo + rnd() % (uint32_t)(hi - lo + 1)), duty, 150 + rnd() % 250);
}

// Ends the step at a segment boundary, so segments start on time.
void clampTo(Out& o, uint32_t t, uint32_t end) {
  if (t + o.us > end) o.us = end > t + kMinStepUs ? end - t : kMinStepUs;
}

float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float smooth(float x) { return x * x * (3 - 2 * x); }
float secs(uint32_t t) { return t * 1e-6f; }

// Exponential gap with the given mean: random, Poisson-style spacing.
uint32_t poissonGapUs(float meanUs) {
  const float u = (float)((rnd() >> 8) + 1) / 16777217.0f;
  return (uint32_t)(-logf(u) * meanUs);
}

// Start-up: a soft beep as each line of the boot log prints, a lower one for
// the low snack reserves, then a quiet two-note "ready". Deliberately gentle:
// the old cassette-loading screech was too much first thing. drawBoot (ui.cpp)
// shows message 0 at once and message i at i * 236.5 - 118 ms (12 messages,
// hex dumps between, over 85% of the 3.2 s splash); the beeps follow that.
constexpr uint32_t kBeepGapUs = 236500;
constexpr uint32_t kBeepUs = ms(14);
constexpr int kLastMsg = 11;  // ALL SYSTEMS NOMINAL: the "ready" notes
constexpr int kLowMsg = 6;    // CHECKING SNACK RESERVES ... LOW
constexpr uint32_t kReadyNoteUs = ms(70);

uint32_t msgAtUs(int i) { return i == 0 ? 0 : i * kBeepGapUs - kBeepGapUs / 2; }

bool bootStep(Voice&, uint32_t t, Out& o) {
  const uint32_t readyAt = msgAtUs(kLastMsg);
  if (t < readyAt) {
    int i = 0;  // the message whose line printed last
    while (i + 1 < kLastMsg && msgAtUs(i + 1) <= t) ++i;
    const uint32_t u = t - msgAtUs(i);
    if (u < kBeepUs) return set(o, i == kLowMsg ? 880 : 1760, 10, kBeepUs - u), true;
    const uint32_t next = msgAtUs(i + 1);
    return silence(o, next - t), true;
  }
  const uint32_t u = t - readyAt;
  if (u < kReadyNoteUs) return set(o, 1320, 14, kReadyNoteUs - u), true;
  if (u < 2 * kReadyNoteUs) {  // the second note, fading out
    const float p = (u - kReadyNoteUs) / (float)kReadyNoteUs;
    set(o, 1760, 14 * (1 - p), 2000);
    clampTo(o, u, 2 * kReadyNoteUs);
    return true;
  }
  return false;
}

constexpr uint32_t kWhirUs = ms(150);
constexpr uint32_t kSnapAt = ms(172);

bool tabStep(Voice& v, uint32_t t, Out& o) {
  if (t < kWhirUs) {
    // Film rushing past: a rising whir, roughened by the motor, with a tick
    // for each frame going by, faster and faster.
    const float p = t / (float)kWhirUs;
    const float e = smooth(p);
    float f = v.from > 0 ? 900 + 1700 * e : 2600 - 1500 * e;
    if ((t / 1200) & 1) f *= 1.09f;
    if (t >= v.nextEvent) {
      hiss(o, 2500, 7000, 40);
      v.nextEvent = t + (uint32_t)(ms(14) - ms(9) * p);
      return true;
    }
    set(o, f, 26, 1200);
    clampTo(o, t, v.nextEvent < kWhirUs ? v.nextEvent : kWhirUs);
    return true;
  }
  if (t < kSnapAt) {  // coasting to a stop
    const float p = (t - kWhirUs) / (float)(kSnapAt - kWhirUs);
    const float top = v.from > 0 ? 2600 : 1100;
    set(o, top * (1 - 0.4f * p), 14, 1500);
    clampTo(o, t, kSnapAt);
    return true;
  }
  // The lens carrier snapping home, with a little rebound.
  const uint32_t u = t - kSnapAt;
  if (u < 2000) hiss(o, 2500, 7500, 150), clampTo(o, u, 2000);
  else if (u < 7000) set(o, 130, 180, 7000 - u);
  else if (u < 22000) silence(o, 22000 - u);
  else if (u < 23500) set(o, 330, 60, 23500 - u);
  else if (u < ms(40)) silence(o, ms(40) - u);
  else return false;
  return true;
}

// Crackle: random pops on top of a hum. `rate` pops a second; returns true
// and fills `o` while a pop is sounding.
bool crackle(Voice& v, uint32_t t, float ratePerSec, float loud, Out& o) {
  if (t >= v.eventEnd && t >= v.nextEvent) {
    v.eventEnd = t + 400 + rnd() % 2600;
    // Now and then a pop comes in a pair.
    const float mean = ratePerSec > 0.1f ? 1e6f / ratePerSec : 1e7f;
    v.nextEvent = rnd() % 5 == 0 ? v.eventEnd + 2000 + rnd() % 6000 : v.eventEnd + poissonGapUs(mean);
  }
  if (t < v.eventEnd) {
    hiss(o, 1200, 7000, loud * (0.5f + (rnd() % 100) / 100.0f));
    clampTo(o, t, v.eventEnd);
    return true;
  }
  return false;
}

void hum(Out& o, uint32_t t, float freq, float duty, const Voice& v) {
  set(o, freq, duty, 3000);
  const uint32_t until = v.nextEvent > t ? v.nextEvent : t + 3000;
  clampTo(o, t, until);
}

constexpr uint32_t kHumUpUs = ms(1000);
constexpr uint32_t kSweepUs = ms(500);
constexpr uint32_t kLockUs = ms(80);

bool warmUpStep(Voice& v, uint32_t t, Out& o) {
  if (t < 3000) return set(o, 110, 190, 3000 - t), true;  // the power switch
  if (t < 6000) return hiss(o, 1500, 6000, 80), clampTo(o, t, 6000), true;
  if (t < kHumUpUs) {
    // The valves warming: the hum swells and settles in pitch; the crackle
    // builds, peaks, then calms.
    const float p = (t - 6000) / (float)(kHumUpUs - 6000);
    if (crackle(v, t, 3 + 30 * sinf(3.1416f * p), 90, o)) return true;
    hum(o, t, 100 + 20 * p, 2 + 16 * powf(p, 1.5f), v);
    clampTo(o, t, kHumUpUs);
    return true;
  }
  if (t < kHumUpUs + kSweepUs) {
    // The dial sweeping down the band, whistling through static, overshooting
    // and coming back onto the station.
    const float q = (t - kHumUpUs) / (float)kSweepUs;
    float f = q < 0.7f ? 3800 - 3150 * (q / 0.7f) : 650 + 750 * ((q - 0.7f) / 0.3f);
    f *= 1 + 0.02f * sinf(kTau * 25 * secs(t));
    if (rnd() % 100 < (uint32_t)(45 * (1 - q))) hiss(o, 800, 6000, 30);
    else set(o, f, 22, 1800);
    clampTo(o, t, kHumUpUs + kSweepUs);
    return true;
  }
  if (t < kHumUpUs + kSweepUs + kLockUs) {  // locked on
    const float p = (t - kHumUpUs - kSweepUs) / (float)kLockUs;
    set(o, 1400, 22 - 16 * p, 2000);
    clampTo(o, t, kHumUpUs + kSweepUs + kLockUs);
    return true;
  }
  return false;
}

float stationHz(int pos, int count) {
  if (count < 2) return 1200;
  return 700 * powf(2600.0f / 700.0f, clamp01(pos / (float)(count - 1)));
}

constexpr uint32_t kScanUs = ms(140);
constexpr uint32_t kSettleUs = ms(30);

bool tunerStep(Voice& v, uint32_t t, Out& o) {
  const float f0 = (float)v.from, f1 = (float)v.to;
  if (t < kScanUs) {
    // Between stations it is mostly static; near one, the whistle wins.
    const float q = t / (float)kScanUs;
    const float f = (f0 + (f1 - f0) * smooth(q)) * (1 + 0.03f * sinf(kTau * 30 * secs(t)));
    if (rnd() % 100 < (uint32_t)(10 + 50 * sinf(3.1416f * q))) hiss(o, 700, 6500, 28);
    else set(o, f, 20, 1500);
    clampTo(o, t, kScanUs);
    return true;
  }
  if (t < kScanUs + kSettleUs) {
    const float p = (t - kScanUs) / (float)kSettleUs;
    set(o, f1, 16 * (1 - p), 1500);
    clampTo(o, t, kScanUs + kSettleUs);
    return true;
  }
  return false;
}

bool ambientStep(Voice& v, uint32_t t, Out& o) {
  // Fade towards wanted. Pauses under other sounds look like a big jump in
  // t, so the fade only counts a few milliseconds of it.
  uint32_t dt = t > v.lastT ? t - v.lastT : 0;
  if (dt > ms(20)) dt = ms(20);
  v.lastT = t;
  if (v.on) v.level = clamp01(v.level + dt / (float)ms(400));
  else v.level = clamp01(v.level - dt / (float)ms(600));
  if (!v.on && v.level <= 0) return false;
  if (v.nextEvent + ms(500) < t) v.nextEvent = t + poissonGapUs(ms(280));  // after a pause
  if (crackle(v, t, 3.5f, 70 * v.level, o)) return true;
  const float duty = 1 + 8 * v.level;
  hum(o, t, 120 + 2 * sinf(kTau * 0.3f * secs(t)), duty, v);
  return true;
}

}  // namespace

uint32_t rnd() {
  // xorshift32: fast and plenty random for noise.
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

void bootBeeps(Voice& v) {
  v = Voice();
  v.step = bootStep;
}

void tabAdvance(Voice& v, int dir) {
  v = Voice();
  v.step = tabStep;
  v.from = dir < 0 ? -1 : 1;
  v.nextEvent = ms(6);
}

void settingsWarmUp(Voice& v) {
  v = Voice();
  v.step = warmUpStep;
  v.nextEvent = ms(120);
}

void tunerScan(Voice& v, int fromPos, int toPos, int count) {
  v = Voice();
  v.step = tunerStep;
  v.from = (int32_t)stationHz(fromPos, count);
  v.to = (int32_t)stationHz(toPos, count);
}

void ambient(Voice& v) {
  const float level = v.level;
  const bool on = v.on;
  v = Voice();
  v.step = ambientStep;
  v.level = level;
  v.on = on;
  v.nextEvent = ms(150);
}

}  // namespace gen
}  // namespace audio
