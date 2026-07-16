# PADReadGC: the GameCube pad-read stub the loader copies to MEM at runtime,
# embedded as data/PADReadGC.bin. Was loader/source/ppc/Makefile.

add_executable(PADReadGC PADReadGC.c)
set_target_properties(PADReadGC PROPERTIES SUFFIX ".elf")

target_compile_options(PADReadGC PRIVATE -Os -Wall -nostartfiles -mhard-float)
target_link_options(PADReadGC PRIVATE
    -Os -nostartfiles -mhard-float -s
    "-T${CMAKE_CURRENT_SOURCE_DIR}/openstub.ld")
set_target_properties(PADReadGC PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/openstub.ld")

add_custom_command(
    OUTPUT "${LAUNCHER_DATA_DIR}/PADReadGC.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LAUNCHER_DATA_DIR}"
    COMMAND "${XPREFIX}objcopy${XSUFFIX}" -S -O binary
            "$<TARGET_FILE:PADReadGC>" "${LAUNCHER_DATA_DIR}/PADReadGC.bin"
    DEPENDS PADReadGC
    COMMENT "objcopy PADReadGC.bin"
    VERBATIM)
add_custom_target(padreadgc ALL DEPENDS "${LAUNCHER_DATA_DIR}/PADReadGC.bin")
