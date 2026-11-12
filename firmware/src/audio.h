// Sound on GPIO 26 (through the PAM8002A amp). By default everything is
// generated in code: Geiger ticks, knob clicks, a short jingle, and the
// machine noises (cassette-loading boot, microfiche tab advance, valve-radio
// settings). A recorded clip in art/sounds/ replaces the generated sound for
// its slot; clips play as 8-bit audio through the pin's DAC (see audio.cpp).
// Timing runs off esp_timer, so a slow screen push never stretches a 2 ms
// tick into a beep. The pin is held low whenever nothing is playing, to keep
// the amp quiet.
//
// There is one voice: a square wave whose pitch and pulse width change from
// step to step. Short random pitch hops make static, crackle and relay
// clatter. A background "ambient" layer (the settings hum) plays whenever
// nothing in the foreground is, and resumes after it.
#pragma once

#include <stdint.h>

namespace audio {

void begin();
void tick(uint32_t nowMs);

// Muted by the sound setting or quiet hours. Muting stops anything playing.
void setEnabled(bool on);
// Volume step 0..3 (25/50/75/100 % of the safe maximum in config.h).
void setVolume(uint8_t step);
void setQuietHours(bool quiet);
bool canPlay();
// True while a one-shot sound (a clip or a generated tune) is playing; the
// settings hum doesn't count. The main loop holds animation frames meanwhile.
bool playing();

// Average Geiger clicks per minute (0 = off). Random (Poisson) spacing.
void setGeigerRate(int clicksPerMin);

void knobClick();
void geigerClick();
void jingle();      // meeting day fanfare, ~1.5 s
void birthday();    // on this unit owner's birthday (the jingle unless a clip is given)
void confirmBeep();
void nudgeChirp();  // an incoming nudge: a quick "bip-bip-BWEEP"

// Power-on: relay clicks, tape motor spooling up, the squeal of a program
// loading from cassette, then the motor spooling down. About 3 s; matches
// the start-up screen.
void bootTape();

// Changing tabs: a microfiche reader's film whirs past, then the lens
// carrier snaps into place. dir < 0 whirs the other way. About 0.25 s.
void tabAdvance(int dir);

// Opening settings: the power switch, valves warming up into a hum with
// crackle, and the tuning dial sweeping onto a station. About 1.5 s.
void settingsWarmUp();

// Moving through the settings list: the tuning dial scans from one
// "station" to the next, whistling through static. pos/count place the
// station on the dial.
void tunerScan(int fromPos, int toPos, int count);

// Leaving settings, a nudge delivered, a nudge that failed, and club data
// fetched. Silent unless art/sounds has a clip for them.
void settingsClose();
void nudgeSent();
void errorBuzz();
void newData();

// The valve hum with the odd crackle, while settings are open. It fades in
// and out, and pauses under other sounds.
void setAmbient(bool on);

}  // namespace audio
