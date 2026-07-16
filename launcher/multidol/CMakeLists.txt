# multidol: the multi-DOL loader stub embedded into the loader as
# data/multidol_ldr.bin. Was multidol/Makefile.

add_executable(multidol_ldr crt0.S cache.c main.c global.c apploader.c dip.c utils.c)
set_target_properties(multidol_ldr PROPERTIES SUFFIX ".elf")

target_compile_options(multidol_ldr PRIVATE
    -mcpu=750 -meabi -mhard-float -O1 -Werror -Wall
    -fdata-sections -ffunction-sections
    $<$<COMPILE_LANGUAGE:ASM>:-D_LANGUAGE_ASSEMBLY;-DHW_RVL>)

target_link_options(multidol_ldr PRIVATE
    -mcpu=750 -meabi -mhard-float -n -nostartfiles -nostdlib
    -Wl,--gc-sections "-T${CMAKE_CURRENT_SOURCE_DIR}/openstub.ld")
set_target_properties(multidol_ldr PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/openstub.ld")

add_custom_command(
    OUTPUT "${LAUNCHER_DATA_DIR}/multidol_ldr.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LAUNCHER_DATA_DIR}"
    COMMAND "${XPREFIX}objcopy${XSUFFIX}" -O binary -S
            "$<TARGET_FILE:multidol_ldr>" "${LAUNCHER_DATA_DIR}/multidol_ldr.bin"
    DEPENDS multidol_ldr
    COMMENT "objcopy multidol_ldr.bin"
    VERBATIM)
add_custom_target(multidol ALL DEPENDS "${LAUNCHER_DATA_DIR}/multidol_ldr.bin")
