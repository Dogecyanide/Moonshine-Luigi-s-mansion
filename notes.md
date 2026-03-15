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