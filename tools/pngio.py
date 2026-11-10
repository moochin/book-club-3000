"""Minimal PNG reader/writer using only the standard library.

Enough for pixel art exported by Aseprite, LibreSprite, Piskel, GIMP and
friends: non-interlaced PNGs of any colour type, 8-bit (and 1/2/4-bit
indexed or grey). No Pillow needed, so the art build runs inside
PlatformIO's own Python.
"""
import struct
import zlib

_SIG = b"\x89PNG\r\n\x1a\n"


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    """Returns (width, height, rows) with rows[y][x] = (r, g, b, a)."""
    data = open(path, "rb").read()
    if data[:8] != _SIG:
        raise ValueError(f"{path}: not a PNG file")
    pos, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = interlace = None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if interlace:
        raise ValueError(f"{path}: interlaced PNGs are not supported; re-export without interlacing")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, channels * depth // 8)  # bytes per pixel, for filtering
    stride = (w * channels * depth + 7) // 8
    raw = zlib.decompress(idat)
    prev = bytearray(stride)
    rows = []
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 255
        prev = line
        # Unpack samples.
        if depth == 8:
            samples = list(line)
        elif depth == 16:
            samples = [line[i] for i in range(0, len(line), 2)]
        else:
            per = 8 // depth
            mask = (1 << depth) - 1
            samples = []
            for byte in line:
                for k in range(per):
                    samples.append((byte >> (8 - depth * (k + 1))) & mask)
            samples = samples[:w * channels]
        px = []
        for x in range(w):
            s = samples[x * channels:(x + 1) * channels]
            if ctype == 3:
                r, g, b = plte[s[0]]
                a = trns[s[0]] if trns and s[0] < len(trns) else 255
            elif ctype in (0, 4):
                v = s[0] * 255 // ((1 << depth) - 1) if depth < 8 else s[0]
                r = g = b = v
                a = s[1] if ctype == 4 else 255
            else:
                r, g, b = s[0], s[1], s[2]
                a = s[3] if ctype == 6 else 255
            px.append((r, g, b, a))
        rows.append(px)
    return w, h, rows


def write_png(path, w, h, rows):
    """rows[y][x] = (r, g, b) or (r, g, b, a). Writes 8-bit RGBA."""
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for p in row:
            raw.extend(p if len(p) == 4 else (*p, 255))

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(_SIG)
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def read_ppm(path):
    """Reads a binary P6 PPM (as written by the simulator)."""
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:  # magic, width, height, maxval
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1  # exactly one whitespace byte before the pixels
    w, h = int(fields[1]), int(fields[2])
    px = data[pos:pos + w * h * 3]
    return w, h, [[tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for x in range(w)] for y in range(h)]
