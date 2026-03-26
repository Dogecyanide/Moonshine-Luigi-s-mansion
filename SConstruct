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

#############
# Constants #
#############

BUILD_DIR = "build/"

# Environment keys
VERS = "VERS" # possible values: jp, pal, us
DEVKITPPC_PATH = "DEVKITPPC_PATH"

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
    p.add_obj_file("susamune.o") # TODO: stupid

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
cdb = env.CompilationDatabase(f"{BUILD_DIR}/compile_commands.json")

##########################
# Main build definitions #
##########################

def tg_out_dol(): 
    patched_dol = SConscript('susamune/SConscript', exports=['env', 'patch_dol'], variant_dir=f"{BUILD_DIR}/susamune", duplicate=False)
    
    # TODO: maybe the dol should be installed somewhere?
    #env.Install(f'dist/{mod_name}/', mod_elf)
    #env.Install(f'dist/{mod_name}/', patched_dol)

    env.Alias("dol", [patched_dol,cdb])

# TODO: define the available targets for help and create aliases here?


def print_help():
    # TODO
    raise NotImplementedError

if 'help' in COMMAND_LINE_TARGETS:
    print_help()
else:
    tg_out_dol()