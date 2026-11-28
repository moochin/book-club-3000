# Book-Club 3000: case files

3D-printable case for the Book-Club 3000 desk terminal, built around an ESP32-2432S028R ("Cheap Yellow Display", USB-C version).

## Which folder to print from

| Folder | What's in it |
|---|---|
| `stl/dial-right/` | Case parts with the dial on the right, seen from the front (as in the renders). **Default.** |
| `stl/dial-left/` | The same case parts mirrored, with the dial on the left. |
| `stl/common/` | Parts that are the same for both versions: the knob and the badges. |
| `stl/test-prints/` | Quick prints for checking fit before printing a full shell. |
| `source/terminal_v3.py` | The parametric script that makes every STL (Python, `manifold3d`, `trimesh`, `numpy`). |

## Parts

| File | Notes |
|---|---|
| `v3_front_shell.stl` | Print face down. Flat face, so no supports. |
| `v3_rear_housing.stl` | Print rear face down. It's modelled ready to turn over onto the shell, so the board pegs line up. |
| `v3_screen_bezel.stl` | Glue-on. Locates with 1.75 mm filament dowels. |
| `v3_dial_drum.stl` | Glue-on ring around the dial. |
| `v3_stand.stl` | Desk stand, 15° tilt, with a tray at the front for the stylus. |
| `v3_knob.stl` | For a 6 mm D-shaft EC11 encoder. |
| `v3_logo_badge.stl` | Single-colour badge. |
| `v3_badge_2c_single_file.stl` | Two-colour badge as one file: black base, swap to yellow at 0.8 mm, back to black at 1.4 mm. |
| `v3_badge_2c_black.stl`, `v3_badge_2c_yellow.stl` | The same two-colour badge as separate bodies, for multi-material printers. |
| `test-prints/v3_window_check.stl` | The 2 mm face only, to check the screen window against the board. |
| `test-prints/v3_fit_test_slice.stl` | A slice of the shell, to check board fit and pegs. |

## Hardware

- 4 × M3 × 12 pan- or button-head screws to join the rear housing to the shell. They self-tap into the shell, so don't overtighten. M2 screws won't grip.
- Optional: M3 × 4 button-head screws in the three decorative holes on the face.
- 1.75 mm filament offcuts as dowels for the bezel, drum and badge.
- EC11 rotary encoder with push switch, 10 k resistor, and a 40 mm, 32 Ω, 0.5 W speaker.

## Regenerating

```
pip install manifold3d trimesh numpy
python3 source/terminal_v3.py
```

Set `DIAL_ON_RIGHT` and `OUT_DIR` near the bottom of the script. The badge text needs the Wide Glide font at `source/fonts/wideglide.ttf`, which isn't included here; add your own copy or point `BADGE_FONT` at any `.ttf`.
