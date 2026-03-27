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

## ranked thing 

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