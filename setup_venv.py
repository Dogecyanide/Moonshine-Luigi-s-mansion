import subprocess
import sys

if __name__ == '__main__':
    subprocess.run([sys.executable, '-m', 'venv', 'venv'])
    subprocess.run(["venv/bin/python", '-m', 'pip', 'install', '--upgrade', 'pip'])
    subprocess.run(["venv/bin/pip", 'install', '-r', 'requirements.txt'])
    print("venv created! source `venv/bin/activate`")