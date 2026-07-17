# Super-build for the custom Nintendont launcher.
#
# The launcher spans three toolchains (bare devkitPPC, big-endian devkitARM, and
# devkitPPC+libogc), and CMake binds one compiler per build tree, so each toolchain
# group is configured as its own ExternalProject with its own toolchain file. The
# groups are wired ppc -> arm -> wii and share three output dirs (generated
# headers, data blobs, the fatfs-ppc lib). The final boot.dol is packaged into the
# HBC app zip.
#
# Normally driven by the root project's `launcher` target (which passes the mod
# manifest + inject header), but also configurable standalone for launcher dev.

cmake_minimum_required(VERSION 3.21)
project(susamune_launcher NONE)

include(ExternalProject)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundledToolchain.cmake")

# Resolve (and, if needed, extract) the devkitPro toolchains once here, then pass
# the absolute paths to every group so they don't each re-resolve/extract.
susamune_resolve_devkitpro()

# --- Inputs (overridable; defaults keep a standalone configure working) --------
if(NOT LAUNCHER_PYTHON)
    find_program(LAUNCHER_PYTHON NAMES python3 python REQUIRED)
endif()
get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LAUNCHER_SCRIPTS_DIR "${_repo_root}/scripts" CACHE PATH "repo scripts/ dir")
set(LAUNCHER_DEFAULT_SOURCE "di" CACHE STRING "meta.xml default load source (di/sd/usb)")
option(LAUNCHER_DISABLE_SUSAMUNE_PATCH "Disable the Susamune patch in the launcher kernel" OFF)

# The mod's inject header dir. In the integrated build the root project generates
# susamune_inject.h and points us at it; standalone we drop an empty one so the
# kernel's __has_include fallback compiles cleanly.
if(NOT SUSAMUNE_INJECT_DIR)
    set(SUSAMUNE_INJECT_DIR "${CMAKE_BINARY_DIR}/inject")
    file(MAKE_DIRECTORY "${SUSAMUNE_INJECT_DIR}")
    if(NOT EXISTS "${SUSAMUNE_INJECT_DIR}/susamune_inject.h")
        # No SUSAMUNE_GAME_ID => the PatchSusamune* paths compile out to no-ops.
        # SUSAMUNE_ARENA_RESERVE is used by one helper outside that guard, so give
        # it a harmless value to keep a standalone build compiling.
        file(WRITE "${SUSAMUNE_INJECT_DIR}/susamune_inject.h"
            "// standalone launcher build: no mod injected\n#define SUSAMUNE_ARENA_RESERVE 0u\n")
    endif()
endif()

# --- Shared output locations, produced by one group and consumed by others -----
set(_shared    "${CMAKE_BINARY_DIR}/shared")
set(_data_dir  "${_shared}/data")
set(_gen_dir   "${_shared}/gen")
set(_lib_dir   "${_shared}/lib")
set(_boot_dol  "${_shared}/boot.dol")
file(MAKE_DIRECTORY "${_data_dir}" "${_gen_dir}" "${_lib_dir}")

set(_common_args
    "-DCMAKE_BUILD_TYPE="
    "-DDEVKITPPC=${DEVKITPPC}"
    "-DDEVKITARM=${DEVKITARM}"
    "-DLAUNCHER_PYTHON=${LAUNCHER_PYTHON}"
    "-DLAUNCHER_SCRIPTS_DIR=${LAUNCHER_SCRIPTS_DIR}"
    "-DLAUNCHER_DATA_DIR=${_data_dir}"
    "-DLAUNCHER_GEN_DIR=${_gen_dir}"
    "-DLAUNCHER_LIB_DIR=${_lib_dir}")

set(_cmake_dir "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

ExternalProject_Add(ppc
    SOURCE_DIR "${_cmake_dir}/group-ppc"
    BINARY_DIR "${CMAKE_BINARY_DIR}/group-ppc"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${_cmake_dir}/toolchain-devkitPPC.cmake"
        ${_common_args}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON)

ExternalProject_Add(arm
    SOURCE_DIR "${_cmake_dir}/group-arm"
    BINARY_DIR "${CMAKE_BINARY_DIR}/group-arm"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${_cmake_dir}/toolchain-devkitARM.cmake"
        "-DSUSAMUNE_INJECT_DIR=${SUSAMUNE_INJECT_DIR}"
        "-DLAUNCHER_DISABLE_SUSAMUNE_PATCH=${LAUNCHER_DISABLE_SUSAMUNE_PATCH}"
        ${_common_args}
    DEPENDS ppc
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON)

ExternalProject_Add(wii
    SOURCE_DIR "${_cmake_dir}/group-wii"
    BINARY_DIR "${CMAKE_BINARY_DIR}/group-wii"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${_cmake_dir}/toolchain-wii.cmake"
        "-DLAUNCHER_BOOT_DOL=${_boot_dol}"
        ${_common_args}
    DEPENDS ppc arm
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON)

# --- Package boot.dol into the HBC app zip (region/icon/meta from the manifest) -
if(SUSAMUNE_MANIFEST AND LAUNCHER_OUTPUT_ZIP)
    add_custom_target(package ALL
        COMMAND "${LAUNCHER_PYTHON}" "${LAUNCHER_SCRIPTS_DIR}/package_launcher.py"
            --manifest "${SUSAMUNE_MANIFEST}"
            --boot-dol "${_boot_dol}"
            --out-zip  "${LAUNCHER_OUTPUT_ZIP}"
            --source   "${LAUNCHER_DEFAULT_SOURCE}"
        COMMENT "Packaging launcher HBC app zip"
        VERBATIM)
    add_dependencies(package wii)
endif()
