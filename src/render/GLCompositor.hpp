// GPU scene compositor for Colibry.
//
// CGLCompositor drives the frame: it caches one GL texture per window
// surface (re-uploading shared-memory pixels only while the surface has
// unpresented damage, importing dmabufs zero-copy), picks between the
// direct fullscreen fast path and the full desktop scene, applies the
// startup fade and the screen corner mask, and scissores everything to the
// damaged rectangles so untouched regions never touch the GPU.

#pragma once

#include "GLEngine.hpp"

#include <aquamarine/buffer/Buffer.hpp>

#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>
#include <map>
#include <vector>

namespace Colibry {

    class CSurface;

    class CGLCompositor {
      public:
        static CGLCompositor& get();

        bool init(int drmFD);
        bool ready() const;

        CGLEngine& engine();

        // Stages new wallpaper pixels; uploaded on the next frame.
        void updateBackground(const std::vector<uint32_t>& pixels, int32_t w, int32_t h);

        // Renders the damaged regions of the desktop into the KMS buffer.
        bool renderFrame(const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target, const Hyprutils::Math::CRegion& damage);

        // Returns the cached content texture of a surface, uploading or
        // re-importing only when the surface carries unpresented damage.
        bool textureForSurface(CSurface* surface, uint32_t& tex, int32_t& w, int32_t& h);

        // Drops the cached texture of a destroyed surface.
        void dropSurface(CSurface* surface);

        // Corner radius of the visible screen rectangle in pixels.
        void    setScreenRounding(int32_t px);
        int32_t screenRounding() const;

        // Public: the singleton is stored as a function-local static.
        CGLCompositor()  = default;
        ~CGLCompositor() = default;

      private:
        CGLCompositor(const CGLCompositor&)            = delete;
        CGLCompositor& operator=(const CGLCompositor&) = delete;

        struct SSurfTex {
            uint32_t tex = 0;
            int32_t  w   = 0;
            int32_t  h   = 0;
        };

        bool fastPath(const Hyprutils::Math::CRegion& damage);
        void drawBackground();
        void drawMask();

        CGLEngine m_engine;

        std::map<CSurface*, SSurfTex> m_textures;

        std::vector<uint32_t> m_bgPixels;
        int32_t               m_bgW = 0;
        int32_t               m_bgH = 0;
        bool                  m_bgDirty = false;
        uint32_t              m_bgTex = 0;

        int32_t m_screenW = 0;
        int32_t m_screenH = 0;
        int32_t m_screenRounding = 0;

        double m_fadeAlpha = 0.0;
    };

}
