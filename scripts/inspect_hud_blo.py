import struct
import sys


def be16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def sbe16(data, offset):
    return struct.unpack_from(">h", data, offset)[0]


def be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def cstring(data, offset):
    end = data.index(0, offset)
    return data[offset:end].decode("ascii")


def yaz0(data):
    if data[:4] != b"Yaz0":
        return data
    output = bytearray()
    target = be32(data, 4)
    source = 16
    valid = 0
    code = 0
    while len(output) < target:
        if valid == 0:
            code = data[source]
            source += 1
            valid = 8
        if code & 0x80:
            output.append(data[source])
            source += 1
        else:
            pair = be16(data, source)
            source += 2
            distance = (pair & 0x0FFF) + 1
            length = pair >> 12
            if length == 0:
                length = data[source] + 0x12
                source += 1
            else:
                length += 2
            for _ in range(length):
                output.append(output[-distance])
        code <<= 1
        valid -= 1
    return bytes(output)


def rarc_file(data, wanted):
    assert data[:4] == b"RARC"
    header = be32(data, 8)
    data_offset = header + be32(data, 0x0C)
    nodes = be32(data, 0x20)
    node_table = header + be32(data, 0x24)
    file_table = header + be32(data, 0x2C)
    strings = header + be32(data, 0x34)
    seen = set()
    for node in range(nodes):
        entry_count = be16(data, node_table + node * 0x10 + 0x0A)
        first = be32(data, node_table + node * 0x10 + 0x0C)
        for index in range(first, first + entry_count):
            if index in seen:
                continue
            seen.add(index)
            entry = file_table + index * 0x14
            flags_name = be32(data, entry + 4)
            flags = flags_name >> 24
            name = cstring(data, strings + (flags_name & 0xFFFFFF))
            if not flags & 0x02 and name == wanted:
                offset = data_offset + be32(data, entry + 8)
                size = be32(data, entry + 0x0C)
                return yaz0(data[offset:offset + size])
    raise RuntimeError(f"{wanted} not found")


def tag_text(raw):
    return "".join("\\0" if value == 0 else chr(value) for value in raw)


def resource(data, offset):
    length = data[offset + 1]
    name = data[offset + 2:offset + 2 + length].decode("ascii")
    return name, offset + 2 + length


def inspect_blo(data):
    assert data[:8] == b"SCRNblo1"
    targets = {
        b"w_ba", b"w_tx", b"\0w_0", b"w_t0", b"w_t1", b"w_t2", b"w_t3",
        b"t_ba", b"t_tx", b"\0t_0", b"\0t_1", b"\0t_2",
        b"b_ba", b"b_sl", b"\0b_0", b"r_ba", b"r_sl", b"\0r_0",
        b"d_ba", b"d_ic", b"\0d_x", b"\0d_0",
        b"c_ba", b"s_ba", b"m_ba",
    }
    offset = 0x20
    parents = [b"ROOT"]
    last = b"ROOT"
    while offset + 8 <= len(data):
        magic = data[offset:offset + 4]
        size = be32(data, offset + 4)
        if size < 8:
            raise RuntimeError(f"bad chunk at {offset:#x}")
        if magic in (b"PAN1", b"PIC1", b"TBX1", b"WIN1"):
            body = offset + 8
            count = data[body]
            visible = data[body + 1]
            tag = data[body + 4:body + 8]
            x, y, width, height = (
                sbe16(data, body + 8), sbe16(data, body + 10),
                sbe16(data, body + 12), sbe16(data, body + 14),
            )
            last = tag
            if tag in targets:
                texture = ""
                if magic == b"PIC1":
                    pane_end = 24
                    if count >= 7:
                        pane_end += 2
                    if count >= 8:
                        pane_end += 1
                    if count >= 9:
                        pane_end += 1
                    if count >= 10:
                        pane_end += 1
                    pane_end = (pane_end + 3) & ~3
                    pic_count = data[offset + pane_end]
                    texture, next_offset = resource(data, offset + pane_end + 1)
                    palette, next_offset = resource(data, next_offset)
                    colors = ""
                    flags_offset = next_offset + 1
                    if pic_count >= 6:
                        colors += f" black={be32(data, flags_offset + 2):08x}"
                    if pic_count >= 7:
                        colors += f" white={be32(data, flags_offset + 6):08x}"
                    texture = (f" tex={texture!r} pal={palette!r} "
                               f"picData={pic_count}{colors}")
                print(
                    f"{tag_text(tag):4} {magic.decode()} visible={visible} "
                    f"rect=({x},{y},{width},{height}) "
                    f"parent={tag_text(parents[-1])}{texture}"
                )
        elif magic == b"BGN1":
            parents.append(last)
        elif magic == b"END1":
            parents.pop()
        elif magic == b"EXT1":
            break
        offset += size


with open(sys.argv[1], "rb") as iso:
    iso.seek(0x424)
    fst_offset = be32(iso.read(4), 0)
    iso.seek(fst_offset)
    root = iso.read(12)
    entry_count = be32(root, 8)
    iso.seek(fst_offset)
    entries = iso.read(entry_count * 12)
    strings_offset = fst_offset + len(entries)
    archive_offset = None
    archive_size = None
    for index in range(1, entry_count):
        entry = index * 12
        type_name = be32(entries, entry)
        if type_name >> 24:
            continue
        iso.seek(strings_offset + (type_name & 0xFFFFFF))
        name = bytearray()
        while True:
            value = iso.read(1)
            if value == b"\0":
                break
            name.extend(value)
        if name.startswith(b"game_6") and name.endswith(b".szs"):
            archive_offset = be32(entries, entry + 4)
            archive_size = be32(entries, entry + 8)
            break
    if archive_offset is None:
        raise RuntimeError("game_6.szs not found")
    iso.seek(archive_offset)
    archive = yaz0(iso.read(archive_size))
inspect_blo(rarc_file(archive, "standard_1.blo"))
