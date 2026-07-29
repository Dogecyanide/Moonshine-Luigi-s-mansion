#ifndef _SUSAMUNE_GLYPHS_HXX
#define _SUSAMUNE_GLYPHS_HXX

// standard_fontEx.bfn uses Shift-JIS on JP and a direct one-byte map on US/PAL.
#if defined(SUSAMUNE_VERSION_JP)
#define SUSAMUNE_GLYPH_A     "\x81\x97"
#define SUSAMUNE_GLYPH_B     "\x81\x94"
#define SUSAMUNE_GLYPH_X     "\x81\x7b"
#define SUSAMUNE_GLYPH_Y     "\x81\x8f"
#define SUSAMUNE_GLYPH_L     "\x81\x83"
#define SUSAMUNE_GLYPH_R     "\x81\x84"
#define SUSAMUNE_GLYPH_Z     "\x81\x90"
#define SUSAMUNE_GLYPH_C     "\x81\x93"
#define SUSAMUNE_GLYPH_AMP   "\x81\x95"
#define SUSAMUNE_GLYPH_LEFT  "\x81\xa9"
#define SUSAMUNE_GLYPH_UP    "\x81\xaa"
#define SUSAMUNE_GLYPH_RIGHT "\x81\xa8"
#define SUSAMUNE_GLYPH_DOWN  "\x81\xab"
#elif defined(SUSAMUNE_VERSION_US) || defined(SUSAMUNE_VERSION_PAL)
#define SUSAMUNE_GLYPH_A     "\x40"
#define SUSAMUNE_GLYPH_B     "\x23"
#define SUSAMUNE_GLYPH_X     "\x2b"
#define SUSAMUNE_GLYPH_Y     "\xa5"
#define SUSAMUNE_GLYPH_L     "\x3c"
#define SUSAMUNE_GLYPH_R     "\x3e"
#define SUSAMUNE_GLYPH_Z     "\x24"
#define SUSAMUNE_GLYPH_C     "\x25"
#define SUSAMUNE_GLYPH_AMP   "\x26"
#define SUSAMUNE_GLYPH_LEFT  "\x5b"
#define SUSAMUNE_GLYPH_UP    "\x5e"
#define SUSAMUNE_GLYPH_RIGHT "\x5d"
#define SUSAMUNE_GLYPH_DOWN  "\xff"
#else
#error "Select a supported Susamune game version"
#endif

#endif  // _SUSAMUNE_GLYPHS_HXX
