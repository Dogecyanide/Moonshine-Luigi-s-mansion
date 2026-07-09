# A mod for Super Mario Sunshine

## credits

- https://github.com/DotKuribo/BetterSunshineEngine/
    - used heavily as reference
- https://github.com/BitPatty/super-mario-sunshine-c-kit/tree/master

## Installation

TODO: but noting here that you need to put "Texture Cache Accuracy = Safe" in dolphin 

## savestates bugs 

- ~~Crash when loading on blue save screen after collecting shine~~
- ~~Timer in piantissimo levels etc. does not save and restore~~
- Crash during shine spawn? Inconsistent. Pianta 3 still breaks on console only.
- Crash when loading during death 
- Goop on the ground of the level is not saved and restored. 
    - Actually, it is, but it may or may not be updated graphically (claude says its some cache invalidation problem for GX).
- Delfino Piranha Plant #1 (bianco hills) crash: invalid read from 0x61f9f244, pc = 0x800bc614
- FLUDD sounds break when loading after shine spawn. might not be fixable reasonably.

## TODO

- [ ] Switch to newer nintendont - will have to integrate networking stuff back in (or maybe not?)
- [ ] Patch on the fly with changes to nintendont rather than needing the ISO at build time
- [ ] update nintendont devkitppc devkitARM versions?
- [ ] new build system cause scons is a lost cause (msvc thing) - some kind of configure.py with jinja build.ninja template
- [ ] see if we can validate nintendont ram
- [ ] warp menu pauses the game
- [ ] Trace feature - can start with just saving to SD card
- [ ] Clean up kuribo_compiler files, remove unecessary ones. Add build scripts in the repo to be able to build those tools from scratch. Ideally for multiple platforms.
- [ ] Clean up the map conversion script so that it only parses instead of attempting to remangle, because that is no longer necessary.
- [ ] Clean up devkit_tools. Are we kuribo_compiler or devkitppc still? Should we add the compile part back or just strip it out since it's not necessary for the project? Should we remove codewarrior stuff?
- [ ] some kind of unit testing framework?? hmm..
- [ ] autodetect emulator