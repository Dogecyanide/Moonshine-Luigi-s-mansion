## 2026-03-26

to handle the iso stuff, can put build/iso - extracted ISO that does not change
then build/overlay - all the stuff we output, mirroring the directory structure of the ISO
the target that builds the ISO either creates another directory or applies these patches on the fly
actually everything should probably just udpate a build/out_iso directory that way we're not constantly copying the entire iso


pacman -S \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-python \
    mingw-w64-x86_64-python-pip \
    mingw-w64-x86_64-python-setuptools

pacman -S \
    mingw-w64-ucrt-x86_64-libjpeg-turbo \
    mingw-w64-ucrt-x86_64-zlib \
    mingw-w64-ucrt-x86_64-libtiff \
    mingw-w64-ucrt-x86_64-freetype \
    mingw-w64-ucrt-x86_64-lcms2 \
    mingw-w64-ucrt-x86_64-libwebp \
    mingw-w64-ucrt-x86_64-openjpeg2 \
    mingw-w64-ucrt-x86_64-libimagequant \
    mingw-w64-ucrt-x86_64-libraqm \
    mingw-w64-ucrt-x86_64-libavif


## 2026-03-25
 
dependencies would be just:
- meson (or meson can just be installed in the virtualenv?? for ides, set venv/bin/meson in vscode settings as meson path)
- ninja 
- python: virtualenv 
- devkitppc

Pros of kuribo:
- Patching goes in the code - no external build scripts with a lot of the logic 
    - This may be possible to accomplish with build scripts as well? I.e. a header file with the addresses and the mappings to the patch functions.
- Theoretically compatible with other mods.. but would this project really go with other mods?
    - If each mod is presumably going to try and modify the game loop, how could they hope to be compatible?
    - Does the engine call each patch in order? But each patched function is going to try and call the original, so how does that work?
    - In theory it would be nice, if very niche, to have all of this work on SMS hacks. Integrating with kuribo would make that easier, but anyway it's extremely far fetched
- GeckoJIT - can just copy the practice gecko codes and turn them on and off dynamically.
    - On the other hand, they seem simple enough to recreate. The quarterframe timer I am probably going to want to implement myself at some point,.
    - Patching them statically is also easy thanks to dol_c_kit.

Cons of kuribo:
- Forced to use CMake
- Have to depend on a bunch of code that I have to make modifications to to support JP and SMS
- I either have to work on upstreaming it, or keep it to myself and then when he updates it will be hell for me to integrate if there's anything useful
- Not a very active project so not clear if he's really working on this still

Pros of not using kuribo:
- (all of the cons above)
- can use meson build system
- can still use the sunshine c headers, i will just integrate them into my own project and give credit 
- When patching the binary instead of loading in via kuribo, probably easier to integrate devkitPPC's libraries? 

0x80426b64
0x804240e0 start of kernel in heap
... not reading the full thing?

## old

https://github.com/JoshuaMKW/dolreader.git


0x7F75BCA8


Note: HxD can start the comparison again after an offset if you just move your cursor
for when things are inserted

OSProtectRange 0x803463DC
Call to replace at 0x802a73f0 [0x800fadf4], 0x802a7404 [0x800fae08] (US) [JP]
hook_string with 0x60000000 (nop) as a string i guess?

JKRExpHeap::createRoot call to replace at 0x802a744c [0x800fae50] (US)

> In that hook, I accomplish a few things.
> 
> I call the original method JKRExpHeap::createRoot(int, bool)
> I call DVDOpen with the path to the Kuribo kernel binary
> I call DVDReadPrio on the file handle to read it into memory
> I call the Kuribo kernel entry point to load modules in








OSFatal: error handling

why not use bettersunshineengine? cause he doesnt support jp and to even compile it i'd have to fill in all those addresses
also because i dont want to inherit changes i dont know about 


TMenuDirector::direct
800f67a8

 -c OnInitApp:0x8000561c:0

 useful for injecting stuff that _actually_ initializes when the app is

# random 

- distance between original sms code (even at the start) and injected text2 is less than 24-bits... is there really a problem with relocations? why are we getting that error?

# how the injection works 

- Find a function call in the function you want to inject in 
- Add an argument to DOLInject saying the name of the function to call instead, the PC of the callsite, and the number of nops to replace before the instruction (?)
- For example: `-c OnUpdate:0x800f9b64:3` Insert 3 nops starting at 0x800f9b64, then 0x800f9b70 (3 instructions later), call `OnUpdate`.
- In this case in particular, 0x800f9b70 is where `gameLoop` calls `mDirector->direct()`. So it is the main call to the game update loop. 
- The call inserted by DOLInsert is just a direct branch and link, because it turns out the text2 segment fits in the 24-bit range just fine. No trampoline required.
- It also does not have to worry about volatile registers or saving context (i think), because it is replacing a function call. The compiler has already worried about saving volatile registers that may get overridden.
- A varying number of nops are required because some calls, like those required for dynamic dispatch, require loading the link register from other registers. For example, in the same OnUpdate example, this is the code before and after:

before:
```
lwz r12, 0(r3)
lwz r12, 0x64(r12)
mtlr r12
blrl 
```

after:
```
nop 
nop 
nop 
bl 0x80417800
```

- I do not know what "-c" and "-r" do just yet.
- The DOLInsert just replaces a function call and does not worry about putting anything back, so that is why the user is responsible for calling the function in their own thing.
- would also be stupid to do it a different way because you need the pointers to the local variables or the current class etc. in the functions you inject. not everything is going to be accessible via global vars.


=> how does kuribo work??
=> how does it link against symbols?
    => takes a map file in "KuriboConverter.exe"  (we should be able to generate one for jp version...)


we take sunshine_header_interface, which in turn uses kuribo
we depend on both 
use converter in kuribo 

how do you patch other files with kuribo? non-executable 

switch to cmake sadge ...