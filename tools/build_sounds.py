#!/usr/bin/env python3
"""Bakes the recorded sounds in art/sounds/ into firmware/src/sound_data.h.

Drop a file named after a slot (see SLOTS, or art/sounds/README.md) into
art/sounds/, as .wav (any rate, 8/16/24/32-bit PCM, mono or stereo) or
.mp3/.ogg/.flac/.m4a (needs ffmpeg on the PATH). Each is mixed to mono,
resampled to RATE, trimmed of leading and trailing silence (not loops),
normalised, given a short fade in and out (not loops) and stored as unsigned
8-bit samples. A slot with no file keeps the firmware's generated sound.

Runs before every firmware build (firmware/scripts/sounds.py).
"""
import array
import math
import random
import os
import shutil
import subprocess
import sys
import wave

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC_DIR = os.path.join(ROOT, "art", "sounds")
OUT = os.path.join(ROOT, "firmware", "src", "sound_data.h")

RATE = 11025          # samples per second on the unit
MAX_SECONDS = 8.0     # per sound
MAX_TOTAL = 400_000   # bytes of sound in the firmware, all together
FADE_MS = 4
SILENCE = 0.02        # trimmed below 2 % of full scale (about -34 dB)
EXTS = (".wav", ".mp3", ".ogg", ".flac", ".m4a", ".aac")

# The slot order is the firmware's: keep it in step with audio.cpp's Clip.
# name: (loops, what it is for)
SLOTS = [
    ("boot", False, "start-up, while the boot screen shows (about 3 s)"),
    ("tab_next", False, "changing tab, forwards"),
    ("tab_prev", False, "changing tab, backwards (else tab_next)"),
    ("settings_open", False, "opening settings"),
    ("settings_hum", True, "background loop while settings are open"),
    ("settings_scroll", False, "moving through the settings list"),
    ("settings_close", False, "leaving settings"),
    ("knob", False, "any other knob turn or tap"),
    ("confirm", False, "a setting changed, sound switched on"),
    ("nudge", False, "a nudge arrives"),
    ("nudge_sent", False, "your nudge was delivered"),
    ("error", False, "a nudge failed to send"),
    ("sync", False, "new club data arrived"),
    ("meeting_day", False, "once on meeting day"),
    ("birthday", False, "once on your birthday (else meeting_day)"),
    ("geiger", False, "one Geiger counter tick"),
]


def find(name):
    for ext in EXTS:
        p = os.path.join(SRC_DIR, name + ext)
        if os.path.exists(p):
            return p
    return None


def read_wav(path):
    """Returns (mono float samples in -1..1, sample rate)."""
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 1:
        vals = [(b - 128) / 128.0 for b in raw]
    elif width == 2:
        a = array.array("h", raw)
        if sys.byteorder == "big":
            a.byteswap()
        vals = [v / 32768.0 for v in a]
    elif width == 3:
        vals = [int.from_bytes(raw[i:i + 3], "little", signed=True) / 8388608.0
                for i in range(0, len(raw), 3)]
    elif width == 4:
        a = array.array("i", raw)
        if sys.byteorder == "big":
            a.byteswap()
        vals = [v / 2147483648.0 for v in a]
    else:
        raise ValueError(f"{path}: unsupported sample width {width}")
    if ch > 1:
        vals = [sum(vals[i:i + ch]) / ch for i in range(0, len(vals), ch)]
    return vals, rate


def decode(path):
    if path.lower().endswith(".wav"):
        try:
            return read_wav(path)
        except (wave.Error, ValueError):
            pass  # e.g. float or compressed WAV: let ffmpeg try
    ff = shutil.which("ffmpeg")
    if not ff:
        sys.exit(f"build_sounds: {os.path.relpath(path, ROOT)} needs ffmpeg to decode "
                 "(install it, or convert the file to a PCM .wav)")
    out = subprocess.run([ff, "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE),
                          "-f", "s16le", "-"], check=True, capture_output=True).stdout
    a = array.array("h", out)
    if sys.byteorder == "big":
        a.byteswap()
    return [v / 32768.0 for v in a], RATE


def lowpass(x, rate, cutoff):
    """Windowed-sinc FIR low-pass (Blackman window), so the top end above the
    unit's Nyquist frequency is removed rather than folding back as harsh
    aliasing (the old moving average let most of it through)."""
    fc = cutoff / rate
    taps = int(4 / fc) | 1  # ~4 cycles of the cutoff: a steep enough edge
    m = taps // 2
    h = []
    for i in range(taps):
        n = i - m
        sinc = 2 * fc if n == 0 else math.sin(2 * math.pi * fc * n) / (math.pi * n)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * i / (taps - 1)) + 0.08 * math.cos(4 * math.pi * i / (taps - 1))
        h.append(sinc * w)
    g = sum(h)
    h = [v / g for v in h]
    out = []
    for i in range(len(x)):
        acc = 0.0
        for j in range(taps):
            k = i + m - j
            if 0 <= k < len(x):
                acc += x[k] * h[j]
        out.append(acc)
    return out


def compress(x, rate, threshold=0.25, ratio=3.0):
    """Gentle compression: brings the body of spiky sounds up relative to
    their peaks, so they use more of the unit's few output levels."""
    att = math.exp(-1 / (rate * 0.002))   # 2 ms attack
    rel = math.exp(-1 / (rate * 0.060))   # 60 ms release
    env, out = 0.0, []
    for v in x:
        a = abs(v)
        env = att * env + (1 - att) * a if a > env else rel * env + (1 - rel) * a
        g = 1.0
        if env > threshold:
            g = (threshold * (env / threshold) ** (1 / ratio)) / env
        out.append(v * g)
    return out


def resample(x, rate):
    if not x:
        return x
    # Remove everything above ~0.45 of the output rate first.
    x = lowpass(x, rate, RATE * 0.45)
    if rate == RATE:
        return x
    n = int(len(x) * RATE / rate)
    step = rate / RATE
    out = []
    for i in range(n):
        p = i * step
        j = int(p)
        f = p - j
        a = x[j]
        b = x[j + 1] if j + 1 < len(x) else a
        out.append(a + (b - a) * f)
    return out


def process(name, loops, path):
    x, rate = decode(path)
    x = resample(x, rate)
    if not x:
        sys.exit(f"build_sounds: {name}: no audio in {path}")
    mean = sum(x) / len(x)
    x = [v - mean for v in x]
    peak = max(abs(v) for v in x) or 1.0
    x = [v / peak for v in x]
    x = compress(x, RATE)
    peak = max(abs(v) for v in x) or 1.0
    x = [v / peak * 0.98 for v in x]
    if not loops:
        start = next((i for i, v in enumerate(x) if abs(v) > SILENCE), 0)
        end = len(x) - next((i for i, v in enumerate(reversed(x)) if abs(v) > SILENCE), 0)
        x = x[start:end]
        fade = min(len(x) // 2, RATE * FADE_MS // 1000)
        for i in range(fade):
            g = i / fade
            x[i] *= g
            x[-1 - i] *= g
    limit = int(MAX_SECONDS * RATE)
    if len(x) > limit:
        print(f"build_sounds: {name}: cut to {MAX_SECONDS:g} s")
        x = x[:limit]
    # TPDF dither: rounding straight to 8 bits turns quiet passages into gritty
    # distortion; a little triangular noise first leaves an even, faint hiss.
    # Seeded by name, so builds are repeatable.
    rnd = random.Random(name)
    return bytes(max(0, min(255, int(round(128 + v * 127 + rnd.random() - rnd.random())))) for v in x)


def main():
    os.makedirs(SRC_DIR, exist_ok=True)
    known = {n for n, _, _ in SLOTS}
    for f in sorted(os.listdir(SRC_DIR)):
        stem, ext = os.path.splitext(f)
        if ext.lower() in EXTS and stem not in known:
            print(f"build_sounds: ignoring art/sounds/{f} (not a slot name; see art/sounds/README.md)")

    clips, total = [], 0
    for name, loops, _ in SLOTS:
        path = find(name)
        data = process(name, loops, path) if path else b""
        clips.append((name, loops, data))
        total += len(data)
        if data:
            print(f"build_sounds: {name:16s} {len(data) / RATE:5.2f} s  {len(data):7d} bytes")
    if total > MAX_TOTAL:
        sys.exit(f"build_sounds: {total} bytes of sound is over the {MAX_TOTAL}-byte budget; "
                 "use shorter clips")

    lines = [
        "// Generated by tools/build_sounds.py from art/sounds/. Do not edit.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace sounddata {",
        "",
        f"constexpr uint32_t kRate = {RATE};",
        "",
        "struct Clip {",
        "  const uint8_t* data;  // unsigned 8-bit, 128 = silence; null: no file",
        "  uint32_t len;",
        "  bool loops;",
        "};",
        "",
    ]
    for name, _, data in clips:
        if not data:
            continue
        lines.append(f"static const uint8_t k_{name}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        lines.append("};")
    lines.append("")
    lines.append(f"constexpr int kClipCount = {len(clips)};")
    lines.append("static const Clip kClips[kClipCount] = {")
    for name, loops, data in clips:
        ref = f"k_{name}, {len(data)}" if data else "nullptr, 0"
        lines.append(f"  {{{ref}, {'true' if loops else 'false'}}},  // {name}")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace sounddata")
    text = "\n".join(lines) + "\n"
    old = open(OUT).read() if os.path.exists(OUT) else None
    if text != old:  # leave it alone when unchanged, so nothing rebuilds
        with open(OUT, "w") as f:
            f.write(text)
    if total == 0:
        print("build_sounds: no recorded sounds; using the generated ones")


if __name__ == "__main__":
    main()
