# coarse timeline/plan 2 

enough for me to practice nicely
* simple hardcoded level select menu 
* practice rom with different ID, so it stops conflicting with the normal rom
* vibe coded asl script reading memory addresses. should not be hard to find something that tells when we got a star.
    -> working POC but i did not finish
* integration in the asl script to remove the memory card automatically. maybe even a dolphin fork
    -> this did not work

cool things to demonstrate (in order)
* bba tcp/ip basic functionality
* upload trace files
    * (uncompressed at first. just upload when a button is pressed) 
* replay trace 
* ranked without replay upload? 

NOT DOING:
* the cool practice mod outlined below
* run-level igt - will just use realtime

# coarse timeline/plan

1. basic practice mod
    - support everything you can do with the gecko codes plus a ui to toggle them.
        - i think most of them should be easy but the really complicated ones like savestate we probably dont need to bother reverse engineering at this stage
    - a proper level select menu 
    - options for setting up save files incl. a way to set a default save file, so that i can do my runs on that
{
2. super practice mod
    - real savestates
    - Run-level IGT?
    - Input recordings?
} ||
{
3. networking
    - integrate libogc. basic ping test 
    - funny delfino plaza alert 
    - autosplitter integration?
} 
4. ranked
    - ??? search for divine inspiration

# uncategorized ideas

- ascii controller for ingame chat? use port 2
    - dolphin and nintendont both support normal keyboards as emulation for the ASCII contoller.
    - would be a cute way for people to call each other slurs in game.

## super practice/runs mod

- File A: Runs mode
- File B: Practice mode
- File C: online (coming soon) 

Runs mode: 
There are a couple settings related to the category of runs: (TBD how these will be saved, if it's practical)
- IGT end/display condition.
    - This also doubles as the autosplitter end condition, when it is enabled.
    - autosplitter just sends pings when shines are collected, and when an end condition is reached.
- Start file
    - Hacked file, peach file, full, etc.

The game ships with presets for each major category: any%, 120, 20 shine, bingo (?)
You can configure the default category on startup, otherwise it can just be an option at the screen where you start the run that is remembered.

In runs mode, every practice setting is disabled. Only the timer runs, and it is not shown until the very end.


## ranked thing 

start simple without ipfs thing just database
public keys for everyone? ask gpt if there are better



```
         IPFS?
           ^
           |
wii ->   server   <- wii
```

still uses client-server but it should always be possible for someone to get the server's data if the owner disappears and it shuts down.. but not necessarily _after_ it has already shut down (though that would be ideal if possible)

it would be great to have a real distributed architecture here and make it so that any person can just 'spin up' an instance of the server that is transparently added to a network and inherits all of the user data, but you have to trust that server.. perhaps there can still be an authorization to add new instances

in the meantime, this solution is easy and gives you the property that the data can still be transparently copied elsewhere. anyone running their own server would also not have to do any work to expose their data. it is convenient because none of this data is sensitive (i think?)

todo is what the threat model is here and how will people authenticate etc. will they do username/password? key pair? i like key pair but you kinda get fucked if you lose your memory card. 


# organization 

thoughts on new repo organization:

```
doc/ - documents 
tools/ - extra stuff that needs to be built 
include/ - shared include: dolphin, sms, jsystem
lib/ - implementation of libraries like the networking one
maps/ - map/ld files for sms
mods/ - 
    - susamune/ - my crappy practice mod
        - src/, include, etc.
    - ranked/ - whatever comes of my attempt to do this 
        - same folder structure as susamune
    - net_test/ - use as a testing ground for random networking crap
- util/ - same as current 
SConstruct - shared build logic for everything
```
can use similar MOD= logic we had before. use subfolders for any output though.


For now: 
```
doc/ - documents 
tools/ - extra stuff that needs to be built 
susamune/ - the folder where all the mod code goes 
- src/ - source for game
- include/ - throw contents of sunshineheaderinterface here 
- maps/ 
util/ - build scripts? maps?
```
Revisit if needs change 
