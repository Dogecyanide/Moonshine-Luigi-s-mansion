# Wii/libogc toolchain for the Nintendont loader (the GUI that becomes boot.dol).
# Same powerpc-eabi- compiler as the bare devkitPPC toolchain, plus the libogc
# include/lib locations that wii_rules exports. The loader's MACHDEP
# (-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float) is applied in loader/CMakeLists.

include(${CMAKE_CURRENT_LIST_DIR}/toolchain-devkitPPC.cmake)

set(LIBOGC_INC "${DEVKITPRO}/libogc/include")
set(LIBOGC_LIB "${DEVKITPRO}/libogc/lib/wii")
