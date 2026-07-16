# codehandler: the Gecko code handler PPC blobs, embedded as
# ${LAUNCHER_GEN_DIR}/{codehandler,codehandleronly}.h (included by kernel/Patch.c).
# Was codehandler/Makefile (which used the prebuilt bin2h host tool).

file(MAKE_DIRECTORY "${LAUNCHER_GEN_DIR}")

set(_headers "")
foreach(name codehandler codehandleronly)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${name}.s")
    set(_elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")
    set(_bin "${CMAKE_CURRENT_BINARY_DIR}/${name}.bin")
    set(_hdr "${LAUNCHER_GEN_DIR}/${name}.h")

    add_custom_command(
        OUTPUT "${_bin}"
        COMMAND "${XPREFIX}gcc${XSUFFIX}" -nostartfiles -nodefaultlibs
                -Wl,-Ttext,0x80001000 -o "${_elf}" "${_src}"
        COMMAND "${XPREFIX}strip${XSUFFIX}"
                --strip-debug --strip-all --discard-all -F elf32-powerpc "${_elf}"
        COMMAND "${XPREFIX}objcopy${XSUFFIX}" -I elf32-powerpc -O binary "${_elf}" "${_bin}"
        DEPENDS "${_src}"
        COMMENT "codehandler blob ${name}"
        VERBATIM)
    susamune_bin2h("${_hdr}" "${_bin}" "${name}")
    list(APPEND _headers "${_hdr}")
endforeach()

add_custom_target(codehandler ALL DEPENDS ${_headers})
