import argparse
import os
import sys
from pathlib import Path

from dol_c_kit import Project
from patches import patches, base_addr, PatchType

# Limit for the buffer added to nintendont to store our code 
CODE_LIMIT = 8 * 1024

def main():
    ap = argparse.ArgumentParser(description="Link the mod object into a binary for nintendont, patch main.dol, or generate into gecko code.")
    ap.add_argument("--obj", required=True, help="Relocatable mod object (susamune_pre.o)")
    ap.add_argument("--linker-script", required=True, help="Linker script (.ld) defining SMS symbols")
    ap.add_argument("--kuribo-home", required=True, help="Kuribo toolchain directory (provides powerpc-eabi-ld)")
    ap.add_argument("--vers", default="jp", choices=["jp", "us", "pal"])
    ap.add_argument("--out", required=False, help="Output file (if mode is not bin)")
    ap.add_argument("--in-dol", help="Input main.dol (required unless --gecko)")
    ap.add_argument("mode", default="bin", choices=["bin","patch_dol","gecko"])
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
    p.project_name = "susamune"
    p.verbose = True
    p.obj_dir = ""
    p.kuribo_compiler_home = args.kuribo_home
    # added originally to try and make updating a patched iso easier, since the size and offset is always the same.
    # but i'm not really making use of it anymore
    p.code_pad = 32 * 1024 
    p.add_linker_script_file(str(linker_script))
    p.add_obj_file(obj.name)

    for i,patch in enumerate(patches):
        addr = patch[args.vers]
        if addr is None:
            raise ValueError(f"Patch {i} address unset for version {args.vers}!")
        if patch["type"] == PatchType.B:
            p.hook_branch(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == PatchType.BL:
            p.hook_branchlink(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == PatchType.W32:
            p.hook_word(patch[args.vers], patch["val"])

    if args.mode == "bin":
        p.base_addr = 0x933F1000 # dol patch spot
    else:
        p.base_addr = base_addr[args.vers]
        if p.base_addr is None:
            raise ValueError(f"Base address unset for version {args.vers}!")

    # NOTE: currently seems to be broken. maybe base_addr is just set wrong?
    if args.mode == "gecko":
        p.build_gecko(str(out))
    elif args.mode == "patch_dol":
        if in_dol is None:
            ap.error("--in-dol is required to patch a dol")
        p.build_dol(str(in_dol), str(out))
    elif args.mode == "bin":
        if not p.build_project():
            raise RuntimeError("project build failed")
        try:
            susamune_size = os.path.getsize("./susamune.bin")
            if susamune_size > CODE_LIMIT:
                raise RuntimeError("error: code too big")
        except OSError as e:
            print(f"error: cannot get size of {susamune_path}: {e}", file=sys.stderr)
            exit(1)

if __name__ == "__main__":
    main()
