# Mascot art

The mascot lives on the STAT tab, in a 64 × 96 pixel box (about 18 × 27 mm on
the 2.8" screen). Draw each pose as a PNG in any pixel-art editor and drop it
in [`mascot/`](mascot/). The firmware build converts the PNGs automatically,
and any pose you haven't drawn yet keeps its labelled placeholder, so you can
do them one at a time.

## The poses

| File name | When it shows | Brief |
| --- | --- | --- |
| `level0` | 0–13 days since the last meeting | Well rested: thumbs up, grinning |
| `level1` | 14–89 days | Healthy: idle, the odd blink |
| `level2` | 90–136 days | Minor sickness: queasy, a bit off-colour |
| `level3` | 137–182 days | Advanced sickness: slumped, limbs flagging |
| `level4` | 183+ days | Critical: halfway to a ghoul |
| `final_week` | Next meeting is 1–7 days away | Excited |
| `meeting_day` | It's meeting day | Celebrating |
| `no_date` | No next date, while still healthy (levels 0–1) | Waiting for orders, a bit lost |
| `syncing` | Before the clock is set, after power-on | Anything patient: an hourglass is fine |

[`templates/`](templates/) has the current placeholder for every pose as a
PNG, already in the right size and colours, if you'd rather start from those
than from a blank canvas.

The character must be original. It must not look like Vault Boy or use
anything from Fallout (see the handover).

## Colours: one colour, 16 shades

The screen is a one-colour phosphor terminal, and each unit can be themed
green or amber. So art is drawn with a fixed 16-colour palette, and the
firmware recolours it for each theme:

- **0** background: treated as transparent (as are transparent pixels)
- **1–12** a dark-to-bright ramp of the theme colour: this is most of the art
- **13, 14** dim and bright warning colour (amber on green units, red-orange
  on amber units): for sickly patches, the antenna glow and so on
- **15** near-white highlight: eyes, glints, sparkle

Load [`palette.gpl`](palette.gpl) into your editor and draw only with those
colours. [`palette.png`](palette.png) shows them on green and amber. Pixels
in other colours are snapped to the nearest one at build time, with a
warning.

**Editors that load `.gpl` palettes:** Aseprite (paid, the best for
animation), LibreSprite (free Aseprite fork), Piskel (free, in the browser at
piskelapp.com), Lospec Pixel Editor (free, in the browser), and GIMP.

## Animation

A pose can have several frames: number them, and optionally add `@<ms>` to
set how long a frame stays up (the default is 500 ms). The frames loop.

```
level1_1@3000.png    eyes open for 3 s
level1_2@150.png     a quick blink
```

Two frames per pose is plenty: the screen redraws about 15 times a second.

## Seeing it without a board

**On GitHub:** commit the PNGs to `art/mascot/` (the web page's "Add file →
Upload files" works). The **Firmware** action builds the firmware with the
art in it and attaches a **previews** download to the run:
`screens.png` shows every STAT screen as the device draws it, and
`poses.png` shows each frame at 3× in both themes. The build fails with a
clear message if a file is the wrong size or has an unknown name.

**Locally** (needs g++, `libsdl2-dev` and PlatformIO):

```bash
art/preview.sh
```

The output is in `art/preview/`.

**On a unit:** flash new firmware (or release it over the air, see
`firmware/README.md`), then use the hidden preview menu (hold the knob 2 s →
ABOUT → press 5 times) to step through every pose.
