# Recorded sounds

Drop a sound file in this folder, named after its slot, and the next firmware
build plays it in place of the generated sound. Remove the file to go back to
the generated sound. Slots with no generated sound stay silent until they have
a file.

## Names

| File name | Plays when | Length | Without a file |
| --- | --- | --- | --- |
| `boot` | the unit starts, under the boot screen | up to ~3 s | cassette-loading noises |
| `tab_next` | you change tab (turn right, or tap a tab) | 0.1–0.4 s | microfiche whir and snap |
| `tab_prev` | you change tab backwards | 0.1–0.4 s | `tab_next`, else the whir |
| `settings_open` | settings open | 0.5–2 s | valves warming, dial tuning in |
| `settings_hum` | **loops** while settings are open, under the other sounds | 1–4 s, seamless loop | valve hum with crackle |
| `settings_scroll` | you move through the settings list | 0.1–0.3 s | the dial scanning |
| `settings_close` | settings close | 0.2–1 s | silent |
| `knob` | any other knob turn or tap | under 50 ms | a click |
| `confirm` | a setting changes (e.g. sound switched on) | 0.1–0.3 s | two-note beep |
| `nudge` | a nudge arrives | 0.5–1.5 s | "bip-bip-BWEEP" |
| `nudge_sent` | your nudge was delivered | 0.3–1 s | silent |
| `error` | a nudge failed to send | 0.3–1 s | silent |
| `sync` | the club data is fetched (at start-up, daily, and on REFRESH NOW) | 0.3–1.5 s | silent |
| `meeting_day` | once on meeting day | 1–3 s | a fanfare |
| `birthday` | once on your own birthday | 1–3 s | `meeting_day`, else the fanfare |
| `geiger` | each Geiger counter tick (more often as the meeting nears) | under 10 ms | a random-pitch tick |

For example: `art/sounds/boot.wav`, `art/sounds/nudge.mp3`. Names are lower
case. A file with any other name is ignored, with a note in the build output.

## Format

- **WAV** (PCM, any sample rate, 8/16/24/32-bit, mono or stereo) works as-is.
- **MP3, OGG, FLAC, M4A** need [ffmpeg](https://ffmpeg.org) on the computer
  doing the build. GitHub's build machines have it.

The build mixes each file to mono, resamples it to 11 kHz 8-bit, trims silence
from the start and end (not for `settings_hum`), evens out the volume, and
adds a 4 ms fade so it doesn't click. Anything above about 5 kHz is lost, and
the tiny speaker has little below 300 Hz, so mid-range sounds come through
best. Each sound is cut at 8 s, and all of them together must fit in 400 KB
(about 36 s in total).

## Tips

- **Short UI sounds feel better**: under 0.3 s for tabs, scrolling and
  the knob, or turning the knob quickly will cut each one off.
- **The hum must loop cleanly**: cut it at zero crossings, or it ticks
  once per loop.
- **Volume**: every file is normalised to the same peak. The overall level
  of recorded sounds is `kClipVolume` in `firmware/src/config.h`.
- **Licences**: this repository is public. Use sounds you made, or ones whose
  licence allows redistribution (CC0 is simplest; CC-BY needs a credit,
  so add it below).

## Credits

(none yet)
