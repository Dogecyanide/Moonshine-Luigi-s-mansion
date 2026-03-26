#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JKernel/JKRFileLoader.hxx>
#include <JSystem/JUtility/JUTPalette.hxx>

struct ResTIMG {
    enum EFormat {
        I4     = 0,
        I8     = 1,
        IA4    = 2,
        IA8    = 3,
        RGB565 = 4,
        RGB5A3 = 5,
        RGBA32 = 6,
        C4     = 8,
        C8     = 9,
        C14X2  = 10,
        CMPR   = 14
    };

    enum EWrapMode { ClampToEdge, Repeat, MirroredRepeat };

    enum EFilterMode {
        Nearest,
        Linear,
        NearestMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapNearest,
        LinearMipmapLinear
    };

    u8 mFormat;
    u8 mAlphaSetting;
    u16 mWidth;
    u16 mHeight;
    u8 mWrapSMode;
    u8 mWrapTMode;
    bool mHasPalette;
    u8 mPaletteFormat;
    u16 mPaletteColors;
    s32 mPaletteOffset;
    u32 _10;
    u8 mFilterMinMode;
    u8 mFilterMagMode;
    u8 mLODMin;
    u8 mLODMag;
    u8 mMipMaps;
    u16 mLODBias;
    s32 mTextureOffset;
};

class JUTTexture {
public:
    inline JUTTexture() {}
    inline JUTTexture(const char *resource) {
        auto *timg = reinterpret_cast<const ResTIMG *>(JKRFileLoader::getGlbResource(resource));
        mTexObj2.val[2] = 0;
        storeTIMG(timg);
        _50 = false;
    }
    JUTTexture(int sX, int sY, GXTexFmt format);
    ~JUTTexture();

    void attachPalette(JUTPalette *palette);
    void initTexObj(GXTlut tlut);
    void initTexObj();
    void load(GXTexMapID id);
    void storeTIMG(const ResTIMG *timg);

    GXTexObj mTexObj;
    GXTexObj mTexObj2;
    u16 _40;
    bool _42;
    bool _43;
    u32 _44;
    u32 _48;
    GXTexObj *mGXObj;  // 0x4C
    bool _50;

    // 4C = GXTexFmt
};