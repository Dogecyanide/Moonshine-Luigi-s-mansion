import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from util.dol_c_kit.cw_map_to_ldscript import main as map_to_ld


def main():
    ap = argparse.ArgumentParser(description="Generate a linker script from a CodeWarrior map file.")
    ap.add_argument("map", help="Input .map file")
    ap.add_argument("ld", help="Output .ld file")
    args = ap.parse_args()
    map_to_ld(args.map, args.ld)


if __name__ == "__main__":
    main()
