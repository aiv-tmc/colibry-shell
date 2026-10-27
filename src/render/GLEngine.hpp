// Low-level GLES/EGL device for the ColibryShell GPU renderer.
//
// CGLEngine owns the GBM device, the EGL context, compiled programs and
// scratch targets. The KMS render target's EGLImage is created once per
// buffer and reused: recreating it per frame is expensive and can block
// on a buffer the display pipeline still owns.

#pragma once

#include "../protocol/Surface.hpp"

#include <aquamarine/buffer/Buffer.hpp>

#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>

namespace ColibryShell {

    class CGLEngine {
      public:
        static CGLEngine& get();

        bool init(int drmFD);
        bool ready() const;

        void makeCurrent();

        // Binds a KMS buffer as the render target. The EGLImage and FBO
        // are (re)created only when the bound buffer changes.
        bool bindTarget(const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target);

        static uint32_t compileProgram(const char* vs, const char* fs);

        uint32_t progQuad() const;
        uint32_t progSolid() const;
        uint32_t progTitlebar() const;
        uint32_t progBlurDown() const;
        uint32_t progBlurUp() const;
        uint32_t progDim() const;
        uint32_t progMask() const;

        float flipX() const;
        float flipY() const;

        uint32_t createTexture();
        void     deleteTexture(uint32_t tex);
        void     uploadRGBA(uint32_t tex, const uint32_t* pixels, int32_t w, int32_t h, int32_t stridePixels);

        bool importDMA(const SSurfaceDMA& dma, uint32_t tex);

        void     bindScene(int32_t w, int32_t h);
        uint32_t sceneTexture() const;

        void     ensureBlur(int32_t screenW, int32_t screenH);
        void     bindBlur(int level);
        uint32_t blurTexture(int level) const;

        void     bindEffect();
        uint32_t effectTexture() const;

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

        float m_flipX = 1.0f;
        float m_flipY = 1.0f;

        // The buffer the target FBO currently refers to; reused across
        // frames until the buffer pool rotates to a different one.
        Aquamarine::IBuffer* m_targetBuffer = nullptr;

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
