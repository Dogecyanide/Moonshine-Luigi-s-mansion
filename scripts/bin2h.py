"""Embed a binary file as a C header, replacing Nintendont's `bin2h` host tool
(launcher/kernel/bin2h/main.c).

Emits, for input `Foo.bin`:

    #define Foo_size 0x<len>
    const unsigned char Foo[] = { 0x.., ... };

The symbol name defaults to the input's basename without extension (overridable
with --name). Unlike the original, no creation-timestamp comment is written, so
the output is deterministic and doesn't trigger spurious downstream rebuilds.

Usage: bin2h.py IN.bin -o OUT.h [--name NAME]
"""
import argparse
import os
import sys


def emit(data, name):
    lines = [
        f"#define {name}_size 0x{len(data):x}",
        "",
        f"const unsigned char {name}[] = {{",
    ]
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        chunk = ""
        for j, b in enumerate(row):
            if j % 4 == 0:
                chunk += "\t"
            chunk += f"0x{b:02X}"
            if i + j + 1 < len(data):
                chunk += ", "
        lines.append(chunk)
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--name")
    args = ap.parse_args(argv)

    name = args.name or os.path.splitext(os.path.basename(args.input))[0]
    with open(args.input, "rb") as f:
        data = f.read()
    with open(args.output, "w") as f:
        f.write(emit(data, name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
