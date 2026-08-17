#!/usr/bin/env python3
"""Inspect one file inside a Yaz0-compressed RARC stored in an SMS ISO."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def disc_files(iso: bytes) -> dict[str, tuple[int, int]]:
    fst_offset, fst_size = struct.unpack_from(">II", iso, 0x424)
    fst = iso[fst_offset : fst_offset + fst_size]
    count = be32(fst, 8)
    strings = fst[count * 12 :]
    stack: list[tuple[str, int]] = []
    files: dict[str, tuple[int, int]] = {}
    for index in range(1, count):
        name_type, offset, size = struct.unpack_from(">III", fst, index * 12)
        while stack and index >= stack[-1][1]:
            stack.pop()
        name_offset = name_type & 0xFFFFFF
        name_end = strings.index(0, name_offset)
        name = strings[name_offset:name_end].decode("ascii")
        path = "/".join([entry[0] for entry in stack] + [name])
        if name_type >> 24:
            stack.append((name, size))
        else:
            files[path] = (offset, size)
    return files


def decode_yaz0(source: bytes) -> bytes:
    if source[:4] != b"Yaz0" or source[8:16] != bytes(8):
        raise ValueError("archive is not canonical Yaz0")
    output_size = be32(source, 4)
    output = bytearray()
    cursor = 16
    code = 0
    mask = 0
    while len(output) < output_size:
        if not mask:
            code = source[cursor]
            cursor += 1
            mask = 0x80
        if code & mask:
            output.append(source[cursor])
            cursor += 1
        else:
            first, second = source[cursor : cursor + 2]
            cursor += 2
            distance = ((first & 0xF) << 8) | second
            length = first >> 4
            if not length:
                length = source[cursor] + 0x12
                cursor += 1
            else:
                length += 2
            copy = len(output) - distance - 1
            if copy < 0:
                raise ValueError("invalid Yaz0 back-reference")
            for _ in range(length):
                output.append(output[copy])
                copy += 1
        mask >>= 1
    if len(output) != output_size:
        raise ValueError("Yaz0 output overrun")
    return bytes(output)


def rarc_files(archive: bytes) -> dict[str, bytes]:
    if archive[:4] != b"RARC" or be32(archive, 8) != 0x20:
        raise ValueError("decoded archive is not RARC")
    data_base = 0x20 + be32(archive, 0x0C)
    node_count = be32(archive, 0x20)
    node_table = 0x20 + be32(archive, 0x24)
    file_count = be32(archive, 0x28)
    file_table = 0x20 + be32(archive, 0x2C)
    string_size = be32(archive, 0x30)
    strings_offset = 0x20 + be32(archive, 0x34)
    strings = archive[strings_offset : strings_offset + string_size]

    def string(offset: int) -> str:
        end = strings.index(0, offset)
        return strings[offset:end].decode("shift_jis", errors="replace")

    nodes: list[tuple[str, int, int]] = []
    for index in range(node_count):
        node = node_table + index * 0x10
        nodes.append((string(be32(archive, node + 4)),
                      struct.unpack_from(">H", archive, node + 0xA)[0],
                      be32(archive, node + 0xC)))

    result: dict[str, bytes] = {}
    visited: set[int] = set()

    def walk(node_index: int, prefix: str) -> None:
        if node_index in visited:
            return
        visited.add(node_index)
        _, count, first = nodes[node_index]
        for entry_index in range(first, first + count):
            if entry_index >= file_count:
                raise ValueError("RARC file-table overrun")
            entry = file_table + entry_index * 0x14
            flags_name = be32(archive, entry + 4)
            flags = flags_name >> 24
            name = string(flags_name & 0xFFFFFF)
            if name in (".", ".."):
                continue
            offset = be32(archive, entry + 8)
            size = be32(archive, entry + 0xC)
            path = f"{prefix}/{name}" if prefix else name
            if flags & 0x02:
                if offset < node_count:
                    walk(offset, path)
            else:
                result[path] = archive[data_base + offset : data_base + offset + size]

    walk(0, "")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("iso", type=Path)
    parser.add_argument("archive", help="disc path, for example data/scene/monte1.szs")
    parser.add_argument("asset", nargs="?", help="RARC suffix to inspect")
    parser.add_argument("--output", type=Path,
                        help="write the single matching asset")
    args = parser.parse_args()

    iso = args.iso.read_bytes()
    offset, size = disc_files(iso)[args.archive]
    files = rarc_files(decode_yaz0(iso[offset : offset + size]))
    matches = sorted((path, data) for path, data in files.items()
                     if args.asset is None or path.endswith(args.asset))
    for path, data in matches:
        print(f"{path}\t{len(data)}\t{zlib.crc32(data):08X}")
    if args.output:
        if len(matches) != 1:
            raise ValueError("--output requires exactly one matching asset")
        args.output.write_bytes(matches[0][1])
    return 0 if matches else 1


if __name__ == "__main__":
    raise SystemExit(main())
