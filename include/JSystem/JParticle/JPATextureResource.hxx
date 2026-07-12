#pragma once

#include <JSystem/JKernel/JKRHeap.hxx>
#include <JSystem/JParticle/JPADefaultTexture.hxx>
#include <JSystem/JParticle/JPATexture.hxx>

class JPATextureResource {
public:
    JPATextureResource(size_t capacity, JKRHeap *heap);

    void registration(u8 *data, JKRHeap *heap);

    JPADefaultTexture mDefaultTexture;
    size_t mTextureNum;
    size_t mTextureMax;
    JPATexture **mTextures;
};