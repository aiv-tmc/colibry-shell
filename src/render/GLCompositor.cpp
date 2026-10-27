// Implementation of the GPU scene compositor.

#include "GLCompositor.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Cursor.hpp"
#include "../desktop/Launcher.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Buffer.hpp"
#include "../protocol/DMABuf.hpp"
#include "../protocol/Surface.hpp"
#include "Draw.hpp"
#include "LauncherDraw.hpp"
#include "SceneDraw.hpp"
#include "SwitcherDraw.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <pixman.h>

#include <drm_fourcc.h>

namespace ColibryShell {

    static constexpr int32_t MAX_TEXTURE_DIM = 16384;

    static bool dmaImportable(const SSurfaceDMA& dma) {
        if (!dma.valid || dma.fd < 0 || dma.stride == 0)
            return false;
        if (dma.width <= 0 || dma.height <= 0 || dma.width > MAX_TEXTURE_DIM || dma.height > MAX_TEXTURE_DIM)
            return false;

        switch (dma.format) {
            case DRM_FORMAT_ARGB8888:
            case DRM_FORMAT_XRGB8888:
            case DRM_FORMAT_ABGR8888:
            case DRM_FORMAT_XBGR8888: return true;
            default: return false;
        }
    }

    static void autoDisableDMA(const char* reason) {
        if (CConfigManager::get().dmabufMode() == -1) {
            std::fprintf(stderr, "[colibry-shell] dma problem (%s), clients will migrate to wl_shm\n", reason);
            CDMABufManager::get().disable();
        }
    }

    CGLCompositor& CGLCompositor::get() {
        static CGLCompositor s_instance;
        return s_instance;
    }

    bool CGLCompositor::init(int drmFD) {
        return m_engine.init(drmFD);
    }

    bool CGLCompositor::ready() const {
        return m_engine.ready();
    }

    CGLEngine& CGLCompositor::engine() {
        return m_engine;
    }

    void CGLCompositor::setScreenRounding(int32_t px) {
        m_screenRounding = std::max(0, px);
    }

    int32_t CGLCompositor::screenRounding() const {
        return m_screenRounding;
    }

    void CGLCompositor::updateBackground(const std::vector<uint32_t>& pixels, int32_t w, int32_t h) {
        m_bgPixels = pixels;
        m_bgW      = w;
        m_bgH      = h;
        m_bgDirty  = true;
    }

    void CGLCompositor::dropSurface(CSurface* surface) {
        const auto it = m_textures.find(surface);
        if (it == m_textures.end())
            return;

        if (m_engine.ready())
            m_engine.deleteTexture(it->second.tex);
        m_textures.erase(it);
    }

    bool CGLCompositor::textureForSurface(CSurface* surface, uint32_t& tex, int32_t& w, int32_t& h) {
        auto& entry = m_textures[surface];

        if (entry.tex && surface->damage().empty()) {
            tex = entry.tex;
            w   = entry.w;
            h   = entry.h;
            return true;
        }

        if (!entry.tex)
            entry.tex = m_engine.createTexture();

        const auto& dma = surface->currentDMA();

        // The dma attributes can be unreadable garbage for shm buffers on
        // some protocol paths; only a structurally valid, importable dma
        // takes the zero-copy route. Anything else falls through to the
        // shared-memory buffer, which is always present for shm clients.
        if (dma.valid && dmaImportable(dma)) {
            const auto start = std::chrono::steady_clock::now();
            const bool ok    = m_engine.importDMA(dma, entry.tex);
            const auto elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

            if (!ok) {
                static CSurface* s_loggedImport = nullptr;
                if (s_loggedImport != surface) {
                    s_loggedImport = surface;
                    std::fprintf(stderr, "[colibry-shell] dma import failed (fourcc=0x%x modifier=0x%lx), using shm\n", dma.format,
                                 (unsigned long)dma.modifier);
                }
                autoDisableDMA("eglCreateImageKHR failed");
                // Fall through to the shm buffer below.
            } else {
                if (elapsedMs > 100) {
                    std::fprintf(stderr, "[colibry-shell] dma import took %lld ms\n", (long long)elapsedMs);
                    autoDisableDMA("import blocked excessively");
                }

                entry.w = (int32_t)dma.width;
                entry.h = (int32_t)dma.height;
                tex     = entry.tex;
                w       = entry.w;
                h       = entry.h;
                return true;
            }
        } else if (dma.valid) {
            static CSurface* s_loggedGarbage = nullptr;
            if (s_loggedGarbage != surface) {
                s_loggedGarbage = surface;
                std::fprintf(stderr, "[colibry-shell] ignoring malformed dma attrs (fourcc=0x%x), using shm\n", dma.format);
            }
        }

        const auto buffer = surface->currentBuffer();
        if (!buffer) {
            if (entry.w > 0) {
                tex = entry.tex;
                w   = entry.w;
                h   = entry.h;
                return true;
            }
            return false;
        }

        const auto size = buffer->size();
        if (size.x <= 0 || size.y <= 0 || size.x > MAX_TEXTURE_DIM || size.y > MAX_TEXTURE_DIM)
            return false;

        buffer->retain();
        m_engine.uploadRGBA(entry.tex, reinterpret_cast<const uint32_t*>(buffer->data()), size.x, size.y, buffer->stride() / 4);
        buffer->release();

        static CSurface* s_loggedShm = nullptr;
        if (s_loggedShm != surface) {
            s_loggedShm = surface;
            std::fprintf(stderr, "[colibry-shell] surface content via shm %dx%d\n", (int)size.x, (int)size.y);
        }

        entry.w = size.x;
        entry.h = size.y;
        tex     = entry.tex;
        w       = entry.w;
        h       = entry.h;
        return true;
    }

    bool CGLCompositor::fastPath(const Hyprutils::Math::CRegion& damage) {
        auto& compositor = CCompositor::get();

        const CWindow* single  = nullptr;
        int            visible = 0;
        for (const auto& window : compositor.m_windows) {
            if (!window->visible())
                continue;
            ++visible;
            single = window.get();
        }

        if (visible != 1 || !single->isFullscreen() || CSwitcher::get().active() || CLauncher::get().active() || m_fadeAlpha > 0.0)
            return false;

        uint32_t tex = 0;
        int32_t  w = 0, h = 0;
        if (!textureForSurface(single->surface(), tex, w, h))
            return false;

        int         rectCount = 0;
        const auto* rects     = pixman_region32_rectangles(damage.pixman(), &rectCount);

        for (int i = 0; i < rectCount; ++i) {
            glScissor(rects[i].x1, m_screenH - rects[i].y2, rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
            Draw::quad(m_engine, tex, 0, 0, m_screenW, m_screenH, 1.0f, 0.0f, (float)m_screenW, (float)m_screenH);
        }

        drawMask();
        return true;
    }

    void CGLCompositor::drawBackground() {
        if (m_bgDirty && m_bgW > 0 && m_bgH > 0) {
            if (!m_bgTex)
                m_bgTex = m_engine.createTexture();
            m_engine.uploadRGBA(m_bgTex, m_bgPixels.data(), m_bgW, m_bgH, m_bgW);
            m_bgDirty = false;
        }

        if (m_bgTex)
            Draw::quad(m_engine, m_bgTex, 0, 0, m_screenW, m_screenH, 1.0f, 0.0f, (float)m_screenW, (float)m_screenH);
        else
            Draw::solid(m_engine, 0.07f, 0.08f, 0.10f, 1.0f, 0, 0, m_screenW, m_screenH, 0, (float)m_screenW, (float)m_screenH);
    }

    void CGLCompositor::drawMask() {
        if (m_screenRounding <= 0)
            return;

        const uint32_t prog = m_engine.progMask();
        glUseProgram(prog);
        glUniform2f(glGetUniformLocation(prog, "uScreenSize"), (float)m_screenW, (float)m_screenH);
        glUniform1f(glGetUniformLocation(prog, "uRadius"), (float)m_screenRounding);

        const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }

    bool CGLCompositor::renderFrame(const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target,
                                    const Hyprutils::Math::CRegion&                            damage) {
        if (!ready())
            return false;

        m_screenW = target->size.x;
        m_screenH = target->size.y;

        m_engine.makeCurrent();

        if (!m_engine.bindTarget(target))
            return false;

        glViewport(0, 0, m_screenW, m_screenH);
        glEnable(GL_SCISSOR_TEST);

        auto& compositor = CCompositor::get();
        m_fadeAlpha      = 1.0 - compositor.fadeFactor(compositor.currentMs());

        if (CSwitcher::get().active()) {
            const bool ok = SwitcherDraw::renderFrame(m_engine, target, damage);
            glDisable(GL_SCISSOR_TEST);
            glFlush();
            return ok;
        }

        if (CLauncher::get().active()) {
            const bool ok = LauncherDraw::renderFrame(m_engine, target, damage);
            glDisable(GL_SCISSOR_TEST);
            glFlush();
            return ok;
        }

        if (fastPath(damage)) {
            SceneDraw::drawCursor(m_engine, m_screenW, m_screenH);
            glDisable(GL_SCISSOR_TEST);
            glFlush();
            return true;
        }

        int         rectCount = 0;
        const auto* rects     = pixman_region32_rectangles(damage.pixman(), &rectCount);

        for (int i = 0; i < rectCount; ++i) {
            glScissor(rects[i].x1, m_screenH - rects[i].y2, rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);

            drawBackground();
            SceneDraw::drawWindows(m_engine, m_screenW, m_screenH);
            SceneDraw::drawCursor(m_engine, m_screenW, m_screenH);

            if (m_fadeAlpha > 0.0)
                Draw::solid(m_engine, 0, 0, 0, (float)m_fadeAlpha, 0, 0, m_screenW, m_screenH, 0, (float)m_screenW, (float)m_screenH);

            drawMask();
        }

        glDisable(GL_SCISSOR_TEST);
        glFlush();
        return true;
    }

}
