import subprocess
import sys

if __name__ == '__main__':
    subprocess.run([sys.executable, '-m', 'venv', 'venv'])
    VENV_BIN = "venv/bin"
    if sys.platform == "win32":
        VENV_BIN = "venv/Scripts"
    subprocess.run([f"{VENV_BIN}/python", '-m', 'pip', 'install', '--upgrade', 'pip'])
    subprocess.run([f"{VENV_BIN}/pip", 'install', '--no-dependencies', '-r', 'requirements.txt'])
    # because nothing works on msys apparently....
    subprocess.run([f"{VENV_BIN}/pip", "install", "--no-dependencies", "--upgrade", "Pillow", "-C", "zlib=disable", "-C", "jpeg=disable", "-C", "tiff=disable", "-C", "freetype=disable", 
        "-C", "raqm=disable", "-C", "lcms=disable", "-C", "webp=disable", "-C", "jpeg2000=disable", "-C", "imagequant=disable", "-C", "xcb=disable", "-C", "avif=disable"])

    print(f"venv created!") #source `{VENV_BIN}/activate`