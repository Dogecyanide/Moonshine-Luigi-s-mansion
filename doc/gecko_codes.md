In general, we try to make it clear from which code the functionality comes from using **bold text**. Sometimes there are multifunction codes like DPad functions; we name exactly the feature we are talking about within that code when that is the case.   

# Simple On/Off Toggles or Select Options

The following Gecko codes either force some behavior statically, that should become built into the mod and toggleable dynamically, or they have some in-game settings to turn on and off with a bind that should instead become a toggle in the menu alongside the static codes. When it is not obvious we describe what those menu options should be. None of these will have binds even if the original gecko codes do.

- **Any Fruit Opens Yoshi Eggs**
- **D-Pad functions**: Fast Text/Restore Dialog Boxes
    - D-Pad Up replaces all text with "!!!" and D-Pad down reverts to normal text. This should just be one toggle in the menu and no longer have a bind.
- **D-Pad functions**: FLUDD in secrets 
    - X+D-Pad Left forces no FLUDD in secrets, X + D-Pad Right forces FLUDD in all secrets, and X+D-Pad Down makes FLUDD appear in completed secrets (default). Each of these FLUDD setting keybinds should become options in a 3-state setting in the menu.
- **Deathless Blooper Surfing**
- **Disable Blue Coin Flag**
- **Enable Exit Area Everywhere**
- **FMV Skips**
- **Fast Piantissimo**
- **Force Plaza Events**
- **Free Pause**
- **Fruit Never Time Out**
- **Infinite Juice**
- **Infinite lives**
- **Intro Skip**
    - This code does not make a lot of sense in case it is being implemented without settings persistence (not implemented at the time of writing), but it will be useful when that is implemented. It should still have a menu option like all the others. 
- **Mute Background Music**
- **Never Pause IGT**
- **No Shine Get Animation**
- **Nozzle lock**
    - B + D-Pad Left locks to the Rocket Nozzle; B + D-Pad Right locks to the Turbo Nozzle; B + D-Pad Up locks to the Hover Nozzle; and B + D-Pad Down releases the lock. 
    - This should instead be one setting in the menu "Nozzle Lock" with 4 options: Unlocked, Rocket, Turbo, Hover.
- **Replace episode names with their ID**
- **Respawn one-time shines**
- **Shadow Mario HP Meter**
- **Shine Outfit**
- **Shiny Shines**
- **Stage Intro Skip**
- **Unlock Nozzles**
- **Unlock yoshi**

# Simple Actions/Binds

These are codes that just do something in-game when you press a button like spawning an object. They don't have any menu settings, but their bind should be configurable in the menu. We list the default button binding they should have.

- **D-Pad Functions**: Regrab Last Held Object (Default X + D-Pad Up)
- **Spawn Yoshi**: 
    - One bind for each color of yoshi. Defaults:
        - Orange -> Y + D-Pad Left
        - Purple -> Y + D-Pad Right
        - Pink -> Y + D-Pad Down
        - Green -> Y + D-Pad Up 
- **Fast Forward**: Default binds:
    - 4x speed -> B + D-Pad Left
    - 8x speed -> B + D-Pad Right

# Complex Features

These features may have binds, menu settings, and/or their own GUI elements, and may take bits from multiple codes.

## Warp Wheel ({Instant} Level Select)

TBD

Stages 

Cap at 8. Less than 8 should definitely not fill the whole circle (align to quadrants).
More than 8 TBD.

L / R to different pages when there are more than 8.

area lock / other edge cases with level select code.

Instant restart -> quick 
Instant restart through level select -> longer

## Level Restart

There are 3 relevant ways to restart the stage in the original gecko codes:

1. **Instant Restart**: The quickest way to restart the level, and only works in the same area/subarea because it skips the full loading process.
2. **Instant Level Select**: Restart with Z + B + D-Pad Up: instantly does a warp to the same area as the current (i.e. without having to go into the pause menu as you would with level select)
3. **Instant Level Select**: Restart with Y + B + D-Pad Up: instantly does a warp to the _last selected warp_

The mod should replicate the behavior of these 3 methods, with 3 actions configurable with binds. These are the names they should have in the menu and their default bind:

1. Instant Restart: B + D-Pad Up
2. Full Restart: Z + B + D-Pad Up
3. Warp to Last Selected: Y + B + D-Pad Up

In addition, you should implement the Area Lock feature of **Instant Level Select**. This will make all warps restart the current area instead of sending Mario to other areas. (Read the documentation of the code for more details). This should be an on/off toggle setting in the menu.

## Pattern Selector

TODO

- Pattern Selector -> change the RNG pattern in the settings menu. Also an option to turn on and off a HUD outside the settings (This is customizable with the webpage same as currently.) 

## Attempt Counter

TODO

binds:
- Increase Stage Attempt Counter by one
- Increase and Decrease Manual Attempt Counter by one 

## Lite Savestate

TODO

Absorbs all the various 'savestate' codes (and the savestate function in D-Pad functions). Each of these flags is an on/off toggle in the setting. 

- Lite Savestate: Position
- Coin 
- Red Coin
- IGT 
- become flags
- Lite savestate has its own bind

## QFT & QSFT

TODO

- Misc timer for QFT. has its own page of settings.

## Controller Display & Customized Display

These both have a toggle code in the settings and implement custom GUIs.

TODO

# Miscellaneous

- **Fix Manta Splitting**: This code fixes a bug in Nintendont for the manta episode. This should just be built into the mod and always on.
- **Force {ANSI,SJIS} Memory Card Encoding**: Ideally, the game could just autodetect memory card encoding and be compatible with both. Study these codes and determine, before implementing, if it is possible to implement this autodetection and inter-compatibility feature.