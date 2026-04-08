# A mod for Super Mario Sunshine

## credits

- https://github.com/DotKuribo/BetterSunshineEngine/
    - used heavily as reference
- https://github.com/BitPatty/super-mario-sunshine-c-kit/tree/master


## running todo

- [x] setup vscode workspace of smsdecomp and bettersms
- [ ] write notes on knowledge of warp system etc. 
- [x] fix the labeling of the existing stage menus. should offset stage id by +0x2.
- [x] instant level warp 
- [ ] non main stage menus. follow logic in asm code gist
- [ ] comprehensive test of every warp supported by the gecko code
- [ ] fix wonky logic in changeStageHook. when are things active etc.
- [ ] proper initialization/de-initialization, concerns with settings menu being re-inited for every onSetup
- [ ] settings menu pauses the game
- [ ] Clean up kuribo_compiler files, remove unecessary ones. Add build scripts in the repo to be able to build those tools from scratch. Ideally for multiple platforms.
- [ ] Clean up the map conversion script so that it only parses instead of attempting to remangle, because that is no longer necessary.
- [ ] Clean up devkit_tools. Are we kuribo_compiler or devkitppc still? Should we add the compile part back or just strip it out since it's not necessary for the project? Should we remove codewarrior stuff?
- [ ] some kind of unit testing framework?? hmm..