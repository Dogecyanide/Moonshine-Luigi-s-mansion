import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pyisotools.iso import GamecubeISO


def main():
    ap = argparse.ArgumentParser(description="Extract a GameCube ISO into a Dolphin-style root directory.")
    ap.add_argument("iso", help="Input .iso file")
    ap.add_argument("out_dir", help="Directory to extract into")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    GamecubeISO.extract_from(Path(args.iso), out_dir)


if __name__ == "__main__":
    main()
