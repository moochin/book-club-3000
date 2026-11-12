# PlatformIO pre-build step: regenerate src/sound_data.h from ../art/sounds/.
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)
import os
import subprocess
import sys

script = os.path.join(env.subst("$PROJECT_DIR"), "..", "tools", "build_sounds.py")  # noqa: F821
subprocess.check_call([sys.executable, script])
