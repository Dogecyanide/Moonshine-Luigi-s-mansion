# Resolve the devkitARM / devkitPPC cross toolchains.
#
# Precedence: the DEVKITPPC / DEVKITARM environment (or cache) variables win; if
# either is unset we fall back to the vendored Windows toolchain shipped as
# launcher/nintendont_devkitpro_win32.zip, extracting it once. This reproduces
# the toolchain-sourcing logic that used to live in scripts/build_launcher.py so
# the launcher still builds with zero setup on Windows.
#
# Included by each toolchain file (toolchain-devkit{PPC,ARM}.cmake, toolchain-wii)
# via susamune_resolve_devkitpro(), which sets DEVKITPPC, DEVKITARM and DEVKITPRO
# in the caller's scope.

include_guard(GLOBAL)

# This module lives in launcher/cmake/, so its own directory's parent is the
# launcher dir. Captured at parse time because CMAKE_CURRENT_LIST_DIR inside the
# macro below reflects the caller, not this file.
get_filename_component(_SUSAMUNE_LAUNCHER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(_susamune_mirror_cc1_dlls dk)
    # On Windows the gcc driver's cc1/cc1plus live under libexec/ and need the
    # mingw runtime DLLs (libwinpthread-1.dll, ...) that ship next to the driver
    # in bin/. Windows resolves an exe's own directory first, so mirror the bin/
    # DLLs beside each cc1 rather than relying on PATH.
    if(NOT CMAKE_HOST_WIN32)
        return()
    endif()
    file(GLOB _dlls "${dk}/bin/*.dll")
    file(GLOB_RECURSE _cc1s "${dk}/libexec/*/cc1*.exe")
    foreach(cc1 IN LISTS _cc1s)
        get_filename_component(_cc1dir "${cc1}" DIRECTORY)
        foreach(dll IN LISTS _dlls)
            get_filename_component(_name "${dll}" NAME)
            if(NOT EXISTS "${_cc1dir}/${_name}")
                file(COPY "${dll}" DESTINATION "${_cc1dir}")
            endif()
        endforeach()
    endforeach()
endfunction()

macro(susamune_resolve_devkitpro)
    if(NOT DEVKITPPC AND DEFINED ENV{DEVKITPPC})
        set(DEVKITPPC "$ENV{DEVKITPPC}")
    endif()
    if(NOT DEVKITARM AND DEFINED ENV{DEVKITARM})
        set(DEVKITARM "$ENV{DEVKITARM}")
    endif()

    set(_launcher_dir "${_SUSAMUNE_LAUNCHER_DIR}")
    set(_bundled "${_launcher_dir}/nintendont_devkitpro")

    if(NOT DEVKITPPC OR NOT DEVKITARM)
        if(NOT EXISTS "${_bundled}")
            message(STATUS "Extracting bundled devkitPro toolchain (nintendont_devkitpro_win32.zip)...")
            file(ARCHIVE_EXTRACT
                INPUT "${_launcher_dir}/nintendont_devkitpro_win32.zip"
                DESTINATION "${_launcher_dir}")
        endif()
        if(NOT DEVKITPPC)
            set(DEVKITPPC "${_bundled}/devkitPPC")
        endif()
        if(NOT DEVKITARM)
            set(DEVKITARM "${_bundled}/devkitARM")
        endif()
        _susamune_mirror_cc1_dlls("${DEVKITPPC}")
        _susamune_mirror_cc1_dlls("${DEVKITARM}")
    endif()

    get_filename_component(DEVKITPRO "${DEVKITPPC}/.." ABSOLUTE)
endmacro()
