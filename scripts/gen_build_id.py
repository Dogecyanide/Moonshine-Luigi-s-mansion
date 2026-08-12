import argparse
import zlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()

    checksum = 0
    for path in args.inputs:
        checksum = zlib.crc32(Path(path).read_bytes(), checksum)
    content = (
        "#ifndef SUSAMUNE_BUILD_CHECKSUM\n"
        f'#define SUSAMUNE_BUILD_CHECKSUM "{checksum:08X}"\n'
        "#endif\n"
    )
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != content:
        output.write_text(content)


if __name__ == "__main__":
    main()
