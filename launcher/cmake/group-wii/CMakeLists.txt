# Wii/libogc group: the Nintendont loader GUI, linked against libogc + the
# vendored extlibs + fatfs-ppc, embedding every data blob the ppc/arm groups
# produced, and emitted as the HBC boot.dol (LAUNCHER_BOOT_DOL). Configured with
# the toolchain-wii.cmake toolchain.

cmake_minimum_required(VERSION 3.21)
project(nintendont_loader C)

get_filename_component(LAUNCHER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${LAUNCHER_ROOT}/cmake")
include(Embed)

add_subdirectory("${LAUNCHER_ROOT}/loader" loader)
