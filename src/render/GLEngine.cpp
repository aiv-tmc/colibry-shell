// Implementation of the GLES/EGL device.

#include "GLEngine.hpp"

#include "GLShaders.hpp"

#include <gbm.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cstdio>

namespace ColibryShell {

    static PFNEGLGETPLATFORMDISPLAYEXTPROC     eglGetPlatformDisplayEXT     = nullptr;
    static PFNEGLCREATEIMAGEKHRPROC            eglCreateImageKHR            = nullptr;
    static PFNEGLDESTROYIMAGEKHRPROC           eglDestroyImageKHR           = nullptr;
    static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

    CGLEngine& CGLEngine::get() {
        static CGLEngine s_instance;
        return s_instance;
    }

    bool CGLEngine::ready() const {
        return m_eglContext != nullptr;
    }

    static void eglError(const char* what) {
        std::fprintf(stderr, "[colibry-shell] EGL: %s failed (0x%x)\n", what, eglGetError());
    }

    bool CGLEngine::init(int drmFD) {
        if (m_eglContext)
            return true;
        if (m_failed)
            return false;

        eglGetPlatformDisplayEXT     = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        eglCreateImageKHR            = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        eglDestroyImageKHR           = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        glEGLImageTargetTexture2DOES = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));

        if (!eglGetPlatformDisplayEXT || !eglCreateImageKHR || !eglDestroyImageKHR || !glEGLImageTargetTexture2DOES) {
            std::fprintf(stderr, "[colibry-shell] missing EGL KMS/dmabuf extensions\n");
            m_failed = true;
            return false;
        }

        m_gbmDevice = gbm_create_device(drmFD);
        if (!m_gbmDevice) {
            std::fprintf(stderr, "[colibry-shell] failed to create the GBM device (fd %d)\n", drmFD);
            m_failed = true;
            return false;
        }

        // Primary path: platform display on GBM. Some stacks reject it
        // (EGL_BAD_ALLOC device enumeration quirks) — fall back to the
        // legacy eglGetDisplay on the GBM device.
        m_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, m_gbmDevice, nullptr);
        if (m_eglDisplay == EGL_NO_DISPLAY || eglInitialize((EGLDisplay)m_eglDisplay, nullptr, nullptr) != EGL_TRUE) {
            if (m_eglDisplay != EGL_NO_DISPLAY)
                eglError("eglInitialize(platform)");
            m_eglDisplay = eglGetDisplay((EGLNativeDisplayType)m_gbmDevice);
            if (m_eglDisplay == EGL_NO_DISPLAY || eglInitialize((EGLDisplay)m_eglDisplay, nullptr, nullptr) != EGL_TRUE) {
                eglError("eglInitialize(legacy)");
                m_failed = true;
                return false;
            }
        }

        eglBindAPI(EGL_OPENGL_ES_API);

        // Strict pbuffer config first; drivers with stripped config lists
        // get the relaxed ES2-only query.
        EGLint cfgAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};

        EGLint count = 0;
        void*  config = nullptr;
        eglChooseConfig((EGLDisplay)m_eglDisplay, cfgAttribs, (EGLConfig*)&config, 1, &count);

        if (count == 0) {
            EGLint relaxed[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
            eglChooseConfig((EGLDisplay)m_eglDisplay, relaxed, (EGLConfig*)&config, 1, &count);
        }

        if (count == 0) {
            eglError("eglChooseConfig");
            m_failed = true;
            return false;
        }

        EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        m_eglContext = eglCreateContext((EGLDisplay)m_eglDisplay, (EGLConfig)config, EGL_NO_CONTEXT, ctxAttribs);
        if (m_eglContext == EGL_NO_CONTEXT) {
            eglError("eglCreateContext");
            m_failed = true;
            return false;
        }

        // A tiny pbuffer is the usual makeCurrent placeholder. Drivers
        // without pbuffer-capable configs (common on virtualized stacks)
        // fall back to a surfaceless context: every frame renders into
        // FBOs attached to textures, so no EGL surface is ever needed.
        EGLint surfAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        m_eglSurface = eglCreatePbufferSurface((EGLDisplay)m_eglDisplay, (EGLConfig)config, surfAttribs);
        if (m_eglSurface == EGL_NO_SURFACE)
            eglGetError(); // clear BAD_MATCH and try surfaceless below

        makeCurrent();

        if (!glGetString(GL_VERSION)) {
            std::fprintf(stderr, "[colibry-shell] EGL: no current GL context (pbuffer and surfaceless both unavailable)\n");
            m_failed = true;
            return false;
        }

        m_progQuad     = compileProgram(QUAD_VERT, QUAD_FRAG);
        m_progSolid    = compileProgram(QUAD_VERT, SOLID_FRAG);
        m_progTitlebar = compileProgram(QUAD_VERT, TITLEBAR_FRAG);
        m_progBlurDown = compileProgram(QUAD_VERT, BLUR_DOWN_FRAG);
        m_progBlurUp   = compileProgram(QUAD_VERT, BLUR_UP_FRAG);
        m_progDim      = compileProgram(QUAD_VERT, DIM_FRAG);
        m_progMask     = compileProgram(QUAD_VERT, MASK_FRAG);

        if (!m_progQuad || !m_progSolid || !m_progTitlebar || !m_progBlurDown || !m_progBlurUp || !m_progDim || !m_progMask) {
            std::fprintf(stderr, "[colibry-shell] failed to compile GL programs\n");
            m_failed = true;
            return false;
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        std::fprintf(stderr, "[colibry-shell] GPU renderer ready (surfaceless: %s)\n", m_eglSurface == EGL_NO_SURFACE ? "yes" : "no");
        return true;
    }

    void CGLEngine::makeCurrent() {
        // With the surfaceless fallback both surface slots are EGL_NO_SURFACE.
        eglMakeCurrent((EGLDisplay)m_eglDisplay, (EGLSurface)m_eglSurface, (EGLSurface)m_eglSurface, (EGLContext)m_eglContext);
    }

    uint32_t CGLEngine::compileProgram(const char* vs, const char* fs) {
        const uint32_t vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vs, nullptr);
        glCompileShader(vert);

        const uint32_t frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fs, nullptr);
        glCompileShader(frag);

        const uint32_t program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);

        glDeleteShader(vert);
        glDeleteShader(frag);
        return program;
    }

    uint32_t CGLEngine::progQuad() const { return m_progQuad; }
    uint32_t CGLEngine::progSolid() const { return m_progSolid; }
    uint32_t CGLEngine::progTitlebar() const { return m_progTitlebar; }
    uint32_t CGLEngine::progBlurDown() const { return m_progBlurDown; }
    uint32_t CGLEngine::progBlurUp() const { return m_progBlurUp; }
    uint32_t CGLEngine::progDim() const { return m_progDim; }
    uint32_t CGLEngine::progMask() const { return m_progMask; }

    bool CGLEngine::bindTarget(const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target) {
        const auto attrs = target->dmabuf();
        if (!attrs.success)
            return false;

        const EGLint imageAttribs[] = {EGL_WIDTH, (EGLint)attrs.size.x, EGL_HEIGHT, (EGLint)attrs.size.y, EGL_LINUX_DRM_FOURCC_EXT, (EGLint)attrs.format,
                                       EGL_DMA_BUF_PLANE0_FD_EXT, attrs.fds[0], EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)attrs.offsets[0],
                                       EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)attrs.strides[0], EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                                       (EGLint)(attrs.modifier & 0xFFFFFFFF), EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(attrs.modifier >> 32), EGL_NONE};

        const EGLImage image = eglCreateImageKHR((EGLDisplay)m_eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, imageAttribs);
        if (image == EGL_NO_IMAGE_KHR) {
            eglError("eglCreateImageKHR(target)");
            return false;
        }

        // The target texture stays alive while bound so the framebuffer
        // attachment remains valid for the whole frame.
        if (m_target.fbo)
            glDeleteFramebuffers(1, &m_target.fbo);
        deleteTexture(m_target.tex);

        m_target.w = attrs.size.x;
        m_target.h = attrs.size.y;
        m_target.tex = createTexture();

        glBindTexture(GL_TEXTURE_2D, m_target.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
        eglDestroyImageKHR((EGLDisplay)m_eglDisplay, image);

        glGenFramebuffers(1, &m_target.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_target.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_target.tex, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "[colibry-shell] target framebuffer incomplete\n");
            return false;
        }

        return true;
    }

    uint32_t CGLEngine::createTexture() {
        uint32_t tex = 0;
        glGenTextures(1, &tex);
        return tex;
    }

    void CGLEngine::deleteTexture(uint32_t tex) {
        if (tex)
            glDeleteTextures(1, &tex);
    }

    void CGLEngine::uploadRGBA(uint32_t tex, const uint32_t* pixels, int32_t w, int32_t h, int32_t stridePixels) {
        glBindTexture(GL_TEXTURE_2D, tex);
        // GLES2 exposes the row-stride pixel store only through the EXT
        // name; every driver shipping EGL_KHR_image_base accepts it.
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stridePixels);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    bool CGLEngine::importDMA(const SSurfaceDMA& dma, uint32_t tex) {
        const EGLint imageAttribs[] = {EGL_WIDTH, (EGLint)dma.width, EGL_HEIGHT, (EGLint)dma.height, EGL_LINUX_DRM_FOURCC_EXT, (EGLint)dma.format,
                                       EGL_DMA_BUF_PLANE0_FD_EXT, dma.fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)dma.offset,
                                       EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)dma.stride, EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                                       (EGLint)(dma.modifier & 0xFFFFFFFF), EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(dma.modifier >> 32), EGL_NONE};

        const EGLImage image = eglCreateImageKHR((EGLDisplay)m_eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, imageAttribs);
        if (image == EGL_NO_IMAGE_KHR)
            return false;

        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
        eglDestroyImageKHR((EGLDisplay)m_eglDisplay, image);
        return true;
    }

    void CGLEngine::ensureTarget(STarget& t, int32_t w, int32_t h) {
        if (t.fbo && t.w == w && t.h == h)
            return;

        freeTarget(t);
        t.w = w;
        t.h = h;
        t.tex = createTexture();
        uploadRGBA(t.tex, nullptr, w, h, w);

        glGenFramebuffers(1, &t.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    }

    void CGLEngine::freeTarget(STarget& t) {
        if (t.fbo)
            glDeleteFramebuffers(1, &t.fbo);
        deleteTexture(t.tex);
        t.fbo = 0;
        t.tex = 0;
        t.w = 0;
        t.h = 0;
    }

    void CGLEngine::bindScene(int32_t w, int32_t h) {
        ensureTarget(m_scene, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, m_scene.fbo);
    }

    uint32_t CGLEngine::sceneTexture() const {
        return m_scene.tex;
    }

    void CGLEngine::ensureBlur(int32_t screenW, int32_t screenH) {
        if (m_blurSrcW == screenW && m_blurSrcH == screenH)
            return;

        m_blurSrcW = screenW;
        m_blurSrcH = screenH;

        for (int i = 0; i < 3; ++i) {
            const int32_t w = std::max(1, screenW >> (i + 1));
            const int32_t h = std::max(1, screenH >> (i + 1));
            ensureTarget(m_blur[i], w, h);
        }
    }

    void CGLEngine::bindBlur(int level) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_blur[level].fbo);
    }

    uint32_t CGLEngine::blurTexture(int level) const {
        return m_blur[level].tex;
    }

    void CGLEngine::bindEffect() {
        ensureTarget(m_effect, std::max(1, m_blurSrcW / 4), std::max(1, m_blurSrcH / 4));
        glBindFramebuffer(GL_FRAMEBUFFER, m_effect.fbo);
    }

    uint32_t CGLEngine::effectTexture() const {
        return m_effect.tex;
    }

}
