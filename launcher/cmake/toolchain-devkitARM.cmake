# Big-endian devkitARM (arm-none-eabi-) toolchain: the Nintendont ARM kernel,
# kernelboot, and fatfs-arm. Per-target flags (the -mbig-endian / arm926ej-s
# MACHDEP, linker scripts) live in each component's CMakeLists; this file only
# selects the compiler + binutils.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

include(${CMAKE_CURRENT_LIST_DIR}/BundledToolchain.cmake)
susamune_resolve_devkitpro()

if(CMAKE_HOST_WIN32)
    set(XSUFFIX ".exe")
endif()
set(XPREFIX "${DEVKITARM}/bin/arm-none-eabi-")

set(CMAKE_C_COMPILER   "${XPREFIX}gcc${XSUFFIX}")
set(CMAKE_CXX_COMPILER "${XPREFIX}g++${XSUFFIX}")
set(CMAKE_ASM_COMPILER "${XPREFIX}gcc${XSUFFIX}")
set(CMAKE_AR           "${XPREFIX}ar${XSUFFIX}")

set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "")
set(CMAKE_CXX_FLAGS_INIT "")
set(CMAKE_ASM_FLAGS_INIT "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
