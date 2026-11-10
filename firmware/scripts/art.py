# PlatformIO pre-build step: regenerate src/art_data.h from ../art/mascot/*.png.
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)
import os
import subprocess
import sys

script = os.path.join(env.subst("$PROJECT_DIR"), "..", "tools", "build_art.py")  # noqa: F821
subprocess.check_call([sys.executable, script])
