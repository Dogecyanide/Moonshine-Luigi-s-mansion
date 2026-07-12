import argparse
import os
import sys
import zipfile
import subprocess
from pathlib import Path

LAUNCHER_DIR = Path(__file__).resolve().parent.parent / "launcher"
BUITLIN_TOOLCHAIN_DIR = LAUNCHER_DIR / "nintendont_devkitpro"
BUITLIN_TOOLCHAIN_ZIP = LAUNCHER_DIR / "nintendont_devkitpro_win32.zip"
DEVKITPPC = Path(os.environ.get("DEVKITPPC", BUITLIN_TOOLCHAIN_DIR / "devkitppc")).resolve()
DEVKITARM = Path(os.environ.get("DEVKITARM", BUITLIN_TOOLCHAIN_DIR / "devkitarm")).resolve()

NEED_TOOLCHAIN_UNZIP = ((not os.path.exists(BUITLIN_TOOLCHAIN_DIR)) and 
    ((os.environ.get("DEVKITPPC") is None) or (os.environ.get("DEVKITARM") is None)))

def main():
    ap = argparse.ArgumentParser(description="Build custom nintendont launcher.")
    args = ap.parse_args()

    if NEED_TOOLCHAIN_UNZIP:
        with zipfile.ZipFile(BUITLIN_TOOLCHAIN_ZIP, "r") as z:
            z.extractall(LAUNCHER_DIR)

    print(f"DEVKITPPC: {DEVKITPPC}")
    print(f"DEVKITARM: {DEVKITARM}")

    # TODO: generate header file from patches.py

    subprocess.run(["make", "-C", LAUNCHER_DIR], env = {"DEVKITPPC": str(DEVKITPPC), "DEVKITARM": str(DEVKITARM)})
    
if __name__ == "__main__":
    main()
