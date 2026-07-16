# Nintendont kernelboot: the tiny ARM stub the loader stages into MEM to bring up
# the kernel. Embedded into the loader as data/kernelboot.bin. Was kernelboot/Makefile.

add_executable(kernelboot _start.S main.c syscalls.s)
set_target_properties(kernelboot PROPERTIES SUFFIX ".elf")

target_compile_options(kernelboot PRIVATE
    -mbig-endian -Os -fPIC -std=gnu89 -g -Wall
    -mcpu=arm926ej-s -msoft-float -mfloat-abi=soft
    -fdata-sections -ffunction-sections
    -fno-builtin-toupper -fno-builtin-memcpy -fno-builtin-memset
    -fno-builtin-memcmp -fno-builtin-malloc -fno-builtin-free)

target_include_directories(kernelboot PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fatfs"
    "${LAUNCHER_GEN_DIR}")

target_link_options(kernelboot PRIVATE
    -nostartfiles -nodefaultlibs -mbig-endian -fPIC -n -Wl,--gc-sections)

add_custom_command(
    OUTPUT "${LAUNCHER_DATA_DIR}/kernelboot.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LAUNCHER_DATA_DIR}"
    COMMAND "${XPREFIX}objcopy${XSUFFIX}" -S -O binary
            "$<TARGET_FILE:kernelboot>" "${LAUNCHER_DATA_DIR}/kernelboot.bin"
    DEPENDS kernelboot
    COMMENT "objcopy kernelboot.bin"
    VERBATIM)
add_custom_target(kernelboot_bin ALL DEPENDS "${LAUNCHER_DATA_DIR}/kernelboot.bin")
