# A mod for Super Mario Sunshine

## credits

- https://github.com/DotKuribo/BetterSunshineEngine/
    - used heavily as reference
- https://github.com/BitPatty/super-mario-sunshine-c-kit/tree/master


## savestates bugs 

- Timer in piantissimo levels etc. does not save and restore
- Crash when loading during death 
- Crash when loading on blue save screen after collecting shine
- Crash during shine spawn? Inconsistent
- looks like it skips a frame?/

## running todo

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