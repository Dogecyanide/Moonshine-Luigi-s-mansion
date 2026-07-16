"""Drop-in replacement for devkitPro's `bin2s`: emit GNU assembler source that
embeds a binary file as `<name>`, `<name>_end` and `<name>_size` symbols. The
bundled Nintendont toolchain ships without bin2s, which the wii_rules `bin2o`
macro relies on to turn files under `data/` into linkable objects.

Usage: bin2s [-a ALIGN] FILE... (writes assembly to stdout)
"""
import os
import sys


def symbol_name(path):
    name = os.path.basename(path)
    name = "".join(c if c.isalnum() else "_" for c in name)
    if name[:1].isdigit():
        name = "_" + name
    return name


def emit(path, alignment, out):
    with open(path, "rb") as f:
        data = f.read()
    name = symbol_name(path)
    out.write(f"\t.section .rodata\n\t.balign {alignment}\n")
    out.write(f"\t.global {name}\n{name}:\n")
    for i in range(0, len(data), 16):
        out.write("\t.byte " + ",".join(str(b) for b in data[i:i + 16]) + "\n")
    out.write(f"\t.global {name}_end\n{name}_end:\n")
    out.write(f"\t.balign {alignment}\n\t.global {name}_size\n{name}_size: .int {len(data)}\n")


def main(argv):
    alignment = 4
    output = None
    files = []
    i = 0
    while i < len(argv):
        if argv[i] == "-a":
            alignment = int(argv[i + 1])
            i += 2
        elif argv[i] in ("-o", "--output"):
            output = argv[i + 1]
            i += 2
        else:
            files.append(argv[i])
            i += 1
    out = open(output, "w") if output else sys.stdout
    try:
        for path in files:
            emit(path, alignment, out)
    finally:
        if output:
            out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
