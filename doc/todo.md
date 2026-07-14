## Known savestate bugs 

- ~~Crash when loading on blue save screen after collecting shine~~
- ~~Timer in piantissimo levels etc. does not save and restore~~
- ~~Goop on the ground of the level is not saved and restored.~~ Not a real bug, just a misconfigured Dolphin emulator setting.
- ~~Crash during shine spawn?~~ Inconsistent. Pianta 3 still breaks on console only.
- ~~Crash when loading during death~~ 
- Delfino Piranha Plant #1 (bianco hills) crash: invalid read from 0x61f9f244, pc = 0x800bc614. Only on emulator for some reason?
- FLUDD sounds break when loading after shine spawn. Might not be fixable reasonably. Sort of inevitable since we don't completely save/restore audio state. Can also just reload the level.

## TODO

- [X] Add customized nintendont loader that loads the mod as a patch to a real disc or ISO. Removes the need to have an ISO at build time for console users.
- [X] Add nintendont toolchains to repo for reproducible compilations.
- [X] Integrate nintendont compilation with the cmake and create easy presets for console and emulator. 
- [ ] Remove nintendont dependency on make.
- [ ] Add a CI.
- [ ] Emulator autodetect.
- [ ] dont require different channels for different regions. load a bin from SD card
- [ ] Input tracing feature. Needs planning.
- [ ] Switch everything to a single Clang compiler (based on the kuribo tree) that does PowerPC and ARM to simplify dependencies.