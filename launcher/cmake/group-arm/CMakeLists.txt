# devkitARM group: the big-endian ARM kernel + kernelboot + fatfs-arm. Consumes
# the ppc group's generated headers (via LAUNCHER_GEN_DIR). Produces
# data/kernel.zip and data/kernelboot.bin. Configured with the
# toolchain-devkitARM.cmake toolchain.

cmake_minimum_required(VERSION 3.21)
project(nintendont_arm C ASM)

get_filename_component(LAUNCHER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${LAUNCHER_ROOT}/cmake")
include(Embed)

set(FATFS_ARCH arm)
add_subdirectory("${LAUNCHER_ROOT}/fatfs"      fatfs)
add_subdirectory("${LAUNCHER_ROOT}/kernel"     kernel)
add_subdirectory("${LAUNCHER_ROOT}/kernelboot" kernelboot)
