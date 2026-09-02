"""Extract and identify main.dol from a GameCube disc image."""

import argparse
import hashlib
import struct
from pathlib import Path


DISC_HEADER_SIZE = 0x440
DOL_HEADER_SIZE = 0x100
DOL_OFFSET_FIELD = 0x420


def dol_size(header: bytes) -> int:
    if len(header) != DOL_HEADER_SIZE:
        raise ValueError("truncated DOL header")

    text_offsets = struct.unpack_from(">7I", header, 0x00)
    data_offsets = struct.unpack_from(">11I", header, 0x1C)
    text_sizes = struct.unpack_from(">7I", header, 0x90)
    data_sizes = struct.unpack_from(">11I", header, 0xAC)
    ends = [DOL_HEADER_SIZE]
    ends.extend(offset + size for offset, size in zip(text_offsets, text_sizes))
    ends.extend(offset + size for offset, size in zip(data_offsets, data_sizes))
    return max(ends)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with args.iso.open("rb") as iso:
        disc_header = iso.read(DISC_HEADER_SIZE)
        if len(disc_header) != DISC_HEADER_SIZE:
            raise ValueError("truncated GameCube disc header")

        game_id = disc_header[:6].decode("ascii", errors="replace")
        dol_offset = struct.unpack_from(">I", disc_header, DOL_OFFSET_FIELD)[0]
        iso.seek(dol_offset)
        header = iso.read(DOL_HEADER_SIZE)
        size = dol_size(header)
        iso.seek(dol_offset)
        payload = iso.read(size)
        if len(payload) != size:
            raise ValueError("main.dol extends beyond the end of the image")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    sha1 = hashlib.sha1(payload).hexdigest()
    print(f"game_id={game_id}")
    print(f"disc_revision={disc_header[7]}")
    print(f"dol_offset=0x{dol_offset:08X}")
    print(f"dol_size=0x{size:08X}")
    print(f"main_dol_sha1={sha1}")


if __name__ == "__main__":
    main()
