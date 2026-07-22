## Known savestate bugs 

- ~~Crash when loading on blue save screen after collecting shine~~
- ~~Timer in piantissimo levels etc. does not save and restore~~
- ~~Goop on the ground of the level is not saved and restored.~~ Not a real bug, just a misconfigured Dolphin emulator setting.
- ~~Crash during shine spawn?~~ Inconsistent. Pianta 3 still breaks on console only.
- ~~Crash when loading during death~~ 
- Delfino Piranha Plant #1 (bianco hills) crash: invalid read from 0x61f9f244, pc = 0x800bc614. Only on emulator for some reason?
- FLUDD sounds break when loading after shine spawn. Might not be fixable reasonably. Sort of inevitable since we don't completely save/restore audio state. Can also just reload the level.

## TODO

- [ ] [Bindings menu](#bindings)
- [ ] [SD card settings persistence](#sd-card-persistence)
- [ ] [Customizable GUI support](#customizable-gui)
- [ ] Gecko code porting (gecko_codes.md)
- [ ] Emulator autodetect.
- [ ] dont require different channels for different regions. load a bin from SD card
- [ ] Input tracing feature. Needs planning.
- [ ] Switch everything to a single Clang compiler (based on the kuribo tree) that does PowerPC and ARM to simplify dependencies.

### Bindings

TODO describe

Settings menu itself needs a bind. Default Y-Start. Savestate has a bind, default is d-pad left save d-pad right load (same as currently).

Either do 2 (3) gui buttons or have it record inputs. 


### SD card settings persistence

TODO describe

SD card save persistence / USB as backup.
- Saved popup
- OR failed save.

popup when loading saves.

### Customizable GUI

TODO describe