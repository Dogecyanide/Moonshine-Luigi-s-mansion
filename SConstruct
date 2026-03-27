# SConstruct
import os
import sys
from pathlib import Path
from dataclasses import dataclass
from enum import Enum

try:
    import elftools, dolreader, geckolibs
except Exception as e:
    print("Failed to import dependencies. Did you set up the virtual environment correctly?",file=sys.stderr)
    raise e

from pyisotools.iso import GamecubeISO

#############
# Constants #
#############

BUILD_DIR = "build/"

# Environment keys
VERS = "VERS" # possible values: jp, pal, us
DEVKITPPC_PATH = "DEVKITPPC_PATH"

VERS_TO_ISO = {
    "jp": "GMSJ01",
    "us": "GMSE01",
    "pal": "GMSP01"
}

#############
# UTILITIES #
#############

# TODO: use the native Pathlib instead? maybe scons already supports it.
def to_path(node):
    return str(node)

def to_paths(nodes):
    return list(map(str,nodes))

def to_out_path(node):
    return BUILD_DIR + str(node)

def to_out_paths(nodes):
    return list(map(lambda n: BUILD_DIR + str(n), nodes))

################
# DOL patching #
################

from util.dol_c_kit import Project, Compiler, Assembler, Linker
from susamune.patches import *

def patch_dol(env, target, source):
    target = list(map(str, target))
    source = list(map(str, source))
    out_dol_path = target[0]
    in_dol_path,mod_obj_path = source

    p = Project()
    p.verbose = True
    p.obj_dir = BUILD_DIR
    p.devkitppc_path = env[DEVKITPPC_PATH] + "/bin/"
    p.linker_flags = env['LINKFLAGS']
    
    p.add_linker_script_file(f"susamune/maps/{env[VERS]}.ld")
    p.add_obj_file("susamune.o") # TODO: hardcoded

    for patch in patches:
        if patch['type'] == PatchType.B:
            p.hook_branch(patch[env[VERS]], patch['sym'])
        elif patch['type'] == PatchType.BL:
            p.hook_branchlink(patch[env[VERS]], patch['sym'], nop_count = patch.get('nop_count', 0))
        elif patch['type'] == PatchType.W32: 
            p.hook_word(patch[env[VERS]], patch['val'])
        else:
            pass
        
    p.build_dol(in_dol_path, out_dol_path)

###############
# Environment #
###############

def setup_environment():
    
    devkitppc_path = os.environ.get('DEVKITPPC', '/opt/devkitpro/devkitPPC')
    
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
        LINKFLAGS=['-Os'],
        
        # Include paths
        CPPPATH=['#susamune/include', '#susamune/include/JSystem'],

        COMPILATIONDB_USE_ABSPATH=True
    )
    env[DEVKITPPC_PATH] = devkitppc_path
    env[VERS] = "jp" # LATER: other versions
    
    return env

env = setup_environment()
env.Tool('compilation_db')
cdb = env.CompilationDatabase(to_out_path("compile_commands.json"))

##########################
# Main build definitions #
##########################

def extract_iso():
    # TODO: use the scons environment, more idiomatic?
    iso_path = os.environ.get("SMS_ISO", f"#{VERS_TO_ISO[env[VERS]]}.iso")

    in_iso = env.Dir(to_out_path("in_iso/"))
    env.Command(
        target=in_iso,
        source=iso_path,
        action=lambda env,target,source: \
            GamecubeISO.extract_from(Path(iso_path), Path(to_path(in_iso)))
    )

    out_iso = env.Dir(to_out_path("out_iso/"))
    if not os.path.exists(out_iso):
        env.Command(
            target=out_iso,
            source=in_iso,
            action=Copy('${TARGET}', '${SOURCE}'),
        )

    return in_iso, out_iso

def tg_out_dol(in_iso, out_iso): 
    in_iso_path,out_iso_path = to_path(in_iso), to_path(out_iso)
    source_files = Glob('susamune/src/*.c') + Glob('susamune/src/*.cpp')
    mod_objs = env.Object(source=source_files)

    obj_link_env = env.Clone(LINKFLAGS = env['LINKFLAGS'] + ['-r'])
    mod_obj = obj_link_env.Program(target=to_out_path("susamune.o"), source=mod_objs) # TODO not using BUILD_DIR

    in_dol_path = in_iso_path + "/root/sys/main.dol"
    out_dol_path = out_iso_path + "/root/sys/main.dol"    
    patched_dol = env.Command(
        target=out_dol_path,
        source=[in_dol_path, mod_obj],
        action=patch_dol
    )
    # TODO: maybe the dol should be installed somewhere?
    #env.Install(f'dist/{mod_name}/', mod_elf)
    #env.Install(f'dist/{mod_name}/', patched_dol)

    return env.Alias("dol", [patched_dol,cdb])

def rebuild_iso(out_iso, pre_iso):
    susamune_iso_name = f"susamune_{env['VERS']}.iso"
    susamune_iso = env.Command(
        target=f"#{susamune_iso_name}",
        source=[out_iso,pre_iso],
        action=
            [lambda env,target,source: \
                GamecubeISO.build_root(Path(to_path(out_iso) + "/root"), susamune_iso_name),
                Move("$TARGET", to_path(out_iso) + "/root/" + susamune_iso_name)
            ]
    )

    env.Alias("iso", susamune_iso)

# TODO: define the available targets for help and create aliases here?


def print_help():
    # TODO
    raise NotImplementedError

if 'help' in COMMAND_LINE_TARGETS:
    print_help()
else:
    in_iso, out_iso = extract_iso()
    d1 = tg_out_dol(in_iso, out_iso)
    
    pre_iso = env.Alias("pre_iso", [d1])
    rebuild_iso(out_iso, pre_iso)