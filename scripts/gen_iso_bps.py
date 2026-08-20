"""Build a source-free BPS patch for a retail Super Mario Sunshine ISO.

The checked-in layout contains only the few offsets and CRCs needed to grow
the DOL in place. The ``layout`` subcommand is the maintainer-only path that
reads a clean ISO and regenerates that metadata.
"""

import argparse
import json
import struct
import zlib
from pathlib import Path

from patches import mod_blob_max_size as MOD_BLOB_MAX_SIZE
from patches import mod_region_size as MOD_REGION_SIZE


BPS_MAGIC = b"BPS1"
GC_ISO_SIZE = 1459978240
RELOCATED_FST_OFFSET = 0x01000000
RELOCATED_FILE_ALIGNMENT = 0x20
BOOT_FST_OFFSET_FIELD = 0x424
BOOT_FIRST_FILE_FIELD = 0x434
DOL_TEXT_OFFSET_TABLE = 0x00
DOL_TEXT_ADDRESS_TABLE = 0x48
DOL_TEXT_SIZE_TABLE = 0x90


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def crc32_combine(crc1, crc2, length2):
    """Return CRC32(data1 + data2) from the two component CRCs."""
    if length2 <= 0:
        return crc1

    def matrix_times(matrix, vector):
        result = 0
        index = 0
        while vector:
            if vector & 1:
                result ^= matrix[index]
            vector >>= 1
            index += 1
        return result

    def matrix_square(matrix):
        return [matrix_times(matrix, row) for row in matrix]

    odd = [0] * 32
    odd[0] = 0xEDB88320
    row = 1
    for index in range(1, 32):
        odd[index] = row
        row <<= 1

    even = matrix_square(odd)
    odd = matrix_square(even)
    while length2:
        even = matrix_square(odd)
        if length2 & 1:
            crc1 = matrix_times(even, crc1)
        length2 >>= 1
        if not length2:
            break
        odd = matrix_square(even)
        if length2 & 1:
            crc1 = matrix_times(odd, crc1)
        length2 >>= 1
    return (crc1 ^ crc2) & 0xFFFFFFFF


def zero_crc(length):
    result_crc = 0
    block_crc = zlib.crc32(b"\0")
    block_size = 1
    while length:
        if length & 1:
            result_crc = crc32_combine(result_crc, block_crc, block_size)
        length >>= 1
        if length:
            block_crc = crc32_combine(block_crc, block_crc, block_size)
            block_size *= 2
    return result_crc


def encode_number(value):
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value == 0:
            encoded.append(0x80 | byte)
            return encoded
        encoded.append(byte)
        value -= 1


def read_crc(stream, offset, size):
    stream.seek(offset)
    crc = 0
    remaining = size
    while remaining:
        data = stream.read(min(remaining, 8 * 1024 * 1024))
        if not data:
            raise ValueError(f"short read at {offset:#x} ({size:#x} bytes)")
        crc = zlib.crc32(data, crc)
        remaining -= len(data)
    return crc & 0xFFFFFFFF


def hook_iso_offset(layout, address):
    for hook in layout["hooks"]:
        if hook["address"] == address:
            return hook["iso_offset"]
    raise ValueError(f"hook address {address:#x} is not mapped by the retail DOL")


def hook_addresses(mod_manifest):
    addresses = [write[0] for write in mod_manifest["writes"]]
    if len(addresses) != len(set(addresses)):
        raise ValueError("mod manifest contains duplicate write addresses")
    return addresses


def add_operation(operations, target_offset, kind, value, size=None):
    if kind == "literal":
        size = len(value)
    if size <= 0:
        return
    operations.append({
        "target_offset": target_offset,
        "kind": kind,
        "value": value,
        "size": size,
    })


def build_operations(layout, mod_manifest):
    code = bytes.fromhex(mod_manifest["code"])
    if len(code) != mod_manifest["size"] or len(code) % 4:
        raise ValueError("mod code size is inconsistent or not word-aligned")
    region_size = layout["mod_region_size"]
    if len(code) > MOD_BLOB_MAX_SIZE:
        raise ValueError(
            f"mod code is {len(code):#x} bytes, over the "
            f"{MOD_BLOB_MAX_SIZE:#x} MEM1 working cap")
    if len(code) > region_size:
        raise ValueError(f"mod code is {len(code):#x} bytes, over the {region_size:#x} DOL region")

    operations = []
    add_operation(operations, BOOT_FST_OFFSET_FIELD, "literal", struct.pack(">I", layout["fst"]["target_offset"]))
    add_operation(operations, BOOT_FIRST_FILE_FIELD, "literal", struct.pack(">I", layout["first_file_offset"]))

    dol = layout["dol"]
    slot = dol["new_text_slot"]
    dol_words = {
        dol["iso_offset"] + DOL_TEXT_OFFSET_TABLE + slot * 4: dol["size"],
        dol["iso_offset"] + DOL_TEXT_ADDRESS_TABLE + slot * 4: mod_manifest["base_addr"],
        dol["iso_offset"] + DOL_TEXT_SIZE_TABLE + slot * 4: region_size,
    }
    for address, value in mod_manifest["writes"]:
        dol_words[hook_iso_offset(layout, address)] = value
    for offset, value in dol_words.items():
        add_operation(operations, offset, "literal", struct.pack(">I", value))

    expanded_dol = dol["iso_offset"] + dol["size"]
    add_operation(operations, expanded_dol, "literal", code)
    add_operation(operations, expanded_dol + len(code), "zero", None, region_size - len(code))

    fst = layout["fst"]
    source_cursor = fst["source_offset"]
    target_cursor = fst["target_offset"]
    for file in sorted(layout["relocated_files"], key=lambda item: item["fst_word_offset"]):
        source_word = fst["source_offset"] + file["fst_word_offset"]
        copy_size = source_word - source_cursor
        add_operation(operations, target_cursor, "source", source_cursor, copy_size)
        target_cursor += copy_size
        add_operation(operations, target_cursor, "literal", struct.pack(">I", file["target_offset"]))
        target_cursor += 4
        source_cursor = source_word + 4
    copy_size = fst["source_offset"] + fst["size"] - source_cursor
    add_operation(operations, target_cursor, "source", source_cursor, copy_size)

    for file in layout["relocated_files"]:
        add_operation(
            operations,
            file["target_offset"],
            "source",
            file["source_offset"],
            file["size"],
        )

    operations.sort(key=lambda operation: operation["target_offset"])
    cursor = 0
    for operation in operations:
        if operation["target_offset"] < cursor:
            raise ValueError(f"overlapping target operation at {operation['target_offset']:#x}")
        cursor = operation["target_offset"] + operation["size"]
    if cursor > layout["iso_size"]:
        raise ValueError("target operations extend beyond the GameCube ISO")
    return operations


def expected_source_ranges(layout, mod_manifest):
    cursor = 0
    for operation in build_operations(layout, mod_manifest):
        target = operation["target_offset"]
        if target > cursor:
            yield cursor, target - cursor
        if operation["kind"] == "source":
            yield operation["value"], operation["size"]
        cursor = target + operation["size"]
    if cursor < layout["iso_size"]:
        yield cursor, layout["iso_size"] - cursor


class BpsBuilder:
    def __init__(self, source_crcs):
        self.actions = []
        self.output_offset = 0
        self.target_crc = 0
        self.source_crcs = source_crcs

    def _append(self, action, size, argument, crc):
        if size <= 0:
            return
        if self.actions:
            previous_action, previous_size, previous_argument = self.actions[-1]
            merge = action == previous_action and (
                action == 0
                or action == 1
                or (action == 2 and previous_argument + previous_size == argument)
            )
            if merge:
                if action == 1:
                    previous_argument += argument
                self.actions[-1] = (action, previous_size + size, previous_argument)
            else:
                self.actions.append((action, size, argument))
        else:
            self.actions.append((action, size, argument))
        self.target_crc = crc32_combine(self.target_crc, crc, size)
        self.output_offset += size

    def _source_crc(self, offset, size):
        try:
            return self.source_crcs[(offset, size)]
        except KeyError as exc:
            raise ValueError(
                f"layout has no CRC for source range {offset:#x}+{size:#x}; "
                "regenerate it with the clean ISO"
            ) from exc

    def source_read(self, offset, size):
        if size <= 0:
            return
        if offset != self.output_offset:
            raise ValueError("SourceRead must use the current output offset")
        self._append(0, size, None, self._source_crc(offset, size))

    def target_read(self, data):
        self._append(1, len(data), data, zlib.crc32(data) & 0xFFFFFFFF)

    def source_copy(self, offset, size):
        if size <= 0:
            return
        self._append(2, size, offset, self._source_crc(offset, size))

    def zeros(self, size):
        if size <= 0:
            return
        self.target_read(b"\0")
        if size > 1:
            self._append(3, size - 1, self.output_offset - 1, zero_crc(size - 1))

    def encode(self, source_size, source_crc):
        patch = bytearray(BPS_MAGIC)
        patch += encode_number(source_size)
        patch += encode_number(self.output_offset)
        patch += encode_number(0)

        source_relative = 0
        target_relative = 0
        for action, size, argument in self.actions:
            patch += encode_number(((size - 1) << 2) | action)
            if action == 1:
                patch += argument
            elif action == 2:
                delta = argument - source_relative
                patch += encode_number((abs(delta) << 1) | int(delta < 0))
                source_relative = argument + size
            elif action == 3:
                delta = argument - target_relative
                patch += encode_number((abs(delta) << 1) | int(delta < 0))
                target_relative = argument + size

        patch += struct.pack("<II", source_crc, self.target_crc)
        patch += struct.pack("<I", zlib.crc32(patch) & 0xFFFFFFFF)
        return patch


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def validate_layout(layout, mod_manifest):
    if layout["format_version"] != 2:
        raise ValueError(f"unsupported ISO layout version {layout['format_version']}")
    if layout["game_id"] != mod_manifest["game_id"]:
        raise ValueError("layout and mod manifest have different game IDs")
    if layout["base_addr"] != mod_manifest["base_addr"]:
        raise ValueError("layout and mod manifest have different link addresses")
    if layout["mod_region_size"] != MOD_REGION_SIZE:
        raise ValueError("layout uses an unexpected DOL mod-region size")
    expected = {hook["address"] for hook in layout["hooks"]}
    actual = set(hook_addresses(mod_manifest))
    if actual != expected:
        raise ValueError("mod hook addresses changed; regenerate the ISO layout with the clean ISO")


def build_patch(layout, mod_manifest):
    validate_layout(layout, mod_manifest)
    source_crcs = {
        (entry["offset"], entry["size"]): int(entry["crc32"], 16)
        for entry in layout["source_ranges"]
    }
    builder = BpsBuilder(source_crcs)
    cursor = 0
    for operation in build_operations(layout, mod_manifest):
        target = operation["target_offset"]
        builder.source_read(cursor, target - cursor)
        if operation["kind"] == "literal":
            builder.target_read(operation["value"])
        elif operation["kind"] == "zero":
            builder.zeros(operation["size"])
        else:
            builder.source_copy(operation["value"], operation["size"])
        cursor = target + operation["size"]
    builder.source_read(cursor, layout["iso_size"] - cursor)
    if builder.output_offset != layout["iso_size"]:
        raise AssertionError("BPS output size is not a full GameCube ISO")
    patch = builder.encode(layout["iso_size"], int(layout["source_crc32"], 16))
    return patch, builder


def create_layout(iso_path, mod_manifest, region):
    from pyisotools.iso import GamecubeISO

    iso_path = Path(iso_path)
    if iso_path.stat().st_size != GC_ISO_SIZE:
        raise ValueError(f"expected a {GC_ISO_SIZE}-byte GameCube ISO")
    disc = GamecubeISO.from_iso(iso_path)
    nodes = list(disc.rfiles(includedOnly=True))
    dol_end = disc.bootheader.dolOffset + disc.dol.size
    expanded_dol_end = dol_end + MOD_REGION_SIZE

    overlapped = [
        node for node in nodes
        if node._fileoffset < expanded_dol_end and node._fileoffset + node.size > dol_end
    ]
    if not overlapped:
        raise ValueError("expanded DOL does not overlap a file; layout assumptions changed")

    target_cursor = align_up(RELOCATED_FST_OFFSET + disc.bootheader.fstSize, RELOCATED_FILE_ALIGNMENT)
    relocated_files = []
    relocated_offsets = {}
    for node in sorted(overlapped, key=lambda item: item._fileoffset):
        target_cursor = align_up(target_cursor, RELOCATED_FILE_ALIGNMENT)
        relocated_files.append({
            "source_offset": node._fileoffset,
            "target_offset": target_cursor,
            "size": node.size,
            "fst_word_offset": node._id * 12 + 4,
        })
        relocated_offsets[node._fileoffset] = target_cursor
        target_cursor += node.size

    if expanded_dol_end > RELOCATED_FST_OFFSET:
        raise ValueError("expanded DOL reaches the relocation window")
    for node in nodes:
        if node._fileoffset < target_cursor and node._fileoffset + node.size > RELOCATED_FST_OFFSET:
            raise ValueError(f"relocation window overlaps retail file {node.path}")

    with iso_path.open("rb") as source:
        source.seek(disc.bootheader.dolOffset)
        text_offsets = struct.unpack(">7I", source.read(28))
        try:
            new_text_slot = text_offsets.index(0)
        except ValueError as exc:
            raise ValueError("retail DOL has no free text section slot") from exc
        source.seek(0)
        game_id = int.from_bytes(source.read(4), "big")

    first_file_offset = min(
        relocated_offsets.get(node._fileoffset, node._fileoffset) for node in nodes
    )
    layout = {
        "format_version": 2,
        "region": region,
        "game_id": game_id,
        "base_addr": mod_manifest["base_addr"],
        "iso_size": disc.MaxSize,
        "source_crc32": "00000000",
        "mod_region_size": MOD_REGION_SIZE,
        "first_file_offset": first_file_offset,
        "dol": {
            "iso_offset": disc.bootheader.dolOffset,
            "size": disc.dol.size,
            "new_text_slot": new_text_slot,
        },
        "fst": {
            "source_offset": disc.bootheader.fstOffset,
            "target_offset": RELOCATED_FST_OFFSET,
            "size": disc.bootheader.fstSize,
        },
        "relocated_files": relocated_files,
        "hooks": [],
        "source_ranges": [],
    }
    if game_id != mod_manifest["game_id"]:
        raise ValueError("clean ISO and mod manifest have different game IDs")

    with iso_path.open("rb") as source:
        for address in hook_addresses(mod_manifest):
            try:
                section = next(
                    section for section in disc.dol.sections
                    if section.address <= address and address + 4 <= section.address + section.size
                )
            except StopIteration as exc:
                raise ValueError(f"hook address {address:#x} is not mapped by the retail DOL") from exc
            layout["hooks"].append({
                "address": address,
                "iso_offset": disc.bootheader.dolOffset + section.offset + address - section.address,
            })
        ranges = list(dict.fromkeys(expected_source_ranges(layout, mod_manifest)))
        for offset, size in ranges:
            layout["source_ranges"].append({
                "offset": offset,
                "size": size,
                "crc32": f"{read_crc(source, offset, size):08X}",
            })
        layout["source_crc32"] = f"{read_crc(source, 0, disc.MaxSize):08X}"
    return layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    build = subparsers.add_parser("build", help="build a BPS without reading the retail ISO")
    build.add_argument("--layout", required=True)
    build.add_argument("--mod-manifest", required=True)
    build.add_argument("--output", required=True)

    layout = subparsers.add_parser("layout", help="regenerate retail layout metadata")
    layout.add_argument("--iso", required=True)
    layout.add_argument("--mod-manifest", required=True)
    layout.add_argument("--region", required=True, choices=("jp", "us", "pal"))
    layout.add_argument("--output", required=True)
    args = parser.parse_args()

    mod_manifest = load_json(args.mod_manifest)
    if args.command == "layout":
        result = create_layout(args.iso, mod_manifest, args.region)
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(
            f"Wrote {output} ({len(result['source_ranges'])} source ranges, "
            f"{len(result['relocated_files'])} relocated files, CRC32 {result['source_crc32']})"
        )
        return

    retail_layout = load_json(args.layout)
    patch, builder = build_patch(retail_layout, mod_manifest)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(patch)
    literal_size = sum(size for action, size, _ in builder.actions if action == 1)
    print(
        f"Wrote {output} ({len(patch)} bytes, {len(builder.actions)} actions, "
        f"{literal_size} literal bytes, target CRC32 {builder.target_crc:08X})"
    )


if __name__ == "__main__":
    main()
