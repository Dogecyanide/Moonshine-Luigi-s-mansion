# Helpers replacing the devkitPro tools the bundled toolchain lacks and the
# `bin2o` macro from devkitPPC/base_rules. Requires LAUNCHER_PYTHON (the repo
# venv interpreter) and LAUNCHER_SCRIPTS_DIR (repo scripts/), plus the XPREFIX /
# XSUFFIX cross-tool names set by the active toolchain file.

include_guard(GLOBAL)

# Embed a binary file into a compiled target the way wii_rules' bin2o does:
# generate a linkable object exposing `<name>`, `<name>_end`, `<name>_size`, and
# the matching `<name>.h` extern header the loader sources #include. Adds both the
# object and its header include dir to <target>.
function(susamune_embed_binary target input name)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/embed")
    file(MAKE_DIRECTORY "${_dir}")
    # file(GENERATE) is idempotent: it won't rewrite (or bump the mtime of) the
    # header when the content is unchanged, so unrelated loader TUs that include
    # it don't recompile on every build.
    file(GENERATE OUTPUT "${_dir}/${name}.h" CONTENT
"extern const u8 ${name}_end[];
extern const u8 ${name}[];
extern const u32 ${name}_size;
")
    set(_s "${_dir}/${name}.s")
    set(_o "${_dir}/${name}.o")
    add_custom_command(
        OUTPUT "${_o}"
        COMMAND "${LAUNCHER_PYTHON}" "${LAUNCHER_SCRIPTS_DIR}/bin2s.py" -a 32 -o "${_s}" "${input}"
        COMMAND "${XPREFIX}as${XSUFFIX}" -o "${_o}" "${_s}"
        DEPENDS "${input}" "${LAUNCHER_SCRIPTS_DIR}/bin2s.py"
        COMMENT "bin2s ${name}"
        VERBATIM)
    set_source_files_properties("${_o}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(${target} PRIVATE "${_o}")
    target_include_directories(${target} PRIVATE "${_dir}")
endfunction()

# Embed a binary as a C header (const array + _size #define), replacing the
# Nintendont `bin2h` host tool. Used for the kernel/asm and codehandler blobs.
function(susamune_bin2h output input name)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${LAUNCHER_PYTHON}" "${LAUNCHER_SCRIPTS_DIR}/bin2h.py" "${input}" -o "${output}" --name "${name}"
        DEPENDS "${input}" "${LAUNCHER_SCRIPTS_DIR}/bin2h.py"
        COMMENT "bin2h ${name}"
        VERBATIM)
endfunction()

# Convert an ELF to a GameCube/Wii DOL (was the wii_rules `%.dol: %.elf` rule).
function(susamune_elf2dol output input)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${LAUNCHER_PYTHON}" "${LAUNCHER_SCRIPTS_DIR}/elf2dol.py" "${input}" "${output}"
        DEPENDS "${input}" "${LAUNCHER_SCRIPTS_DIR}/elf2dol.py"
        COMMENT "elf2dol ${output}"
        VERBATIM)
endfunction()
