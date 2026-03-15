# SConstruct
import os
import sys
from pathlib import Path

# Environment setup
def setup_environment():
    """Setup the build environment with devkitPPC toolchain"""
    
    # Get devkitPPC path from environment or use fallback
    devkitppc_path = os.environ.get('DEVKITPPC', '/opt/devkitpro/devkitPPC')
    
    # Check if the path exists
    if not os.path.exists(devkitppc_path):
        print(f"Warning: devkitPPC path '{devkitppc_path}' not found!")
        print("Please set DEVKITPPC environment variable to the correct path")
    
    # Create base environment
    env = Environment(
        ENV=os.environ.copy(),
        CC=os.path.join(devkitppc_path, 'bin', 'powerpc-eabi-gcc'),
        CXX=os.path.join(devkitppc_path, 'bin', 'powerpc-eabi-g++'),
        AS=os.path.join(devkitppc_path, 'bin', 'powerpc-eabi-as'),
        AR=os.path.join(devkitppc_path, 'bin', 'powerpc-eabi-ar'),
        LINK=os.path.join(devkitppc_path, 'bin', 'powerpc-eabi-ld'),
        OBJCOPY=os.path.join(devkitppc_path, 'bin', 'powerpc-eabi-objcopy'),
        
        # -Wall', '-O2', '-mrvl', '-mcpu=750', '-meabi', '-mhard-float
        CCFLAGS=['-O1', '-fno-function-sections'],
        CXXFLAGS=['-std=c++17', '-fno-exceptions', '-fno-rtti'],
        LINKFLAGS=['-Os', '-Tutil/dollinker', '-Tutil/smsFuncs'],
        
        # Include paths
        CPPPATH=['#include'],
    )
    
    return env

def build_mod(target, source, env):
    """Builder for creating a mod ELF and patched DOL"""
    # This would be replaced with your actual patching logic
    print(f"Patching DOL for mod: {target[0]}")
    return 0

#def build_patch_tool(target, source, env):
#    """Builder for the patching tool itself"""
#    # This is just an example - replace with your actual tool compilation
#    import subprocess
#    cmd = f"{env['CXX']} -o {target[0]} {source[0]}"
#    return subprocess.call(cmd, shell=True)

# Define the mods and their source files
MODS = {
    'doublejump': {
        'main': ['src/doublejump/main.c'],
        'lib_files': []
    },
}

# Main build script
env = setup_environment()

# Create build directories
#build_dir = env.Dir('build')
#dist_dir = env.Dir('dist')

env.VariantDir('build', '.', duplicate=0)

# Check if we're building a specific mod
target_mods = []
if 'all' in COMMAND_LINE_TARGETS:
    target_mods = list(MODS.keys())
else: 
    for target in COMMAND_LINE_TARGETS:
        if target in MODS:
            target_mods.append(target)
    
#else:
#    # Default to first mod if none specified
#    if MODS:
#        target_mods = [list(MODS.keys())[0]]
#        print(f"No mod specified, building default: {target_mods[0]}")

# First, build the patching tool if needed
# patch_tool_src = 'tools/dol_patcher.cpp'  # Adjust path as needed
# patch_tool = env.Command(
#     target='build/tools/dol_patcher',
#     source=patch_tool_src,
#     action=build_patch_tool
# )
# env.AlwaysBuild(patch_tool)

# Build each requested mod
for mod_name in target_mods:
    mod = MODS[mod_name]
    
    # Collect all source files for this mod
    source_files = mod['main'] + mod['lib_files']
    
    # Create object files in build directory
    objects = []
    for src in source_files:
        # Convert source path to build path
        if src.startswith('lib/'):
            build_src = os.path.join('build', src)
        elif src.startswith('src/'):
            build_src = os.path.join('build', src)
        else:
            build_src = src
            
        obj = env.Object(build_src.replace('.cpp', '.o').replace('.c', '.o'), src)
        objects.append(obj)
    
    # Link mod object file
    mod_map = env.File(f"build/{mod_name}.map")
    mod_obj = env.Program(
        target=f'build/{mod_name}/obj_mod.elf', # TODO: "mod" ?
        source=objects,
        LINKFLAGS=env['LINKFLAGS'] + ['-Map',mod_map.path]
    )
    env.SideEffect(mod_map,mod_obj)
    
    mod_bin = env.Command(
        target=f'build/{mod_name}/mod.bin',
        source=mod_obj, 
        action=f"{env['OBJCOPY']} $SOURCE $TARGET -O binary -R .eh_frame -R .comment -R .gnu.attributes -g -S"
    )
    
    # Patch the base DOL with the mod ELF
    base_dol = 'main.dol'  # Adjust path to your base DOL
    patched_dol = env.Command(
        target=f'build/{mod_name}/main.dol',
        source=[mod_map, mod_bin, base_dol],  # , patch_tool
        # TODO: windoze
        action=[
            Copy("temp.bin", "${SOURCES[1]}"),
            "util\\DOLInsert.exe temp.bin -m ${SOURCES[0]} -dol ${SOURCES[2]} -o $TARGET -c OnUpdate:0x800f9b64:3 -c OnSetup:0x800ece3c:0 -c OnDraw2D:0x80206734:0 -c OnWaterHitsGround:0x8015ebf8:0 -c OnObjectTouchMario:0x801886d8:3 -c OnAllNPCsUpdate:0x80251d50:0 -c OnSmallEnemyHitMario:0x8027f64c:3 -c OnUpdateGameMode:0x800ec6c4:0 -r OnEMarioControl:0x80253ac0 -r IsMario:0x8012cfe0",
            Delete("temp.bin")
        ]
    )
    
    ## Add any additional files that need to be copied
    #env.Install(f'dist/{mod_name}/', mod_elf)
    #env.Install(f'dist/{mod_name}/', patched_dol)
    #

    # Create an alias for this mod
    env.Alias(mod_name, patched_dol)

# Add 'all' target
# TODO ???????? gpt
# env.Alias('all', list(MODS.keys()))

# Clean targets
# TODO ??
env.Clean('all', ['build', 'dist'])

env['LINKCOM'] = 'echo LINKING: $LINK $LINKFLAGS $SOURCES -o $TARGET && ' + env['LINKCOM']


# Print help
# TODO
def print_help():
    print("\nAvailable targets:")
    print("  scons mod1        - Build mod1")
    print("  scons mod2        - Build mod2")
    print("  scons all         - Build all mods")
    print("\nEnvironment variables:")
    print("  DEVKITPPC         - Path to devkitPPC (default: /opt/devkitpro/devkitPPC)")
    print("\nOutput directories:")
    print("  build/            - Intermediate build files")
    print("  dist/[mod]/       - Final mod files (ELF and patched DOL)")

if 'help' in COMMAND_LINE_TARGETS:
    print_help()
    Return()