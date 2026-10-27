// Implementation of the GPU scene compositor.

#include "GLCompositor.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Cursor.hpp"
#include "../desktop/Launcher.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Buffer.hpp"
#include "../protocol/Surface.hpp"
#include "Draw.hpp"
#include "LauncherDraw.hpp"
#include "SceneDraw.hpp"
#include "SwitcherDraw.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cstdio>
#include <pixman.h>

#include <drm_fourcc.h>

namespace ColibryShell {

    static constexpr int32_t MAX_TEXTURE_DIM = 16384;

    // Mesa clients routinely produce the BGR little-endian variants; all
    // four 32-bit formats the renderer can sample are accepted.
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
        if (dma.valid) {
            if (!dmaImportable(dma)) {
                static CSurface* s_loggedSurface = nullptr;
                if (s_loggedSurface != surface) {
                    s_loggedSurface = surface;
                    std::fprintf(stderr, "[colibry-shell] surface content rejected: dma fourcc=0x%x %ux%u stride=%u modifier=0x%lx\n", dma.format, dma.width,
                                 dma.height, dma.stride, (unsigned long)dma.modifier);
                }
                return false;
            }

            if (!m_engine.importDMA(dma, entry.tex)) {
                static CSurface* s_loggedImport = nullptr;
                if (s_loggedImport != surface) {
                    s_loggedImport = surface;
                    std::fprintf(stderr, "[colibry-shell] dma import failed for surface (fourcc=0x%x modifier=0x%lx)\n", dma.format,
                                 (unsigned long)dma.modifier);
                }
                return false;
            }

            entry.w = (int32_t)dma.width;
            entry.h = (int32_t)dma.height;
            tex     = entry.tex;
            w       = entry.w;
            h       = entry.h;
            return true;
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
