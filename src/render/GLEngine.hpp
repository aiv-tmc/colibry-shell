// Low-level GLES/EGL device for the Colibry GPU renderer.
//
// CGLEngine owns the GBM device, the EGL context and every GPU-side
// resource shared across frames: compiled shader programs, offscreen
// scratch targets (full-size scene, blur pyramid levels and a generic
// effect buffer) and helpers to upload CPU pixels or import client dmabuf
// buffers as textures. All desktop drawing goes through this device.

#pragma once

#include "../protocol/Surface.hpp"

#include <aquamarine/buffer/Buffer.hpp>

#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>

namespace Colibry {

    class CGLEngine {
      public:
        static CGLEngine& get();

        // Creates the GBM device from the DRM fd, initializes EGL on a tiny
        // pbuffer surface and compiles all shader programs. Returns false
        // when GPU rendering is unavailable.
        bool init(int drmFD);
        bool ready() const;

        void makeCurrent();

        // Binds a KMS buffer as the render target through an EGLImage.
        bool bindTarget(const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target);

        static uint32_t compileProgram(const char* vs, const char* fs);

        uint32_t progQuad() const;
        uint32_t progSolid() const;
        uint32_t progTitlebar() const;
        uint32_t progBlurDown() const;
        uint32_t progBlurUp() const;
        uint32_t progDim() const;
        uint32_t progMask() const;

        uint32_t createTexture();
        void     deleteTexture(uint32_t tex);
        void     uploadRGBA(uint32_t tex, const uint32_t* pixels, int32_t w, int32_t h, int32_t stridePixels);

        // Imports a client-provided dmabuf (legacy single-plane protocol
        // path) into the given texture.
        bool importDMA(const SSurfaceDMA& dma, uint32_t tex);

        // Full-size offscreen scene target for overlay backdrops.
        void     bindScene(int32_t w, int32_t h);
        uint32_t sceneTexture() const;

        // Blur pyramid at half, quarter and eighth resolution.
        void     ensureBlur(int32_t screenW, int32_t screenH);
        void     bindBlur(int level);
        uint32_t blurTexture(int level) const;

        // Quarter-size reusable scratch target for one-off effects.
        void     bindEffect();
        uint32_t effectTexture() const;

        // Public: CGLCompositor embeds the engine by value.
        CGLEngine()  = default;
        ~CGLEngine() = default;

      private:
        CGLEngine(const CGLEngine&)            = delete;
        CGLEngine& operator=(const CGLEngine&) = delete;

        struct STarget {
            uint32_t fbo = 0;
            uint32_t tex = 0;
            int32_t  w   = 0;
            int32_t  h   = 0;
        };

        void ensureTarget(STarget& t, int32_t w, int32_t h);
        void freeTarget(STarget& t);

        void* m_gbmDevice  = nullptr;
        void* m_eglDisplay = nullptr;
        void* m_eglContext = nullptr;
        void* m_eglSurface = nullptr;
        bool  m_failed     = false;

        uint32_t m_progQuad     = 0;
        uint32_t m_progSolid    = 0;
        uint32_t m_progTitlebar = 0;
        uint32_t m_progBlurDown = 0;
        uint32_t m_progBlurUp   = 0;
        uint32_t m_progDim      = 0;
        uint32_t m_progMask     = 0;

        STarget m_target;
        STarget m_scene;
        STarget m_effect;
        STarget m_blur[3];
        int32_t m_blurSrcW = 0;
        int32_t m_blurSrcH = 0;
    };

}
