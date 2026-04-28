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
from pyisotools.bnrparser import BNR
from PIL import Image

#############
# Constants #
#############

BUILD_DIR = "build/"

# Environment keys
VERS = "VERS" # possible values: jp, pal, us
KURIBO_COMPILER_HOME = "KURIBO_COMPILER_HOME"

VERS_TO_ISO = {
    "jp": "GMSJ01",
    "us": "GMSE01",
    "pal": "GMSP01"
}

VERS_TO_REGION = {
    "jp": BNR.Regions.JAPAN,
    "us": BNR.Regions.AMERICA,
    "pal": BNR.Regions.EUROPE,
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

from util.dol_c_kit import Project, LDPlusPlus, ABI
from susamune.patches import *

def patch_dol(env, target, source):
    target = list(map(str, target))
    source = list(map(str, source))
    out_dol_path = target[0]
    in_dol_path = source[0]
    mod_obj_path = source[1]

    p = Project()
    p.verbose = True
    p.obj_dir = BUILD_DIR
    p.kuribo_compiler_home = env[KURIBO_COMPILER_HOME]
    p.code_pad = 32 * 1024 # Pad code size to 32K so we can swap DOL without re-creating ISO
    #p.linker_flags = env['LINKFLAGS']

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
    repo_root = Dir('#').abspath
    kuribo_compiler_home = os.environ.get('KURIBO_COMPILER_HOME', os.path.join(repo_root, 'util', 'kuribo_compiler'))
    
    if not os.path.exists(kuribo_compiler_home):
        print(f"Warning: user-specified Kuribo compiler path '{kuribo_compiler_home}' not found!")
        print("Please set KURIBO_COMPILER_HOME environment variable to the correct path")
    
    SHARED_FLAGS = [
        '--target=powerpc-gecko-ibm-kuribo-eabi',
        '-Os',
        '-fno-exceptions',
        '-fno-rtti',
        '-ffast-math',
        '-fpermissive',
        '-fno-unwind-tables',
        '-nodefaultlibs',
        '-nostdlib',
        '-fno-use-cxa-atexit',
        '-fno-c++-static-destructors',
        '-fno-function-sections',
        '-fno-data-sections'

    ]
    # Create base environment
    env = Environment(
        ENV=os.environ.copy(),
        CC=os.path.join(kuribo_compiler_home, 'clang'),
        CXX=os.path.join(kuribo_compiler_home, 'clang++'),
        LINK=os.path.join(kuribo_compiler_home, 'clang++'),
        OBJCOPY=os.path.join(kuribo_compiler_home, 'powerpc-eabi-objcopy'),
        
        CXXFLAGS= SHARED_FLAGS + ['-nobuiltininc','-nostdinc++','-std=c++17','-Werror','-Wno-main','-Wno-incompatible-library-redeclaration'],
        LINKFLAGS= SHARED_FLAGS + ['-fuse-ld=lld','-Werror'],
        
        # Include paths
        CPPPATH=['#susamune/include', '#susamune/include/JSystem', '#susamune/include/net'],

        COMPILATIONDB_USE_ABSPATH=True
    )
    env[KURIBO_COMPILER_HOME] = kuribo_compiler_home 
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
    iso_file = env.File(Path(iso_path))

    in_iso = env.Dir(to_out_path("in_iso/"))
    env.Command(
        target=in_iso,
        source=iso_file,
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

    obj_link_env = env.Clone(LINKFLAGS = env['LINKFLAGS'] + ['-r'], PROGSUFFIX='.o')
    mod_obj = obj_link_env.Program(target=to_out_path("susamune.o"), source=mod_objs) # TODO not using BUILD_DIR

    in_dol = env.Alias(in_iso_path + "/root/sys/main.dol", in_iso) # weird hack because the iso extraction generates files scons doesnt know about
    out_dol = env.File(Path(out_iso_path + "/root/sys/main.dol"))
    patched_dol = env.Command(
        target=out_dol,
        source=[in_dol, mod_obj, 'susamune/patches.py'],  
        action=patch_dol
    )
    # TODO: maybe the dol should be installed somewhere?
    #env.Install(f'dist/{mod_name}/', mod_elf)
    #env.Install(f'dist/{mod_name}/', patched_dol)

    return env.Alias("dol", [patched_dol,cdb])

def patch_bnr(env,target,source):
    target = to_path(target[0])
    source = to_path(source[0])
    bnr = BNR(Path(source), region=VERS_TO_REGION[env[VERS]])
    bnr.gameName = "susamune practice mod"
    bnr.gameDescription = "the best practice mod you will ever use"
    bnr.gameTitle = "susamune!"
    bnr.developerName = "2026   J"
    bnr.developerTitle = "2026   J"
    bnr.rawImage = Image.open("sms.bmp")
    bnr.save_bnr(Path(target))

def tg_out_bnr(in_iso,out_iso):
    in_iso_path,out_iso_path = to_path(in_iso), to_path(out_iso)

    in_bnr = env.Alias(in_iso_path + "/root/files/opening.bnr", in_iso)
    out_bnr = env.File(Path(out_iso_path + "/root/files/opening.bnr"))
    patched_bnr = env.Command(
        target=out_bnr,
        source=[in_bnr],
        action=patch_bnr
    )
    return env.Alias("bnr", [patched_bnr])

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

from util.dol_c_kit.cw_map_to_ldscript import main as map_to_ld_main
def map_to_ld(env,target,source):
    target = to_path(target[0])
    source = to_path(source[0])
    map_to_ld_main(source,target)

def regen_map():
    new_map = env.Command(
        target=f"#susamune/maps/{env[VERS]}.ld",
        source=[f"#susamune/maps/{env[VERS]}.map"],
        action=map_to_ld
    )
    env.Alias("regen_map", new_map)

def print_help():
    # TODO
    raise NotImplementedError

if 'help' in COMMAND_LINE_TARGETS:
    print_help()
else:
    regen_map()

    in_iso, out_iso = extract_iso()
    d1 = tg_out_dol(in_iso, out_iso)
    d2 = tg_out_bnr(in_iso, out_iso)
    
    pre_iso = env.Alias("pre_iso", [d1, d2])
    rebuild_iso(out_iso, pre_iso)