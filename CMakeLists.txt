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
        set(KURIBO_COMPILER_HOME "${CMAKE_CURRENT_SOURCE_DIR}/util/kuribo_compiler")
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

option(SUSAMUNE_EMULATOR "Target Dolphin (ON) instead of Wii/Nintendont (OFF)" ON)
option(SUSAMUNE_SAVESTATE_DBG "Enable savestate debug output" OFF)
option(SUSAMUNE_SAVESTATES_ONLY "Build the savestate-only variant" OFF)

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

set(LD_SCRIPT "${CMAKE_SOURCE_DIR}/susamune/maps/${VERS}.ld")
set(MAP_FILE "${CMAKE_SOURCE_DIR}/susamune/maps/${VERS}.map")

# --- Mod object ------------------------------------------------------------
# Compile the sources and partial-link (-r) them into a single relocatable
# object that the DOL patcher later injects.

file(GLOB MOD_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/susamune/src/*.cpp")

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
    OUTPUT_NAME susamune
    SUFFIX .o
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_include_directories(susamune_obj PRIVATE
    "${CMAKE_SOURCE_DIR}/susamune/include"
    "${CMAKE_SOURCE_DIR}/susamune/include/JSystem"
    "${CMAKE_SOURCE_DIR}/susamune/include/net")

target_compile_options(susamune_obj PRIVATE
    ${SHARED_FLAGS}
    -nobuiltininc
    -nostdinc++
    -std=c++17
    -Werror
    -Wno-main
    -Wno-incompatible-library-redeclaration)

target_compile_definitions(susamune_obj PRIVATE
    SUSAMUNE_EMULATOR=$<BOOL:${SUSAMUNE_EMULATOR}>
    SUSAMUNE_SAVESTATE_DBG=$<BOOL:${SUSAMUNE_SAVESTATE_DBG}>
    $<$<BOOL:${SUSAMUNE_SAVESTATES_ONLY}>:SUSAMUNE_SAVESTATES_ONLY>)

target_link_options(susamune_obj PRIVATE ${SHARED_FLAGS} -fuse-ld=lld -Werror -r)

set(MOD_OBJ "$<TARGET_FILE:susamune_obj>")

# --- Pipeline scripts (run from the venv) ----------------------------------

set(IN_ISO_DIR "${CMAKE_BINARY_DIR}/in_iso")
set(OUT_ISO_DIR "${CMAKE_BINARY_DIR}/out_iso")
set(IN_ISO_STAMP "${CMAKE_BINARY_DIR}/in_iso.stamp")
set(PATCHED_DOL "${CMAKE_BINARY_DIR}/main.dol")
set(GECKO_TXT "${CMAKE_BINARY_DIR}/savestates.txt")
set(FINAL_ISO "${CMAKE_BINARY_DIR}/susamune_${VERS}.iso")

add_custom_command(
    OUTPUT "${IN_ISO_STAMP}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/extract_iso.py" "${SMS_ISO}" "${IN_ISO_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${IN_ISO_STAMP}"
    DEPENDS "${SMS_ISO}" "${CMAKE_SOURCE_DIR}/scripts/extract_iso.py"
    COMMENT "Extracting ${SMS_ISO}"
    VERBATIM)

add_custom_command(
    OUTPUT "${PATCHED_DOL}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/patch_dol.py"
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS}"
        --in-dol "${IN_ISO_DIR}/root/sys/main.dol"
        --out "${PATCHED_DOL}"
    DEPENDS susamune_obj "${IN_ISO_STAMP}" "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/susamune/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/patch_dol.py"
    COMMENT "Patching main.dol"
    VERBATIM)
add_custom_target(dol ALL DEPENDS "${PATCHED_DOL}")

# NOTE: gecko code target is currently broken, it doesn't seem to work in-game. But it is left here anyway.
add_custom_command(
    OUTPUT "${GECKO_TXT}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/patch_dol.py"
        --gecko
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS}"
        --out "${GECKO_TXT}"
    DEPENDS susamune_obj "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/susamune/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/patch_dol.py"
    COMMENT "Generating Gecko code list"
    VERBATIM)
add_custom_target(gecko DEPENDS "${GECKO_TXT}")

add_custom_command(
    OUTPUT "${FINAL_ISO}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/build_iso.py"
        --in-iso "${IN_ISO_DIR}"
        --out-iso "${OUT_ISO_DIR}"
        --dol "${PATCHED_DOL}"
        --output "${FINAL_ISO}"
    DEPENDS "${PATCHED_DOL}" "${IN_ISO_STAMP}"
            "${CMAKE_SOURCE_DIR}/scripts/build_iso.py"
    COMMENT "Building ${FINAL_ISO}"
    VERBATIM)
add_custom_target(iso DEPENDS "${FINAL_ISO}")

# Regenerate the checked-in linker script from the CodeWarrior map. Run on
# demand (`cmake --build build --target regen_ld`) after the map changes.
add_custom_target(regen_ld
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py" "${MAP_FILE}" "${LD_SCRIPT}"
    COMMENT "Regenerating ${LD_SCRIPT} from ${MAP_FILE}"
    VERBATIM)
