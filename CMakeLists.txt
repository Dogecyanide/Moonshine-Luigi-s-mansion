cmake_minimum_required(VERSION 3.21)

# --- Toolchain -------------------------------------------------------------
# The Kuribo clang fork must be selected before project() so CMake uses it as
# the compiler. Its default target isn't hosted, so the usual compiler check
# (which links a test executable) can't succeed; we assert it works instead.
# CMAKE_SYSTEM_NAME=Generic marks this as a bare-metal cross build, which keeps
# CMake off the host-Windows toolchain path (no RC compiler, no MSVC probing).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

if(NOT DEFINED KURIBO_COMPILER_HOME)
    if(DEFINED ENV{KURIBO_COMPILER_HOME})
        set(KURIBO_COMPILER_HOME "$ENV{KURIBO_COMPILER_HOME}")
    else()
        set(KURIBO_COMPILER_HOME "${CMAKE_CURRENT_SOURCE_DIR}/toolchain")
    endif()
endif()
set(KURIBO_COMPILER_HOME "${KURIBO_COMPILER_HOME}" CACHE PATH "Kuribo clang toolchain directory")

if(CMAKE_HOST_WIN32)
    set(_kuribo_exe ".exe")
endif()
set(CMAKE_CXX_COMPILER "${KURIBO_COMPILER_HOME}/clang++${_kuribo_exe}")
set(CMAKE_CXX_COMPILER_WORKS TRUE)

project(susamune CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "The Visual Studio generator is not supported. Configure with Ninja, e.g. `cmake -B build -G Ninja` or `cmake --preset default`.")
endif()

set(VENV_DIR "${CMAKE_SOURCE_DIR}/venv")
if(CMAKE_HOST_WIN32)
    set(VENV_PYTHON "${VENV_DIR}/Scripts/python.exe")
else()
    set(VENV_PYTHON "${VENV_DIR}/bin/python")
endif()
if(NOT EXISTS "${VENV_PYTHON}")
    message(FATAL_ERROR "Virtualenv not found at ${VENV_PYTHON}. Run `python setup_venv.py` first.")
endif()

# --- Options ---------------------------------------------------------------

set(VERS "jp" CACHE STRING "Game version to build against")
set_property(CACHE VERS PROPERTY STRINGS jp us pal)
set(SUPPORTED_VERS jp us pal)
if(NOT VERS IN_LIST SUPPORTED_VERS)
    message(FATAL_ERROR "VERS must be one of: ${SUPPORTED_VERS}")
endif()
string(TOUPPER "${VERS}" VERS_UPPER)

set(LAUNCHER_DEFAULT_SOURCE "di" CACHE STRING "Where to load the iso from by default in meta.xml")
set_property(CACHE LAUNCHER_DEFAULT_SOURCE PROPERTY STRINGS di sd usb)

option(IS_EMULATOR "Target Dolphin (ON) instead of Wii/Nintendont (OFF)" OFF)
option(ENABLE_SAVESTATE_DBG "Enable savestate debug output" OFF)
option(ENABLE_WARP_MENU "Enable warp menu" OFF)
option(UPDATE_ISO_METADATA "Update ISO metadata (game code 01 -> 02, change picture, name, etc.)" OFF)
option(LAUNCHER_DISABLE_SUSAMUNE_PATCH "Disable the Susamune patch in the launcher kernel" OFF)

set(ISO_CODE_jp GMSJ01)
set(ISO_CODE_us GMSE01)
set(ISO_CODE_pal GMSP01)
set(ISO_CODE "${ISO_CODE_${VERS}}")

if(DEFINED ENV{SMS_ISO})
    set(_sms_iso_default "$ENV{SMS_ISO}")
else()
    set(_sms_iso_default "${CMAKE_SOURCE_DIR}/${ISO_CODE}.iso")
endif()
set(SMS_ISO "${_sms_iso_default}" CACHE FILEPATH "Source game ISO to patch")

set(LD_SCRIPT "${CMAKE_SOURCE_DIR}/maps/${VERS}.ld")
set(MAP_FILE "${CMAKE_SOURCE_DIR}/maps/${VERS}.map")

# --- Mod object ------------------------------------------------------------
# Compile the sources and partial-link (-r) them into a single relocatable
# object that the DOL patcher later injects.

file(GLOB MOD_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

set(SHARED_FLAGS
    --target=powerpc-gecko-ibm-kuribo-eabi
    -Os
    -fno-exceptions
    -fno-rtti
    -ffast-math
    -fpermissive
    -fno-unwind-tables
    -nodefaultlibs
    -nostdlib
    -fno-use-cxa-atexit
    -fno-c++-static-destructors
    -fno-function-sections
    -fno-data-sections)

add_executable(susamune_obj ${MOD_SOURCES})
set_target_properties(susamune_obj PROPERTIES
    OUTPUT_NAME susamune_pre
    SUFFIX .o
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_include_directories(susamune_obj PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/JSystem")

target_compile_options(susamune_obj PRIVATE
    ${SHARED_FLAGS}
    -nobuiltininc
    -nostdinc++
    -std=c++17
    -Werror
    -Wno-main
    -Wno-incompatible-library-redeclaration)

target_compile_definitions(susamune_obj PRIVATE
    IS_EMULATOR=$<BOOL:${IS_EMULATOR}>
    ENABLE_SAVESTATE_DBG=$<BOOL:${ENABLE_SAVESTATE_DBG}>
    SUSAMUNE_VERSION_${VERS_UPPER}=1
    $<$<BOOL:${ENABLE_WARP_MENU}>:ENABLE_WARP_MENU>)

target_link_options(susamune_obj PRIVATE ${SHARED_FLAGS} -fuse-ld=lld -Werror -r)

set(MOD_OBJ "$<TARGET_FILE:susamune_obj>")

# --- Pipeline scripts (run from the venv) ----------------------------------

set(IN_ISO_DIR "${CMAKE_BINARY_DIR}/in_iso")
set(OUT_ISO_DIR "${CMAKE_BINARY_DIR}/out_iso")
set(IN_ISO_STAMP "${CMAKE_BINARY_DIR}/in_iso.stamp")
set(MANIFEST "${CMAKE_BINARY_DIR}/susamune_manifest.json")
set(PATCHED_DOL "${CMAKE_BINARY_DIR}/main.dol")
set(GECKO_TXT "${CMAKE_BINARY_DIR}/susamune.txt")
set(FINAL_ISO "${CMAKE_BINARY_DIR}/susamune_${VERS}.iso")
set(LAUNCHER_ZIP "${CMAKE_BINARY_DIR}/susamune_launcher_${VERS}.zip")

add_custom_command(
    OUTPUT "${IN_ISO_STAMP}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/extract_iso.py" "${SMS_ISO}" "${IN_ISO_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${IN_ISO_STAMP}"
    DEPENDS "${SMS_ISO}" "${CMAKE_SOURCE_DIR}/scripts/extract_iso.py"
    COMMENT "Extracting ${SMS_ISO}"
    VERBATIM)

add_custom_command(
    OUTPUT "${MANIFEST}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS}"
        --out "${MANIFEST}"
        launcher
    DEPENDS susamune_obj "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/scripts/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
    COMMENT "Generating mod manifest"
    VERBATIM)
add_custom_target(manifest ALL DEPENDS "${MANIFEST}")

# FIXME: This should also copy to OUT_ISO_DIR/root/sys/main.dol. Otherwise, the vscode tasks (run in dolphin) for development
# will break, because the dol won't be up to date unless we also build the iso target. 
add_custom_command(
    OUTPUT "${PATCHED_DOL}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS}"
        --in-dol "${IN_ISO_DIR}/root/sys/main.dol"
        --out "${PATCHED_DOL}"
        patch_dol
    DEPENDS susamune_obj "${IN_ISO_STAMP}" "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/scripts/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
    COMMENT "Patching main.dol"
    VERBATIM)
add_custom_target(dol DEPENDS "${PATCHED_DOL}")

# NOTE: gecko code target is currently broken, it doesn't seem to work in-game. But it is left here anyway.
add_custom_command(
    OUTPUT "${GECKO_TXT}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS}"
        --out "${GECKO_TXT}"
        gecko
    DEPENDS susamune_obj "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/scripts/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
    COMMENT "Generating Gecko code list"
    VERBATIM)
add_custom_target(gecko DEPENDS "${GECKO_TXT}")

add_custom_command(
    OUTPUT "${FINAL_ISO}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/build_iso.py"
        --update-metadata "${UPDATE_ISO_METADATA}"
        --in-iso "${IN_ISO_DIR}"
        --out-iso "${OUT_ISO_DIR}"
        --vers "${VERS}"
        --dol "${PATCHED_DOL}"
        --output "${FINAL_ISO}"
    DEPENDS "${PATCHED_DOL}" "${IN_ISO_STAMP}"
            "${CMAKE_SOURCE_DIR}/scripts/build_iso.py"
    COMMENT "Building ${FINAL_ISO}"
    VERBATIM)
add_custom_target(iso DEPENDS "${FINAL_ISO}")

# The launcher (custom Nintendont) is a CMake super-build under launcher/. It's a
# separate build tree because it spans three cross toolchains (devkitARM,
# devkitPPC, devkitPPC+libogc) that CMake can't mix with the Kuribo clang used
# here. We drive it as an ExternalProject.
#
# First, the mod's inject header becomes a real build artifact: generating it from
# the manifest, and pointing the launcher's kernel at it, makes a mod-source edit
# relink only the kernel + loader instead of rebuilding the whole launcher.
include(ExternalProject)

set(INJECT_HEADER "${CMAKE_BINARY_DIR}/susamune_inject.h")
add_custom_command(
    OUTPUT "${INJECT_HEADER}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/gen_inject_header.py"
        "${MANIFEST}" -o "${INJECT_HEADER}"
    DEPENDS "${MANIFEST}" "${CMAKE_SOURCE_DIR}/scripts/gen_inject_header.py"
    COMMENT "Generating susamune_inject.h"
    VERBATIM)
add_custom_target(inject_header DEPENDS "${INJECT_HEADER}")

ExternalProject_Add(nintendont_launcher
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/launcher"
    BINARY_DIR "${CMAKE_BINARY_DIR}/launcher"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DLAUNCHER_PYTHON=${VENV_PYTHON}"
        "-DLAUNCHER_SCRIPTS_DIR=${CMAKE_SOURCE_DIR}/scripts"
        "-DSUSAMUNE_INJECT_DIR=${CMAKE_BINARY_DIR}"
        "-DSUSAMUNE_MANIFEST=${MANIFEST}"
        "-DLAUNCHER_OUTPUT_ZIP=${LAUNCHER_ZIP}"
        "-DLAUNCHER_DEFAULT_SOURCE=${LAUNCHER_DEFAULT_SOURCE}"
        "-DLAUNCHER_DISABLE_SUSAMUNE_PATCH=${LAUNCHER_DISABLE_SUSAMUNE_PATCH}"
    DEPENDS inject_header
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
    EXCLUDE_FROM_ALL TRUE)

add_custom_target(launcher DEPENDS nintendont_launcher)

# Regenerate the checked-in linker script from the CodeWarrior map. Run on
# demand (`cmake --build build --target regen_ld`) after the map changes.
add_custom_target(regen_ld
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py" "${MAP_FILE}" "${LD_SCRIPT}"
    COMMENT "Regenerating ${LD_SCRIPT} from ${MAP_FILE}"
    VERBATIM)

add_custom_target(regen_all_ld
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py"
        "${CMAKE_SOURCE_DIR}/maps/jp.map" "${CMAKE_SOURCE_DIR}/maps/jp.ld"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py"
        "${CMAKE_SOURCE_DIR}/maps/us.map" "${CMAKE_SOURCE_DIR}/maps/us.ld"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py"
        "${CMAKE_SOURCE_DIR}/maps/pal.map" "${CMAKE_SOURCE_DIR}/maps/pal.ld"
    COMMENT "Regenerating linker scripts for all supported versions"
    VERBATIM)
