# Bare-metal devkitPPC group: everything built with powerpc-eabi- that does NOT
# link libogc. Produces the data blobs (multidol_ldr.bin, stub.bin, PADReadGC.bin),
# the generated kernel/asm + codehandler headers, and libfatfs-ppc.a. Configured
# by the launcher super-build with the toolchain-devkitPPC.cmake toolchain file.

cmake_minimum_required(VERSION 3.21)
project(nintendont_ppc C ASM)

get_filename_component(LAUNCHER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${LAUNCHER_ROOT}/cmake")
include(Embed)

set(FATFS_ARCH ppc)
add_subdirectory("${LAUNCHER_ROOT}/fatfs"            fatfs)
add_subdirectory("${LAUNCHER_ROOT}/multidol"         multidol)
add_subdirectory("${LAUNCHER_ROOT}/resetstub"        resetstub)
add_subdirectory("${LAUNCHER_ROOT}/loader/source/ppc" padreadgc)
add_subdirectory("${LAUNCHER_ROOT}/kernel/asm"       asm)
add_subdirectory("${LAUNCHER_ROOT}/codehandler"      codehandler)
