/*
 * Minimal retail symbol contract for the GLMJ01 memory diagnostic.
 *
 * The diagnostic calls retail functions through fixed-address function
 * pointers, so the linker only needs the startup/arena values used to prove
 * that the injected image and the root heap cannot overlap.
 */

_stack_addr = 0x804B63E8;
__ArenaLo = 0x804B8400;
__ArenaHi = 0x81700000;

_SDA_BASE_ = 0x804A0AE0;
_SDA2_BASE_ = 0x804AA380;

