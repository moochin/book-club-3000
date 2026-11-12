// Sound on GPIO 26 (through the PAM8002A amp). Everything is generated in
// code: Geiger ticks, knob clicks and a short jingle. Timing runs off
// esp_timer, so a slow screen push never stretches a 2 ms tick into a beep.
// The pin is held low whenever nothing is playing, to keep the amp quiet.
#pragma once

#include <stdint.h>

namespace audio {

void begin();
void tick(uint32_t nowMs);

// Muted by the sound setting or quiet hours. Muting stops anything playing.
void setEnabled(bool on);
void setQuietHours(bool quiet);
bool canPlay();

// Average Geiger clicks per minute (0 = off). Random (Poisson) spacing.
void setGeigerRate(int clicksPerMin);

void knobClick();
void geigerClick();
void jingle();      // meeting day fanfare, ~1.5 s
void confirmBeep();
void nudgeChirp();  // an incoming nudge: a quick "bip-bip-BWEEP"

}  // namespace audio
