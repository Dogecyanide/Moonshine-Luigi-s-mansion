# kernel/asm: small PPC code blobs the ARM kernel patches into the game. Each .S
# is assembled, stripped to a raw binary, then embedded as a C header
# (${LAUNCHER_GEN_DIR}/asm/<name>.h, included by kernel/PatchCodes.h). Was
# kernel/asm/Makefile (which used the prebuilt bin2h host tool).

file(GLOB _asm_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.S")

set(_gen_asm_dir "${LAUNCHER_GEN_DIR}/asm")
file(MAKE_DIRECTORY "${_gen_asm_dir}")

set(_headers "")
foreach(src IN LISTS _asm_sources)
    get_filename_component(name "${src}" NAME_WE)
    set(_elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")
    set(_bin "${CMAKE_CURRENT_BINARY_DIR}/${name}.bin")
    set(_hdr "${_gen_asm_dir}/${name}.h")

    add_custom_command(
        OUTPUT "${_bin}"
        COMMAND "${XPREFIX}as${XSUFFIX}" -I "${CMAKE_CURRENT_SOURCE_DIR}" "${src}" -o "${_elf}"
        COMMAND "${XPREFIX}strip${XSUFFIX}" "${_elf}" -O binary -S -s -o "${_bin}"
        DEPENDS "${src}"
        COMMENT "asm blob ${name}"
        VERBATIM)
    susamune_bin2h("${_hdr}" "${_bin}" "${name}")
    list(APPEND _headers "${_hdr}")
endforeach()

add_custom_target(asm_headers ALL DEPENDS ${_headers})
