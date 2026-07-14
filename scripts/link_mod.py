import argparse
import os
from pathlib import Path

from dol_c_kit import Project
import patches


def main():
    ap = argparse.ArgumentParser(description="Link the mod object into a patched main.dol, a Gecko code list, or a launcher manifest.")
    ap.add_argument("--obj", required=True, help="Relocatable mod object (susamune_pre.o)")
    ap.add_argument("--linker-script", required=True, help="Linker script (.ld) defining SMS symbols")
    ap.add_argument("--kuribo-home", required=True, help="Kuribo toolchain directory (provides powerpc-eabi-ld)")
    ap.add_argument("--vers", default="jp", choices=["jp", "us", "pal"])
    ap.add_argument("--out", required=True, help="Output file")
    ap.add_argument("--in-dol", help="Input main.dol (required for patch_dol)")
    ap.add_argument("mode", choices=["patch_dol", "gecko", "launcher"])
    args = ap.parse_args()

    obj = Path(args.obj).resolve()
    linker_script = Path(args.linker_script).resolve()
    out = Path(args.out).resolve()
    in_dol = Path(args.in_dol).resolve() if args.in_dol else None

    # dol_c_kit builds intermediate paths as `obj_dir + name` and prepends
    # "./", so it only works with a relative obj_dir. Run from the object's
    # directory and address everything else by absolute path.
    os.chdir(obj.parent)

    base = patches.base_addr[args.vers]
    if base is None:
        raise ValueError(f"Base address unset for version {args.vers}!")

    p = Project()
    p.project_name = "susamune"
    p.verbose = True
    p.obj_dir = ""
    p.kuribo_compiler_home = args.kuribo_home
    p.base_addr = base
    p.add_linker_script_file(str(linker_script))
    p.add_obj_file(obj.name)

    for i, patch in enumerate(patches.patches):
        addr = patch[args.vers]
        if addr is None:
            raise ValueError(f"Patch {i} address unset for version {args.vers}!")
        if patch["type"] == patches.PatchType.B:
            p.hook_branch(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == patches.PatchType.BL:
            p.hook_branchlink(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == patches.PatchType.W32:
            p.hook_word(patch[args.vers], patch["val"])

    if args.mode == "patch_dol":
        if in_dol is None:
            ap.error("--in-dol is required to patch a dol")
        p.build_dol(str(in_dol), str(out))
    elif args.mode == "gecko":
        p.build_gecko(str(out))
    elif args.mode == "launcher":
        meta = {
            "game_id": patches.game_id[args.vers],
            "region": patches.region[args.vers],
            "disc_name": patches.disc_name[args.vers],
        }
        p.build_launcher_manifest(str(out), meta=meta, max_size=patches.mod_region_size)


if __name__ == "__main__":
    main()
