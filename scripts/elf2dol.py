"""Drop-in replacement for devkitPro's `elf2dol`: convert an ELF into a
GameCube/Wii DOL. The bundled Nintendont toolchain ships without it, and the
wii_rules `%.dol: %.elf` rule relies on it.

Usage: elf2dol IN.elf OUT.dol
"""
import struct
import sys

from elftools.elf.elffile import ELFFile

PF_X = 0x1
HEADER_SIZE = 0x100


def build_dol(elf):
    text = []  # (addr, data)
    data = []
    bss_start = None
    bss_end = 0

    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        vaddr = seg["p_vaddr"]
        filesz = seg["p_filesz"]
        memsz = seg["p_memsz"]
        if memsz > filesz:
            start = vaddr + filesz
            bss_start = start if bss_start is None else min(bss_start, start)
            bss_end = max(bss_end, vaddr + memsz)
        if filesz == 0:
            continue
        payload = seg.data()[:filesz]
        (text if seg["p_flags"] & PF_X else data).append((vaddr, payload))

    text_off = [0] * 7
    text_addr = [0] * 7
    text_size = [0] * 7
    data_off = [0] * 11
    data_addr = [0] * 11
    data_size = [0] * 11
    blob = bytearray()

    def place(sections, offs, addrs, sizes, limit):
        for i, (addr, payload) in enumerate(sections):
            if i >= limit:
                raise RuntimeError("too many DOL sections")
            offs[i] = HEADER_SIZE + len(blob)
            addrs[i] = addr
            sizes[i] = len(payload)
            blob.extend(payload)
            blob.extend(b"\x00" * (-len(blob) % 32))

    place(text, text_off, text_addr, text_size, 7)
    place(data, data_off, data_addr, data_size, 11)

    bss_addr = bss_start or 0
    bss_size = (bss_end - bss_addr) if bss_start is not None else 0

    header = struct.pack(
        ">18I18I18I2II",
        *text_off, *data_off,
        *text_addr, *data_addr,
        *text_size, *data_size,
        bss_addr, bss_size,
        elf["e_entry"],
    )
    header = header.ljust(HEADER_SIZE, b"\x00")
    return header + bytes(blob)


def main(argv):
    with open(argv[0], "rb") as f:
        dol = build_dol(ELFFile(f))
    with open(argv[1], "wb") as f:
        f.write(dol)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
