#!/usr/bin/env python3
"""
Parser for CodeWarrior linker map files (GameCube/Wii).

Based on the parsing logic from:
  https://github.com/Cuyler36/Ghidra-GameCube-Loader
  (SymbolLoader.java)

Produces a list of (symbol_name, virtual_address) tuples.
"""

import re
import sys
from dataclasses import dataclass
from typing import Optional


UINT_MASK = 0xFFFFFFFF


@dataclass
class MemoryMapSectionInfo:
    name: str
    starting_address: int
    size: int
    file_offset: int


@dataclass
class SymbolInfo:
    name: str
    container: str
    starting_address: int
    size: int
    virtual_address: int
    alignment: int
    is_sub_entry: bool


def _parse_memory_map(lines: list[str]) -> Optional[list[MemoryMapSectionInfo]]:
    """
    Find the 'Memory map:' section near the end of the file and parse it.
    Returns None if the section is not found (older map format).
    """
    mem_map_start = -1
    for i in range(len(lines) - 1, -1, -1):
        if "Memory map:" in lines[i]:
            mem_map_start = i + 2  # skip header lines ("Starting Size File", "address ...")
            break

    if mem_map_start < 0:
        return None

    sections: list[MemoryMapSectionInfo] = []
    for i in range(mem_map_start, len(lines)):
        line = lines[i]
        if line.strip() == "":
            break

        parts = line.split()
        if len(parts) < 4:
            continue

        try:
            name = parts[0]
            starting_address = int(parts[1], 16) & UINT_MASK
            size = int(parts[2], 16) & UINT_MASK
            file_offset = int(parts[3], 16) & UINT_MASK
        except ValueError:
            continue

        if size > 0:
            sections.append(MemoryMapSectionInfo(name, starting_address, size, file_offset))

    # Adjust file offsets so the first section starts at 0.
    if sections and sections[0].file_offset != 0:
        adjust = sections[0].file_offset
        for s in sections:
            if s.file_offset != 0:
                s.file_offset -= adjust

    return sections if sections else None


def _parse_linker_generated_symbols(lines: list[str]) -> list[SymbolInfo]:
    """
    Parse the 'Linker generated symbols:' block at the very end of the file.
    Each line looks like:  _symbol_name 80XXXXXX
    """
    start = -1
    for i in range(len(lines) - 1, -1, -1):
        if "Linker generated symbols:" in lines[i]:
            start = i + 1
            break

    if start < 0:
        return []

    symbols: list[SymbolInfo] = []
    for i in range(start, len(lines)):
        line = lines[i].strip()
        if line == "":
            break
        parts = line.split()
        if len(parts) < 2:
            continue
        try:
            name = parts[0]
            addr = int(parts[1], 16) & UINT_MASK
            symbols.append(SymbolInfo(name, "", 0, 0, addr, 0, False))
        except ValueError:
            continue

    return symbols


def _parse_section_symbols(lines: list[str], mem_map: Optional[list[MemoryMapSectionInfo]],
                           object_address: int = 0) -> list[SymbolInfo]:
    """
    Walk through the section layout entries and collect symbols.
    This handles both the memory-map-present and memory-map-absent paths
    from the Java reference, but since we only need (name, address) and the
    virtual address is already present in the map file lines for GC DOLs,
    the logic is straightforward.
    """
    symbols: list[SymbolInfo] = []
    current_section_name = ""

    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped == "":
            i += 1
            continue

        # Detect section headers like ".text section layout"
        if " section layout" in stripped:
            section_name = stripped[: stripped.index(" section layout")].strip()
            current_section_name = section_name

            # Skip the column header lines ("Starting  Virtual", "address  Size   address", "----")
            if i + 1 < len(lines) and lines[i + 1].strip().startswith("Starting"):
                i += 4  # skip header + dashes line + blank
                continue
            i += 1
            continue

        # Skip lines from the link-map tree (lines starting with digits followed by ']')
        # e.g. "  1] __start (func,weak) found in ..."
        if re.match(r'\s*\d+\]', line):
            i += 1
            continue

        # Skip the "Memory map:" block and everything after it (handled separately)
        if "Memory map:" in stripped:
            break

        # Skip UNREFERENCED DUPLICATE lines
        if ">>>" in stripped:
            i += 1
            continue

        # Skip UNUSED lines (no virtual address)
        if stripped.startswith("UNUSED"):
            i += 1
            continue

        # Handle "(entry of <section>)" annotations — strip them from the line
        is_sub_entry = False
        entry_match = re.search(r'\(entry of [^)]+\)', line)
        if entry_match:
            is_sub_entry = True
            line = line[: entry_match.start()] + line[entry_match.end():]
            stripped = line.strip()

        # Now try to parse a normal symbol line:
        #   starting_addr  size  virtual_addr  alignment  name  container
        # e.g.:
        #   00000000 000044 80005600  4 main 	main.o
        parts = stripped.split()
        if len(parts) < 5:
            i += 1
            continue

        try:
            starting_address = int(parts[0], 16) & UINT_MASK
            size = int(parts[1], 16) & UINT_MASK
            virtual_address = int(parts[2], 16) & UINT_MASK
        except ValueError:
            i += 1
            continue

        # Determine alignment and name index.
        # For "(entry of ...)" lines, the alignment field is actually the name.
        try:
            alignment = int(parts[3])
            name = parts[4] if len(parts) > 4 else ""
            container = parts[5] if len(parts) > 5 else ""
        except ValueError:
            # alignment field is not a number — treat parts[3] as the name
            alignment = 0
            name = parts[3]
            container = parts[4] if len(parts) > 4 else ""

        # Compilation unit BLOBs have alignment == 1 and their name is a
        # section name (e.g. ".text", ".sdata2", "extab", "extabindex", etc).
        # These represent per-object-file section chunks, not real symbols.
        # The Java code skips sub-entries whose name matches the current
        # section name; we generalise this to skip any alignment-1 entry
        # whose name looks like a known section name.
        # "(entry of ...)" symbols (is_sub_entry) with *unique* names like
        # gTRKInterruptVectorTable are kept.
        _SECTION_NAMES = {
            ".init", ".text", ".ctors", ".dtors", ".rodata", ".data", ".bss",
            ".sdata", ".sbss", ".sdata2", ".sbss2",
            "extab", "extabindex",
        }
        if alignment == 1 and (name.startswith(".") or name in _SECTION_NAMES):
            i += 1
            continue

        if is_sub_entry and name == current_section_name:
            i += 1
            continue

        sym = SymbolInfo(name, container, starting_address, size, virtual_address, alignment, is_sub_entry)
        symbols.append(sym)
        i += 1

    return symbols


def parse_map_file(filepath: str) -> list[tuple[str, int]]:
    """
    Parse a CodeWarrior GameCube/Wii .map file and return a list of
    (symbol_name, virtual_address) tuples.
    """
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    # Strip trailing newlines
    lines = [l.rstrip("\n").rstrip("\r") for l in lines]

    mem_map = _parse_memory_map(lines)
    section_symbols = _parse_section_symbols(lines, mem_map)
    linker_symbols = _parse_linker_generated_symbols(lines)

    result: list[tuple[str, int]] = []
    seen: set[tuple[str, int]] = set()

    for sym in section_symbols + linker_symbols:
        key = (sym.name, sym.virtual_address)
        if key not in seen:
            seen.add(key)
            result.append(key)

    return result

from util.dol_c_kit.mangle import LDPlusPlus, ABI
from util.dol_c_kit.mac_demangle import mac_demangle_signature, DemangleError

def main(map_path,ld_path):
    
    symbols = parse_map_file(map_path)

    with open("syms.log", 'w') as log_f:

        ldpp = LDPlusPlus(ABI.Macintosh)
        for (sym, val) in symbols:
            # virtual thunk || weird section artifact from parsing map file (? not sure) 
            if sym.startswith("@") or sym.startswith("..."):
                continue 

            try:
                sig = mac_demangle_signature(sym)
                m = sig.macintosh_mangle()
                if "<" in m or "<" in m or "@" in m or m == "0":
                    # TODO: somewhere in this pipeline, it is not handling symbols that were mangled 
                    # with "<", ">", and "@" in the output. They appear in the remangled symbol, and this will not parse correctly.
                    # For now, we just skip them. They won't appear in the linker script.
                    # having issues with "0" also? idk
                    raise DemangleError
                ldpp.assign_sig(sig, val)
                log_f.write(f"{val:08x} = {sym} | {m}  \n")
            except DemangleError:
                log_f.write(f"{val:08x} = {sym} | ???  \n")
                print("Could not demangle symbol: " + sym)
        ldpp.save(ld_path)
        
#
#    filepath = sys.argv[1]
#    csv_mode = "--csv" in sys.argv
#
#    symbols = parse_map_file(filepath)
#
#    if csv_mode:
#        print("symbol,address")
#        for name, addr in symbols:
#            print(f"{name},0x{addr:08X}")
#    else:
#        for name, addr in symbols:
#            print(f"0x{addr:08X}  {name}")
#
#    print(f"\n# Total symbols: {len(symbols)}", file=sys.stderr)



if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <map_file> <ld_file>")
        sys.exit(1)
    
    map_path = sys.argv[1]
    ld_path = sys.argv[2]

    main(map_path, ld_path)

