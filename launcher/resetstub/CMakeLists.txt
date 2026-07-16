# resetstub: the reset stub embedded into the loader as data/stub.bin.
# Was resetstub/Makefile.

add_executable(tinyload crt0.S memory.S ios.c utils.c cache.c usbgecko.c main.c)
set_target_properties(tinyload PROPERTIES SUFFIX ".elf")

target_compile_options(tinyload PRIVATE
    -mcpu=750 -mno-eabi -mhard-float -DTINY -DDEBUG
    -O1 -Werror -Wall -pipe -ffunction-sections
    -finline-functions-called-once -mno-sdata
    $<$<COMPILE_LANGUAGE:ASM>:-D_LANGUAGE_ASSEMBLY;-DHW_RVL;-DTINY>)

target_link_options(tinyload PRIVATE
    -mcpu=750 -mno-eabi -mhard-float -DTINY -DDEBUG
    -n -nostartfiles -nostdlib
    "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/openstub.ld")
set_target_properties(tinyload PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/openstub.ld")

add_custom_command(
    OUTPUT "${LAUNCHER_DATA_DIR}/stub.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LAUNCHER_DATA_DIR}"
    COMMAND "${XPREFIX}objcopy${XSUFFIX}" -O binary
            "$<TARGET_FILE:tinyload>" "${LAUNCHER_DATA_DIR}/stub.bin"
    DEPENDS tinyload
    COMMENT "objcopy stub.bin"
    VERBATIM)
add_custom_target(resetstub ALL DEPENDS "${LAUNCHER_DATA_DIR}/stub.bin")
