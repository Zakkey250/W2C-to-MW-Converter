#pragma once
#include <array>
#include <d3d9.h>
#include "ProcessTermination.h"
#include "TextureTransfer.h"

namespace w2v2 {
// SYSTEMMEM upload strips for the most recently used formats and mip sizes.
// All slots together fit the original 512 KiB staging lease.
class TextureStrip {
    static constexpr std::size_t capacityBytes = 512 * 1024;
    struct Slot {
        IDirect3DTexture9* texture{};
        IDirect3DSurface9* surface{};
        unsigned width{}, height{}, level{}, format{};
        std::size_t bytes{};
        std::uint64_t used{};
    };
    std::array<Slot, 12> slots{};
    std::size_t resident{};
    std::uint64_t sequence{};

    void release(Slot& slot) {
        if(slot.surface) slot.surface->Release();
        if(slot.texture) slot.texture->Release();
        resident -= slot.bytes;
        slot = {};
    }
    Slot* prepare(IDirect3DDevice9* device, const TextureLayout& layout, const TextureSlice& slice) {
        unsigned level=0;
        if(layout.block)
            while(level<2 && ((slice.width<<level)%4 || (slice.height<<level)%4)) ++level;
        const unsigned width=slice.width<<level, height=slice.height<<level;
        if(!width || !height || width>8192 || height>8192) return nullptr;
        for(auto& slot:slots) {
            if(slot.texture && slot.width==width && slot.height==height && slot.level==level && slot.format==layout.format) {
                slot.used=++sequence;
                return &slot;
            }
        }
        std::size_t bytes=0;
        for(unsigned mip=0;mip<=level;++mip) {
            const unsigned w=std::max(1u,width>>mip), h=std::max(1u,height>>mip);
            bytes += layout.block ? std::size_t((w+3)/4)*((h+3)/4)*layout.block : std::size_t(w)*h*4;
        }
        if(bytes>capacityBytes) return nullptr;
        Slot* freeSlot=nullptr;
        do {
            Slot* oldest=nullptr;
            for(auto& slot:slots) {
                if(!slot.texture) freeSlot=&slot;
                else if(!oldest || slot.used<oldest->used) oldest=&slot;
            }
            if(freeSlot && resident<=capacityBytes-bytes) break;
            if(!oldest) return nullptr;
            release(*oldest);
            freeSlot=oldest;
        } while(true);
        if(FAILED(device->CreateTexture(width,height,level+1,0,D3DFORMAT(layout.format),D3DPOOL_SYSTEMMEM,&freeSlot->texture,nullptr))) return nullptr;
        if(FAILED(freeSlot->texture->GetSurfaceLevel(level,&freeSlot->surface))) {
            release(*freeSlot);
            return nullptr;
        }
        freeSlot->width=width;freeSlot->height=height;freeSlot->level=level;freeSlot->format=layout.format;
        freeSlot->bytes=bytes;freeSlot->used=++sequence;resident+=bytes;
        return freeSlot;
    }
public:
    TextureStrip()=default;
    TextureStrip(const TextureStrip&)=delete;
    TextureStrip& operator=(const TextureStrip&)=delete;
    ~TextureStrip(){if(MayReleaseGraphics())clear();}
    void clear(){for(auto& slot:slots)release(slot);}
    bool upload(IDirect3DDevice9* device,IDirect3DTexture9* destination,const TextureLayout& layout,const TextureSlice& slice,const void* data) {
        if(!slice.rows) return false;
        auto slot=prepare(device,layout,slice);
        if(!slot) return false;
        D3DLOCKED_RECT dest{};
        if(FAILED(slot->texture->LockRect(slot->level,&dest,nullptr,0))) return false;
        const auto stride=layout.mip[slice.level].rowBytes;
        bool valid=dest.pBits && dest.Pitch>=int(stride);
        const auto source=static_cast<const unsigned char*>(data)+slice.offset;
        if(valid && dest.Pitch==int(stride)) std::memcpy(dest.pBits,source,std::size_t(stride)*slice.rows);
        else if(valid) {
            for(unsigned row=0;row<slice.rows;++row)
                std::memcpy(static_cast<unsigned char*>(dest.pBits)+std::size_t(row)*dest.Pitch,source+std::size_t(row)*stride,stride);
        }
        if(FAILED(slot->texture->UnlockRect(slot->level)) || !valid) return false;
        IDirect3DSurface9* target=nullptr;
        if(FAILED(destination->GetSurfaceLevel(slice.level,&target))) return false;
        POINT point{0,LONG(slice.y)};
        const auto result=device->UpdateSurface(slot->surface,nullptr,target,&point);
        target->Release();
        return SUCCEEDED(result);
    }
};
}
