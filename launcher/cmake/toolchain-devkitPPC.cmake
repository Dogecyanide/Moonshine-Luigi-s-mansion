# Bare-metal devkitPPC (powerpc-eabi-) toolchain: the Nintendont PPC pieces that
# do NOT link libogc — multidol, resetstub, PADReadGC, the kernel/asm code blobs,
# codehandler, and fatfs-ppc. Per-target flags (MACHDEP, linker scripts, ...) live
# in each component's CMakeLists, matching the original Makefiles; this file only
# selects the compiler + binutils.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

include(${CMAKE_CURRENT_LIST_DIR}/BundledToolchain.cmake)
susamune_resolve_devkitpro()

if(CMAKE_HOST_WIN32)
    set(XSUFFIX ".exe")
endif()
set(XPREFIX "${DEVKITPPC}/bin/powerpc-eabi-")

set(CMAKE_C_COMPILER   "${XPREFIX}gcc${XSUFFIX}")
set(CMAKE_CXX_COMPILER "${XPREFIX}g++${XSUFFIX}")
set(CMAKE_ASM_COMPILER "${XPREFIX}gcc${XSUFFIX}")
set(CMAKE_AR           "${XPREFIX}ar${XSUFFIX}")

# Bare-metal target: the default "compile and link a test executable" probe can't
# succeed, so assert the compilers work instead (mirrors the root CMakeLists).
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# No implicit optimisation/debug flags; each component sets its own.
set(CMAKE_C_FLAGS_INIT "")
set(CMAKE_CXX_FLAGS_INIT "")
set(CMAKE_ASM_FLAGS_INIT "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
