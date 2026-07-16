# FatFS, built for either ARM (Nintendont kernel) or PPC (loader), selected by
# FATFS_ARCH. Was fatfs/Makefile.arm + fatfs/Makefile.ppc.

add_library(fatfs-${FATFS_ARCH} STATIC ff.c ff_utf8.c option/ccsbcs.c)
target_include_directories(fatfs-${FATFS_ARCH} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(FATFS_ARCH STREQUAL "arm")
    target_compile_options(fatfs-arm PRIVATE
        -mbig-endian -O3 -std=gnu89 -g -Wall
        -mcpu=arm926ej-s -msoft-float -mfloat-abi=soft
        -fdata-sections -ffunction-sections
        -fno-builtin-toupper -fno-builtin-memcpy -fno-builtin-memset
        -fno-builtin-memcmp -fno-builtin-malloc -fno-builtin-free)
else()
    target_compile_options(fatfs-ppc PRIVATE
        -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
        -O3 -g -std=gnu89 -Wall)
    # The loader links this by -L${LAUNCHER_LIB_DIR} -lfatfs-ppc.
    set_target_properties(fatfs-ppc PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${LAUNCHER_LIB_DIR}")
endif()
