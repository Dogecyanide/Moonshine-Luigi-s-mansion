import argparse
import importlib.util
import json
import os
import re
from pathlib import Path

from dol_c_kit import Project


def load_patch_module(path):
    spec = importlib.util.spec_from_file_location("susamune_build_patches", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load patch configuration {}".format(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_shared_layout(patches, vers):
    """Keep the build-side layout in lockstep with the shared launcher header."""
    header = Path(__file__).parent.parent / "include" / "susamune" / "mod_bin.h"
    text = header.read_text()

    def hex_define(name):
        match = re.search(
            r"^#define\s+{}\s+(0x[0-9a-fA-F]+)u?\s*$".format(re.escape(name)),
            text, re.M)
        if not match:
            raise RuntimeError("{} not found as a hex constant in {}".format(name, header))
        return int(match.group(1), 16)

    base_defines = {
        "jp": "SUSAMUNE_MOD_BASE_JP",
        "us": "SUSAMUNE_MOD_BASE_US",
        "pal": "SUSAMUNE_MOD_BASE_PAL",
        "lmj": "SUSAMUNE_MOD_BASE_LMJ",
    }
    if vers not in base_defines:
        raise RuntimeError("no shared mod-base define for version {}".format(vers))
    if vers not in patches.base_addr:
        raise RuntimeError("patch configuration has no base for {}".format(vers))
    expected = {
        "SUSAMUNE_MOD_REGION_SIZE": patches.mod_region_size,
        "SUSAMUNE_MOD_BLOB_MAX_SIZE": patches.mod_blob_max_size,
        "SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE": patches.mod_mem1_working_cap_size,
        "SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET": patches.mod_attachment_heap_offset,
        "SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE": patches.mod_attachment_heap_size,
        "SUSAMUNE_SCRATCH": patches.mod_scratch_size,
        "SUSAMUNE_DEBUG_STACK_SIZE": patches.debug_stack_size,
    }
    # The inherited table historically checked every regional base even when
    # only one payload was selected. Keep that drift check while allowing the
    # GLMJ-specific table to define just its own base.
    for patch_vers, base in patches.base_addr.items():
        if patch_vers not in base_defines:
            raise RuntimeError(
                "no shared mod-base define for version {}".format(patch_vers))
        expected[base_defines[patch_vers]] = base
    # Authenticated payloads may bind themselves to Nintendont's parsed DOL
    # tuple.  Keep those C/Python constants under the same build-time drift
    # check as the MEM1 layout without imposing them on legacy V1 payloads.
    dol_sizes = getattr(patches, "dol_size", {})
    if vers in dol_sizes:
        if vers != "lmj":
            raise RuntimeError("no shared DOL fingerprint defines for {}".format(vers))
        expected["SUSAMUNE_MOD_DOL_SIZE_LMJ"] = dol_sizes[vers]
        expected["SUSAMUNE_MOD_DOL_MIN_LMJ"] = patches.dol_min[vers]
        expected["SUSAMUNE_MOD_DOL_MAX_LMJ"] = patches.dol_max[vers]
    for name, value in expected.items():
        header_value = hex_define(name)
        if header_value != value:
            raise RuntimeError(
                "{} is {:#x} in {} but {:#x} in patches.py".format(
                    name, header_value, header, value))

    mem2_header = header.parent / "mem2_map.h"
    mem2_text = mem2_header.read_text()
    def mem2_hex_define(name):
        match = re.search(
            r"^#define\s+{}\s+(0x[0-9a-fA-F]+)u?\s*$".format(
                re.escape(name)), mem2_text, re.M)
        if not match:
            raise RuntimeError("{} not found as a hex constant in {}".format(
                name, mem2_header))
        return int(match.group(1), 16)

    staged_max = mem2_hex_define("SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE")
    vault_offset = mem2_hex_define("SUSAMUNE_GHOST_ASSET_VAULT_OFFSET")
    if staged_max != patches.mod_file_max_size or staged_max != vault_offset:
        raise RuntimeError(
            "mod file ceiling, asset vault, and patches.py disagree")


def check_arena_reserve(linker_script, base, patches):
    """Verify the debug-stack gap this build's arena reserve assumes.

    OSInit lowers __OSArenaLo to ALIGN32(_stack_addr) when no debug monitor is
    present, so the reserve has to span that gap plus the mod region. If a
    region's map disagrees, the heap would silently overlap the blob.
    """
    text = Path(linker_script).read_text()
    syms = {}
    for name in ("_stack_addr", "__ArenaLo"):
        m = re.search(r"^{} = (0x[0-9a-fA-F]+);".format(re.escape(name)), text, re.M)
        if not m:
            raise RuntimeError("{} not found in {}".format(name, linker_script))
        syms[name] = int(m.group(1), 16)

    if syms["__ArenaLo"] != base:
        raise RuntimeError("link base {:#x} is not __ArenaLo {:#x}".format(base, syms["__ArenaLo"]))

    gap = syms["__ArenaLo"] - ((syms["_stack_addr"] + 0x1F) & ~0x1F)
    if gap != patches.debug_stack_size:
        raise RuntimeError(
            "debug stack gap is {:#x} (__ArenaLo {:#x}, _stack_addr {:#x}) but "
            "patches.debug_stack_size is {:#x}".format(
                gap, syms["__ArenaLo"], syms["_stack_addr"], patches.debug_stack_size))


def main():
    ap = argparse.ArgumentParser(description="Link the mod object into a patched main.dol, a Gecko code list, or a launcher manifest.")
    ap.add_argument("--obj", required=True, help="Relocatable mod object (susamune_pre.o)")
    ap.add_argument("--linker-script", required=True, help="Linker script (.ld) defining SMS symbols")
    ap.add_argument("--kuribo-home", required=True, help="Kuribo toolchain directory (provides powerpc-eabi-ld)")
    ap.add_argument("--vers", default="jp")
    ap.add_argument(
        "--patches-file",
        default=str(Path(__file__).with_name("patches.py")),
        help="Patch configuration module (defaults to scripts/patches.py)")
    ap.add_argument("--out", required=True, help="Output file")
    ap.add_argument("--in-dol", help="Input main.dol (required for patch_dol)")
    ap.add_argument("--print-commands", action="store_true", help="Echo the raw compiler/linker argv")
    ap.add_argument("mode", choices=["patch_dol", "gecko", "launcher"])
    args = ap.parse_args()

    patch_path = Path(args.patches_file).resolve()
    patches = load_patch_module(patch_path)
    check_shared_layout(patches, args.vers)

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

    check_arena_reserve(linker_script, base, patches)

    p = Project()
    # The intermediates (<name>.o / <name>.bin) live in obj_dir, which is the
    # build dir shared by every version, and all three versions link in
    # parallel -- so the name has to carry the version or they clobber
    # each other mid-link.
    p.project_name = f"susamune_{args.vers}"
    p.verbose = True
    p.print_commands = args.print_commands
    p.obj_dir = ""
    p.kuribo_compiler_home = args.kuribo_home
    p.base_addr = base
    p.blob_max_size = patches.mod_blob_max_size
    p.add_linker_script_file(str(linker_script))
    p.add_obj_file(obj.name)
    p.linker_flags.append("--gc-sections")

    for i, patch in enumerate(patches.patches):
        addr = patch[args.vers]
        if addr is None:
            raise ValueError(f"Patch {i} address unset for version {args.vers}!")
        if patch["type"] == patches.PatchType.B:
            p.linker_flags.append(f"--undefined={patch['sym']}")
            p.hook_branch(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == patches.PatchType.BL:
            p.linker_flags.append(f"--undefined={patch['sym']}")
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
        p.build_launcher_manifest(str(out), meta=meta, region_reserve=patches.arena_reserve)

        # Authenticated payloads carry the expected retail word beside every
        # replacement. All words are checked before the kernel copies code or
        # writes a hook, so one mismatch leaves the game completely untouched.
        expected_words = []
        auth_checks = getattr(patches, "checks", [])
        authenticated = bool(auth_checks)
        for patch in patches.patches:
            expected = patch.get("expected")
            emitted = 1 + patch.get("nop_count", 0)
            if expected is None:
                expected_words.extend([None] * emitted)
                continue
            authenticated = True
            if isinstance(expected, int):
                expected = [expected]
            if len(expected) != emitted:
                raise ValueError(
                    "patch at {:#x} has {} emitted writes but {} expected words".format(
                        patch[args.vers], emitted, len(expected)))
            expected_words.extend(expected)

        manifest = json.loads(out.read_text())
        if authenticated:
            if any(value is None for value in expected_words):
                raise ValueError("authenticated manifests require an expected word for every write")
            if len(expected_words) != len(manifest["writes"]):
                raise RuntimeError("emitted write count and expected-word count disagree")
            manifest["writes"] = [
                [addr, expected, replacement]
                for (addr, replacement), expected in zip(
                    manifest["writes"], expected_words)
            ]
            manifest["checks"] = [
                [check["addr"], check["expected"]]
                for check in auth_checks
            ]
        out.write_text(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
