"""Package the built Nintendont loader into the HBC app zip:
susamune_launcher_<region>/{boot.dol, icon.png, meta.xml}.

meta.xml is rendered from launcher/meta.xml.j2 (jinja2) with the git short hash
and the region/disc metadata carried in the mod manifest. This is the tail end
of the old build_launcher.py (render_meta + make_zip); the compilation it used
to drive is now done by CMake.

Usage: package_launcher.py --manifest M.json --boot-dol boot.dol \
                           --out-zip out.zip [--source di|sd|usb]
"""
import argparse
import json
import subprocess
import sys
import zipfile
from pathlib import Path

LAUNCHER_DIR = Path(__file__).resolve().parent.parent / "launcher"
META_TEMPLATE = LAUNCHER_DIR / "meta.xml.j2"
APP_NAME_PREFIX = "susamune_launcher"
APP_ICONS = {
    "JP": LAUNCHER_DIR / "icon_jp.png",
    "US": LAUNCHER_DIR / "icon_us.png",
    "PAL": LAUNCHER_DIR / "icon_pal.png",
}


def render_meta(manifest, source):
    import jinja2
    try:
        version = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(LAUNCHER_DIR.parent), text=True).strip()
    except Exception:
        version = "unknown"
    template = jinja2.Template(META_TEMPLATE.read_text())
    return template.render(
        region=manifest["region"],
        version=version,
        disc_name=manifest["disc_name"],
        source=source,
    )


def main(argv):
    ap = argparse.ArgumentParser(description="Package the Nintendont launcher HBC app zip.")
    ap.add_argument("--manifest", required=True, help="Mod manifest JSON from link_mod.py launcher")
    ap.add_argument("--boot-dol", required=True, help="Built loader boot.dol")
    ap.add_argument("--out-zip", required=True, help="Output HBC app zip")
    ap.add_argument("--source", default="di", choices=["di", "sd", "usb"])
    args = ap.parse_args(argv)

    manifest = json.loads(Path(args.manifest).read_text())
    region = manifest["region"]
    app_name = f"{APP_NAME_PREFIX}_{region.lower()}"
    meta_xml = render_meta(manifest, args.source)

    with zipfile.ZipFile(args.out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(args.boot_dol, f"{app_name}/boot.dol")
        z.write(APP_ICONS[region], f"{app_name}/icon.png")
        z.writestr(f"{app_name}/meta.xml", meta_xml)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
