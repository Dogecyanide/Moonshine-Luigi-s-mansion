import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pyisotools.iso import GamecubeISO


def main():
    ap = argparse.ArgumentParser(description="Assemble a patched ISO from an extracted root and a patched main.dol.")
    ap.add_argument("--in-iso", required=True, help="Pristine extracted root (from extract_iso.py)")
    ap.add_argument("--out-iso", required=True, help="Working directory for the patched root")
    ap.add_argument("--dol", required=True, help="Patched main.dol to install")
    ap.add_argument("--output", required=True, help="Path of the ISO to write")
    args = ap.parse_args()

    in_root = Path(args.in_iso) / "root"
    out_root = Path(args.out_iso) / "root"

    if out_root.exists():
        shutil.rmtree(out_root)
    shutil.copytree(in_root, out_root)

    shutil.copyfile(args.dol, out_root / "sys" / "main.dol")

    # Region byte: mark the disc as the modded build.
    boot_bin = out_root / "sys" / "boot.bin"
    data = bytearray(boot_bin.read_bytes())
    data[0x05] = 0x32
    boot_bin.write_bytes(data)

    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    GamecubeISO.build_root(out_root, str(output))


if __name__ == "__main__":
    main()
