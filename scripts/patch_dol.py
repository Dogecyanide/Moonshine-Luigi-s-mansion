import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from util.dol_c_kit import Project
from susamune.patches import patches, PatchType

# Base address to link code against. Set explicitly to avoid issues with autodetection in devkit_tools.
# TODO: other game versions, this is only for JP
BASE_ADDR = 0x80414020
# Pad injected code so the DOL can be swapped in-place without rebuilding the ISO.
CODE_PAD = 32 * 1024


def main():
    ap = argparse.ArgumentParser(description="Link the mod object into main.dol and apply patches.")
    ap.add_argument("--obj", required=True, help="Relocatable mod object (susamune.o)")
    ap.add_argument("--linker-script", required=True, help="Linker script (.ld) placing the new code")
    ap.add_argument("--kuribo-home", required=True, help="Kuribo toolchain directory (provides powerpc-eabi-ld)")
    ap.add_argument("--vers", default="jp", choices=["jp", "us", "pal"])
    ap.add_argument("--out", required=True, help="Output file (patched main.dol, or Gecko .txt)")
    ap.add_argument("--in-dol", help="Input main.dol (required unless --gecko)")
    ap.add_argument("--gecko", action="store_true", help="Emit a Gecko code list instead of a patched DOL")
    args = ap.parse_args()

    obj = Path(args.obj).resolve()
    linker_script = Path(args.linker_script).resolve()
    out = Path(args.out).resolve()
    in_dol = Path(args.in_dol).resolve() if args.in_dol else None

    # dol_c_kit builds intermediate paths as `obj_dir + name` and prepends
    # "./", so it only works with a relative obj_dir. Run from the object's
    # directory and address everything else by absolute path.
    os.chdir(obj.parent)

    p = Project()
    p.verbose = True
    p.obj_dir = ""
    p.kuribo_compiler_home = args.kuribo_home
    p.code_pad = CODE_PAD
    p.add_linker_script_file(str(linker_script))
    p.add_obj_file(obj.name)

    for patch in patches:
        if patch["type"] == PatchType.B:
            p.hook_branch(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == PatchType.BL:
            p.hook_branchlink(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == PatchType.W32:
            p.hook_word(patch[args.vers], patch["val"])

    p.base_addr = BASE_ADDR
    if args.gecko:
        p.build_gecko(str(out))
    else:
        if in_dol is None:
            ap.error("--in-dol is required when not building a Gecko code list")
        p.build_dol(str(in_dol), str(out))


if __name__ == "__main__":
    main()
