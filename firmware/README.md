# Book-Club 3000 firmware

PlatformIO / Arduino-ESP32 firmware for the ESP32-2432S028R ("Cheap Yellow
Display"). One binary serves every unit; per-unit details are entered on the
Wi-Fi setup page and kept in NVS. The spec is [`docs/HANDOVER.md`](../docs/HANDOVER.md).

```bash
pip install platformio
```

```bash
pio test -e native                  # state-engine unit tests on the PC
```

```bash
pio run -e cyd -t upload            # build and flash over USB (ILI9341 boards)
```

```bash
pio run -e cyd_st7789 -t upload     # the same for ST7789 boards
```

```bash
pio device monitor                  # serial log, 115200
```

If the upload stops at `Wrong boot mode detected (0x13)`, the board's
auto-reset can't pull GPIO0 low (some CYD clones are wired this way): hold
**BOOT** while the upload says `Connecting...`, and let go once it starts
writing.

| Environment | What it is |
| --- | --- |
| `cyd` | The firmware, ILI9341 panel |
| `cyd_st7789` | The firmware, ST7789 panel (many USB-C boards) |
| `cyd_bringup` | Hardware check: test pattern, knob, backlight, speaker |
| `cyd_ota` | `cyd`, uploaded over the LAN with ArduinoOTA |
| `native` | Host unit tests for `lib/bookclub_state` |

## Bring-up (build plan step 1)

Do this once on the first board, then record the answers here.

```bash
pio run -e cyd_bringup -t upload && pio device monitor
```

1. **Driver and inversion.** You should see red, green, blue and white bars,
   then the two theme colours. Garbled or blank: switch to the ST7789 build
   (edit `-DBC_PANEL_ILI9341` to `-DBC_PANEL_ST7789` in the `cyd_bringup`
   env). Colours like a photo negative: **hold the knob 2 s** to flip
   inversion live, then set `BC_PANEL_INVERT` to match. Red and blue swapped:
   set `rgb_order = true` in `src/display.cpp`.
2. **Rotation.** Put the board in the case (USB-C on the left, JST plugs at
   the top) and **turn the knob** until the text reads upright. Set
   `kDefaultRotation` in `src/config.h` (each unit can also override it on
   the setup page).
3. **Knob.** Each detent should log one `turn +1` or `-1` (and change the
   rotation once). If it takes two detents per step, set
   `kEncCountsPerDetent` to 2. Backwards: swap `kPinEncA` and `kPinEncB`.
   A press logs `short press`; if nothing happens, check the 10k pull-up on
   GPIO 35.
4. **Backlight and speaker.** Each press steps the backlight 25/50/100 % and
   beeps.
5. **Light sensor.** The serial log prints `ldr=` twice a second. Note the
   reading in a bright room and in a dark one and set `kLdrBright`,
   `kLdrDark` and `kLdrDarkRoom` in `src/config.h` (and flip
   `kLdrBrightIsLow` if the reading rises with light).

**Confirmed on hardware:** partly, on a USB-C "TPM408-2.8" board (2026-11-11).
Flash it with the `cyd_st7789` env.

| Setting | Value |
| --- | --- |
| Panel driver | ST7789 |
| `BC_PANEL_INVERT` | 0 (`rgb_order` false) |
| Rotation in the case | ? |
| Encoder counts per detent | ? |
| LDR bright / dark readings | ? / ? |

## How it fits together

```
src/net        Wi-Fi: WiFiManager portal on first boot, background reconnect
src/clock      NTP + Europe/London POSIX TZ ("GMT0BST,M3.5.0/1,M10.5.0")
src/sync       HTTPS fetch of bookclub.json in its own FreeRTOS task; NVS cache;
               pull-style firmware updates
src/settings   per-unit settings and the cached file, in NVS (Preferences)
lib/bookclub_state   PURE: JSON parsing + computeState(data, now) -> what to show
src/ui         draws the model it is given; turns knob events into actions
src/mascot     draws ../art/mascot PNGs (baked into src/art_data.h by
               scripts/art.py before each build); placeholders for the rest
src/display    LovyanGFX panel + one 320x240 4-bit palette canvas, scanlines
src/input      EC11 on the hardware pulse counter, debounced push switch
src/audio      generated Geiger ticks, knob clicks, meeting-day jingle
src/backlight  auto-dimming from the LDR, 10-minute dark-room blanking
src/ota        ArduinoOTA for LAN flashing
src/main.cpp   the non-blocking loop that ticks each module
```

The state engine alone decides what the screen shows. `computeState()` takes
the parsed file and the local date/time and returns the exposure level, bar,
quest state, countdown and mascot pose; it has no Arduino headers, so
`pio test -e native` covers every row of the handover's test table plus
boundaries, parsing and DST. Thresholds live in one table, `bc::Thresholds`
in `lib/bookclub_state/src/bc_state.h`.

**Reliability rules the code follows:**

- A fetched file that fails to parse or has no valid `last.date` is
  rejected; the cached copy stays on screen. Strings are copied into
  fixed-size buffers, the file is capped at 12 KB, and nothing on the parse
  path can crash the unit.
- No `delay()` in the main loop. HTTPS runs on core 0 in its own task, so
  the knob stays responsive during a slow fetch.
- Fetch on boot, then every 24 h plus up to 30 min of random delay; after a
  failure retry at 2, 10, 30 and then 60 minutes.
- Before the first NTP sync the screen says **SYNCING CLOCK** rather than
  showing wrong numbers.
- If the saved network is down the unit keeps running on cached data and
  retries every minute. The setup portal only opens from the menu
  (**Wi-Fi reset**), and the old network is kept until a new one is saved.

## The UI without a board

`sim/` builds the real `ui.cpp`, `mascot.cpp`, `display.cpp` and state engine
for the PC (LovyanGFX's SDL back end), drives them with knob events and saves
every screen as an image. Use it to check layout changes; `art/preview.sh`
wraps it for checking mascot art.

```bash
sudo apt-get install libsdl2-dev && pio pkg install -e cyd
```

```bash
./sim/build.sh && ./sim/out/sim sim/out
```

On the device, **hold 2 s → ABOUT → press 5 times quickly** opens the hidden
**PREVIEW STATES** menu, which shows every exposure level and quest state
from a built-in sample file. A banner shows while a preview is active; pick
**LIVE DATA** to leave.

## Firmware updates

**Over the LAN** (Pete's own network): `pio run -e cyd_ota -t upload
--upload-port bookclub-<owner>.local`. If the unit has an OTA password, add
`upload_flags = --auth=...` in your local copy of the env.

**On friends' networks** (no cable, no LAN access): units update themselves
from a GitHub Release, triggered by the shared file.

1. Bump `BC_FW_VERSION` in `src/config.h`, commit, and push a tag `vX.Y.Z`
   matching it. The **Firmware** action tests, builds and publishes
   `firmware.bin` (ILI9341) and `firmware-st7789.bin` to a release.
2. Add a `firmware` block to `bookclub.json`:

   ```json
   "firmware": {
     "version": "1.1.0",
     "url": "https://github.com/moochin/book-club-3000/releases/download/v1.1.0/firmware.bin",
     "url_st7789": "https://github.com/moochin/book-club-3000/releases/download/v1.1.0/firmware-st7789.bin"
   }
   ```

3. On their next sync, units running an older version download it, show
   **INSTALLING UPDATE**, and reboot. A failed update is retried at the next
   sync; a unit never downgrades. Older firmware ignores the block.

ILI9341 builds use `url`; ST7789 builds use only `url_st7789` and skip the
update if it is missing, so a unit is never flashed with the other panel's
binary. List only the one your fleet uses if they are all the same.

The partition table (`partitions.csv`) has two 1.9 MB app slots for OTA and a
64 KB NVS, which holds settings, saved Wi-Fi and the cached file.

## Where the data comes from

There is no built-in data URL: the club file is in a secret gist, and its
link must never be committed to this public repo. Each unit is given the
gist's Raw link (`https://gist.githubusercontent.com/<user>/<id>/raw/bookclub.json`,
without a commit hash) on the setup page and keeps it in NVS. Until it has
one, the unit shows **no data URL: set it on the setup page**.

The setup page also takes an optional GitHub token, sent as
`Authorization: Bearer`, for a data file in a private repo. The gist doesn't
need one.

## Licences and IP

The look is inspired by retro sci-fi terminals but contains no game assets.
The mascot is an original placeholder, the sounds are generated in code, and
the text uses the classic 5×7 GLCD bitmap font bundled with LovyanGFX
(BSD licence), stretched to a tall 6×16 cell. The boot-screen logo is a
rendering of "BOOK-CLUB 3000" in Wide Glide (Brain Stew Fonts, 1997), made by
`tools/build_logo.py`; the font file itself is not in the repo. Libraries: LovyanGFX (FreeBSD),
WiFiManager (MIT), ArduinoJson (MIT), ESP32Encoder (MIT).

## Not done yet

- **Hardware confirmation** of the bring-up table above.
- **Final mascot art** (build plan step 6): see [`art/README.md`](../art/README.md).
- **Case files** for `case/`.
