// Renders the generated machine noises (src/audio_gen.cpp) to WAV files, so
// they can be heard without a unit. The speaker's square wave is
// synthesised at 44.1 kHz and gently low-passed, about as a small speaker
// would.
//
//   g++ -std=gnu++17 -O2 -Isrc sim/sound_preview.cpp src/audio_gen.cpp -o sim/out/sound_preview
//   sim/out/sound_preview art/preview/sounds
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "audio_gen.h"

using namespace audio::gen;

namespace {

constexpr int kRate = 44100;

struct Track {
  std::vector<float> s;
  double phase = 0;
  float lp = 0;

  // Plays one step of a square wave (or silence).
  void play(const Out& o) {
    const int n = (int)lround(o.us * 1e-6 * kRate);
    const double width = o.duty / 256.0;
    for (int i = 0; i < n; ++i) {
      float x = 0;
      if (o.freq && o.duty) {
        phase += o.freq / (double)kRate;
        phase -= floor(phase);
        x = phase < width ? 1.f : 0.f;
      }
      lp += 0.35f * (x - lp);
      s.push_back(lp);
    }
  }
  void rest(uint32_t us) { play(Out{0, 0, us}); }

  // Runs a voice to its end, or for at most maxUs.
  void voice(Voice& v, uint32_t maxUs = 60u * 1000 * 1000) {
    Out o;
    for (uint32_t t = 0; t < maxUs && v.step(v, t, o); t += o.us) play(o);
  }

  void save(const std::string& path) const {
    // Remove the DC offset of the 0/1 pulses, then normalise.
    std::vector<float> out(s.size());
    float dc = 0, peak = 1e-6f;
    for (size_t i = 0; i < s.size(); ++i) {
      dc += 0.0005f * (s[i] - dc);
      out[i] = s[i] - dc;
      peak = fmaxf(peak, fabsf(out[i]));
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
      perror(path.c_str());
      return;
    }
    const uint32_t bytes = (uint32_t)out.size() * 2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16), u16(1), u16(1), u32(kRate), u32(kRate * 2), u16(2), u16(16);
    fwrite("data", 1, 4, f);
    u32(bytes);
    for (float x : out) u16((uint16_t)(int16_t)lrintf(x / peak * 0.9f * 32767));
    fclose(f);
    printf("wrote %s (%.2f s)\n", path.c_str(), out.size() / (double)kRate);
  }
};

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  Voice v;

  {
    Track t;
    bootTape(v);
    t.voice(v);
    t.rest(300000);
    t.save(dir + "/boot.wav");
  }
  {
    Track t;  // three tabs forward, then one back
    for (int dir : {1, 1, 1, -1}) {
      tabAdvance(v, dir);
      t.voice(v);
      t.rest(450000);
    }
    t.save(dir + "/tabs.wav");
  }
  {
    // Open settings, sit in the hum, scroll down a few rows and back, then
    // close settings and let the hum fade.
    Track t;
    Voice amb;
    settingsWarmUp(v);
    t.voice(v);
    amb.level = 1;
    amb.on = true;
    ambient(amb);
    uint32_t ambT = 0;
    auto hum = [&](uint32_t us) {
      Out o;
      for (uint32_t end = ambT + us; ambT < end && amb.step(amb, ambT, o); ambT += o.us) t.play(o);
    };
    hum(2000000);
    int sel = 0;
    for (int to : {1, 2, 3, 4, 3}) {
      tunerScan(v, sel, to, 13);
      sel = to;
      t.voice(v);
      hum(600000);
    }
    hum(1500000);
    amb.on = false;
    hum(2000000);
    t.rest(200000);
    t.save(dir + "/settings.wav");
  }
  return 0;
}
