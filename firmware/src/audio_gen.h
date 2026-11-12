// The machine noises, as step generators. Pure code with no hardware access,
// so the host can render them to WAV files (sim/sound_preview.cpp).
//
// A voice is a square wave. Each step sets its pitch, its pulse width (0..255,
// roughly the volume) and how long to hold them. Pitch 0 or width 0 is silence.
#pragma once

#include <stdint.h>

namespace audio {
namespace gen {

struct Out {
  uint16_t freq = 0;
  uint8_t duty = 0;
  uint32_t us = 1000;  // hold this long before the next step
};

struct Voice;
// Fills the next step, t microseconds after the sound started. False: done.
using StepFn = bool (*)(Voice& v, uint32_t t, Out& o);

struct Voice {
  StepFn step = nullptr;
  // Scratch state for the generator.
  uint32_t nextEvent = 0;  // next crackle / pop, in us
  uint32_t eventEnd = 0;
  int32_t from = 0, to = 0;
  float level = 0;         // ambient fade level, 0..1
  bool on = false;         // ambient: wanted (fades out when false)
  uint32_t lastT = 0;
};

void bootTape(Voice& v);
void tabAdvance(Voice& v, int dir);
void settingsWarmUp(Voice& v);
void tunerScan(Voice& v, int fromPos, int toPos, int count);
// Starts the ambient hum, fading in from the voice's current level.
void ambient(Voice& v);

// Steps shorter than this are not worth a timer round trip.
constexpr uint32_t kMinStepUs = 150;

uint32_t rnd();

}  // namespace gen
}  // namespace audio
