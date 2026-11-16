"""Build and flash a CYD whose auto-reset can't enter download mode.

Some CYD clones (like the USB-C ST7789 board) can't pull GPIO0 low, so
`pio run -t upload` fails with "Wrong boot mode (0x13)" unless BOOT is held
for the whole connect, and esptool's own reset drops the chip back to normal
boot if BOOT is released early. This script instead:

  1. builds the env (skip with --no-build),
  2. finds the CH340 by USB id 1A86:7523 (the COM number changes),
  3. pulses RTS to reset the chip every ~0.6 s until the ROM boot log says
     DOWNLOAD -- hold BOOT for ~2 s while it waits,
  4. runs esptool with --before no_reset to write the bootloader, partition
     table, boot_app0 and firmware.

Run it with PlatformIO's Python, which has pyserial and esptool:

    ~/.platformio/penv/Scripts/python scripts/flash_cyd.py            # cyd_st7789
    ~/.platformio/penv/Scripts/python scripts/flash_cyd.py cyd        # ILI9341
"""
import argparse
import os
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

FIRMWARE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_HOME = os.environ.get("PLATFORMIO_CORE_DIR", os.path.expanduser("~/.platformio"))
BOOT_APP0 = os.path.join(PIO_HOME, "packages", "framework-arduinoespressif32",
                         "tools", "partitions", "boot_app0.bin")
ESPTOOL = os.path.join(PIO_HOME, "packages", "tool-esptoolpy", "esptool.py")
CH340 = (0x1A86, 0x7523)
BAUD_LOG, BAUD_FLASH = 115200, 460800


PORT = None  # --port: flash this one instead of the first CH340


def find_port():
    if PORT:
        return PORT if any(p.device == PORT for p in list_ports.comports()) else None
    for p in list_ports.comports():
        if (p.vid, p.pid) == CH340:
            return p.device
    return None


def wait_for_port(deadline):
    while time.time() < deadline:
        port = find_port()
        if port:
            return port
        time.sleep(1)
    sys.exit(f"{PORT or 'No CH340 port'} not found (is the board plugged in?)")


def wait_for_download_mode(port, deadline):
    """Reset the chip repeatedly until its boot log says DOWNLOAD."""
    while time.time() < deadline:
        try:
            with serial.Serial(port, BAUD_LOG, timeout=0.1) as s:
                s.dtr = False
                s.rts = True   # EN low: hold in reset
                time.sleep(0.1)
                s.rts = False  # release
                buf = b""
                t0 = time.time()
                while time.time() - t0 < 0.5:
                    buf += s.read(256)
                if b"DOWNLOAD" in buf:
                    return port
        except serial.SerialException as e:
            print("Serial error, retrying:", e, flush=True)
            time.sleep(1)
            port = find_port() or port
    sys.exit("Timed out waiting for download mode (was BOOT held?)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("env", nargs="?", default="cyd_st7789",
                    help="PlatformIO env to flash (default: cyd_st7789)")
    ap.add_argument("--no-build", action="store_true", help="flash the existing build")
    ap.add_argument("--port", help="serial port, e.g. COM10 (default: the first CH340)")
    ap.add_argument("--timeout", type=float, default=90,
                    help="seconds to wait for BOOT (default: 90)")
    args = ap.parse_args()
    global PORT
    PORT = args.port

    if not args.no_build:
        subprocess.check_call([sys.executable, "-m", "platformio", "run", "-e", args.env],
                              cwd=FIRMWARE_DIR)

    build = os.path.join(FIRMWARE_DIR, ".pio", "build", args.env)
    images = [("0x1000", os.path.join(build, "bootloader.bin")),
              ("0x8000", os.path.join(build, "partitions.bin")),
              ("0xe000", BOOT_APP0),
              ("0x10000", os.path.join(build, "firmware.bin"))]
    for _, path in images:
        if not os.path.exists(path):
            sys.exit(f"Missing {path}")

    deadline = time.time() + args.timeout
    port = wait_for_port(deadline)
    print(f"Port: {port}. Hold BOOT now.", flush=True)
    port = wait_for_download_mode(port, deadline)
    print("Chip is in download mode; flashing.", flush=True)

    cmd = [sys.executable, ESPTOOL, "--chip", "esp32", "--port", port,
           "--baud", str(BAUD_FLASH), "--before", "no_reset", "--after", "hard_reset",
           "write_flash", "-z"]
    for addr, path in images:
        cmd += [addr, path]
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
