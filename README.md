# A mod for Super Mario Sunshine

## credits

- https://github.com/DotKuribo/BetterSunshineEngine/
    - used heavily as reference
- https://github.com/BitPatty/super-mario-sunshine-c-kit/tree/master

## Installation

TODO: but noting here that you need to put "Texture Cache Accuracy = Safe" in dolphin 

## Known savestate bugs 

- ~~Crash when loading on blue save screen after collecting shine~~
- ~~Timer in piantissimo levels etc. does not save and restore~~
- ~~Goop on the ground of the level is not saved and restored.~~ Not a real bug, just a misconfigured Dolphin emulator setting.
- Crash during shine spawn? Inconsistent. Pianta 3 still breaks on console only.
- Crash when loading during death 
- Delfino Piranha Plant #1 (bianco hills) crash: invalid read from 0x61f9f244, pc = 0x800bc614
- FLUDD sounds break when loading after shine spawn. Might not be fixable reasonably.

## TODO

- [ ] Add customized nintendont loader that loads the mod as a patch to a real disc or ISO. Removes the need to have an ISO at build time for console users.
- [ ] Add nintendont toolchains to repo for reproducible compilations.
- [ ] Integrate nintendont compilation with the cmake and create easy presets for console and emulator. 
- [ ] Remove nintendont dependency on make.
- [ ] Add a CI.
- [ ] Emulator autodetect.
- [ ] dont require different channels for different reason. just need to load project.bin from
- [ ] Input tracing feature. Needs planning.
- [ ] Switch everything to a single Clang compiler (based on the kuribo tree) that does PowerPC and ARM to simplify dependencies.