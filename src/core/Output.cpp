// Implementation of the per-output presentation wrapper.

#include "Output.hpp"

#include "../config/ConfigManager.hpp"
#include "../desktop/Shadow.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/TitleBar.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/OutputInfo.hpp"
#include "Compositor.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <drm_fourcc.h>
#include <pixman.h>

namespace Colibry {

    static constexpr uint32_t BACKGROUND_COLOR = 0xFF000000;

    // Commit pacing: 17 ms is just above the 60 Hz refresh period, so the
    // previous page flip has always completed and the cadence stays even
    // without a single "page-flip awaiting" failure.
    static constexpr uint64_t COMMIT_INTERVAL_MS = 17;
    static constexpr uint64_t COMMIT_BACKOFF_MS  = 24;

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    COutput::COutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output) : m_output(output) {
        m_destroyListener = m_output->events.destroy.listen([this]() { onBackendDestroy(); });
    }

    Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> COutput::aq() const {
        return m_output;
    }

    Hyprutils::Math::Vector2D COutput::size() const {
        return m_pixelSize;
    }

    bool COutput::needsRender() const {
        return m_needsRender;
    }

    void COutput::invalidateBackground() {
        m_backgroundDirty = true;
        m_fullRedraw      = true;
    }

    void COutput::onBackendDestroy() {
        COutputInfo::get().removeOutput(m_output->name);

        auto& outputs = CCompositor::get().m_outputs;

        for (auto it = outputs.begin(); it != outputs.end(); ++it) {
            if (it->get() == this) {
                outputs.erase(it);
                return;
            }
        }
    }

    const SMonitorRule* COutput::matchingRule() const {
        const auto& rules = CConfigManager::get().monitorRules();

        for (const auto& r : rules) {
            if (r.name.empty() || r.name == m_output->name)
                return &r;
        }
        return nullptr;
    }

    Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> COutput::pickMode() const {
        const auto* rule = matchingRule();

        auto fallbackMode = [&]() -> Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> {
            if (auto preferred = m_output->preferredMode())
                return preferred;
            return m_output->modes.empty() ? nullptr : m_output->modes.front();
        };

        if (!rule || rule->resolution == "preferred" || rule->resolution == "auto")
            return fallbackMode();

        int wantW = 0, wantH = 0;
        if (std::sscanf(rule->resolution.c_str(), "%dx%d", &wantW, &wantH) != 2 || wantW <= 0 || wantH <= 0) {
            std::fprintf(stderr, "[colibry] invalid monitor resolution '%s', using preferred\n", rule->resolution.c_str());
            return fallbackMode();
        }

        Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> best;
        int bestDistance = -1;
        for (const auto& m : m_output->modes) {
            const int distance = std::abs(static_cast<int>(m->pixelSize.x) - wantW) + std::abs(static_cast<int>(m->pixelSize.y) - wantH);
            if (!best || distance < bestDistance) {
                best         = m;
                bestDistance = distance;
            }
        }
        return best;
    }

    bool COutput::allocateBuffers(const Hyprutils::Math::Vector2D& pixelSize) {
        const auto* rule = matchingRule();

        const double scale = (rule && rule->scale > 0.0) ? rule->scale : 1.0;
        const auto   size  = Hyprutils::Math::Vector2D(
            std::max(1, static_cast<int>(pixelSize.x / scale)),
            std::max(1, static_cast<int>(pixelSize.y / scale)));

        const auto renderer = CCompositor::get().renderer();
        if (!renderer) {
            std::fprintf(stderr, "[colibry] %s: renderer unavailable\n", m_output->name.c_str());
            return false;
        }

        m_buffer = renderer->createLinearBuffer(size);
        if (!m_buffer) {
            std::fprintf(stderr, "[colibry] failed to allocate a buffer for %s\n", m_output->name.c_str());
            return false;
        }

        m_pixelSize          = size;
        m_scene.assign((size_t)size.x * size.y, BACKGROUND_COLOR);
        m_backgroundPixels.assign((size_t)size.x * size.y, BACKGROUND_COLOR);
        m_backgroundDirty    = true;
        m_fullRedraw         = true;
        return true;
    }

    void COutput::rebuildBackground() {
        m_backgroundPixels.assign((size_t)m_pixelSize.x * m_pixelSize.y, BACKGROUND_COLOR);

        const auto& wallpaper = CCompositor::get().m_wallpaper;
        if (wallpaper && wallpaper->hasImage())
            wallpaper->draw(m_backgroundPixels.data(), m_pixelSize.x, m_pixelSize.x, m_pixelSize.y);
    }

    static void blendRow(uint32_t* dst, const uint32_t* src, int32_t width, bool hasAlpha) {
        if (!hasAlpha) {
            std::memcpy(dst, src, width * 4);
            return;
        }

        for (int32_t x = 0; x < width; ++x) {
            const uint32_t p = src[x];
            const uint32_t a = p >> 24;
            if (a == 0xFF)
                dst[x] = p;
            else if (a != 0) {
                const uint32_t ia = 0xFF - a;
                const uint32_t d  = dst[x];
                dst[x]            = 0xFF000000 | ((((p & 0x00FF00FF) * a + (d & 0x00FF00FF) * ia) >> 8) & 0x00FF00FF) |
                       ((((p & 0x0000FF00) * a + (d & 0x0000FF00) * ia) >> 8) & 0x0000FF00);
            }
        }
    }

    void COutput::composite(uint32_t* pixels, int32_t stridePixels, const Hyprutils::Math::CRegion& damage) {
        const int32_t width  = m_pixelSize.x;
        const int32_t height = m_pixelSize.y;

        int         rectCount = 0;
        const auto* rects     = pixman_region32_rectangles(damage.pixman(), &rectCount);

        auto& compositor = CCompositor::get();
        const auto barH  = TitleBar::height();

        // Pass A (per rectangle): restore the cached background, then draw
        // shadows and window contents clipped to the rectangle.
        for (int i = 0; i < rectCount; ++i) {
            const int32_t rx0 = rects[i].x1;
            const int32_t ry0 = rects[i].y1;
            const int32_t rx1 = std::min(rects[i].x2, width);
            const int32_t ry1 = std::min(rects[i].y2, height);
            if (rx0 >= rx1 || ry0 >= ry1)
                continue;

            for (int32_t y = ry0; y < ry1; ++y)
                std::memcpy(pixels + y * stridePixels + rx0, m_backgroundPixels.data() + y * width + rx0, (size_t)(rx1 - rx0) * 4);

            if (const auto& shadow = compositor.m_shadow; shadow && shadow->enabled()) {
                for (const auto& window : compositor.m_windows) {
                    if (!window->visible() || window->isFullscreen())
                        continue;

                    const auto wpos  = window->position();
                    const auto wsize = window->size();
                    shadow->draw(window.get(), wpos, {wsize.x, wsize.y + barH}, damage, pixels, stridePixels, width, height);
                }
            }

            for (const auto& window : compositor.m_windows) {
                if (!window->visible())
                    continue;

                const auto buffer = window->surface()->currentBuffer();
                if (!buffer)
                    continue;

                const auto pos   = window->position();
                const auto wsize = buffer->size();

                const bool  fullscreen = window->isFullscreen();
                const auto  contentPos = fullscreen ? pos : Hyprutils::Math::Vector2D(pos.x, pos.y + barH);
                const bool  hasAlpha   = buffer->format() == WL_SHM_FORMAT_ARGB8888;
                const auto* src        = buffer->data();

                const int32_t x0 = std::max(rx0, (int32_t)contentPos.x);
                const int32_t y0 = std::max(ry0, (int32_t)contentPos.y);
                const int32_t x1 = std::min(rx1, (int32_t)(contentPos.x + wsize.x));
                const int32_t y1 = std::min(ry1, (int32_t)(contentPos.y + wsize.y));

                for (int32_t y = y0; y < y1; ++y) {
                    const uint32_t* srow =
                        reinterpret_cast<const uint32_t*>(src + (y - (int32_t)contentPos.y) * buffer->stride() + (x0 - (int32_t)contentPos.x) * 4);
                    blendRow(pixels + y * stridePixels + x0, srow, x1 - x0, hasAlpha);
                }
            }
        }

        // Pass B (once): title bars and the cursor. Region rectangles do not
        // overlap, so every pixel is written exactly once per frame.
        const auto cursorPos = compositor.m_cursor->position();
        const bool leftDown  = compositor.m_seat->leftButtonDown();

        for (const auto& window : compositor.m_windows) {
            if (!window->visible() || window->isFullscreen())
                continue;

            const auto pos   = window->position();
            const auto wsize = window->size();

            bool barDamaged = false;
            for (int i = 0; i < rectCount && !barDamaged; ++i)
                barDamaged = rects[i].x1 < pos.x + wsize.x && rects[i].x2 > pos.x && rects[i].y1 < pos.y + barH && rects[i].y2 > pos.y;

            if (!barDamaged)
                continue;

            int hoverButton = -1;
            if (cursorPos.x >= pos.x && cursorPos.x < pos.x + wsize.x && cursorPos.y >= pos.y && cursorPos.y < pos.y + barH) {
                const auto hit = TitleBar::hitTest(cursorPos - pos, wsize);
                if (hit == TitleBar::HIT_MINIMIZE)
                    hoverButton = 0;
                else if (hit == TitleBar::HIT_MAXIMIZE)
                    hoverButton = 1;
                else if (hit == TitleBar::HIT_CLOSE)
                    hoverButton = 2;
            }

            const int last = m_hoverState[window.get()];
            if (last != hoverButton) {
                m_hoverState[window.get()] = hoverButton;
                compositor.addDamage(Hyprutils::Math::CBox({pos.x, pos.y}, {wsize.x, (double)barH}));
            }

            TitleBar::draw(window.get(), compositor.m_focusedWindow == window.get(), hoverButton, leftDown, pixels, stridePixels, width, height);
        }

        if (CSwitcher::get().active())
            CSwitcher::get().draw(pixels, stridePixels, width, height);

        bool cursorDamaged = false;
        const auto cbox    = compositor.m_cursor->damageBox();
        for (int i = 0; i < rectCount && !cursorDamaged; ++i)
            cursorDamaged = rects[i].x1 < cbox.x + cbox.width && rects[i].x2 > cbox.x && rects[i].y1 < cbox.y + cbox.height && rects[i].y2 > cbox.y;

        if (cursorDamaged)
            compositor.m_cursor->draw(pixels, stridePixels, width, height);
    }

    bool COutput::setup() {
        const auto mode = pickMode();
        if (!mode) {
            std::fprintf(stderr, "[colibry] output %s has no modes, skipping\n", m_output->name.c_str());
            return false;
        }

        if (!allocateBuffers(mode->pixelSize))
            return false;

        m_output->state->setFormat(DRM_FORMAT_XRGB8888);
        m_output->state->setEnabled(true);
        m_output->state->setMode(mode);

        present();

        if (m_presented)
            CCompositor::notifyFramePresented();

        return m_presented;
    }

    void COutput::scheduleRender() {
        m_needsRender = true;
    }

    void COutput::present() {
        if (m_inPresent)
            return;

        const auto now = nowMs();
        if (now < m_nextCommitMs) {
            m_needsRender = true;
            return;
        }
        m_inPresent = true;

        if (!m_buffer) {
            m_needsRender = true;
            m_inPresent   = false;
            return;
        }

        // Determines the dirty region: a forced full redraw (first frame,
        // wallpaper change, workspace switch) or the accumulated damage.
        Hyprutils::Math::CRegion damage;
        if (m_fullRedraw)
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
        else
            damage = CCompositor::get().takeDamage();

        if (m_backgroundDirty) {
            rebuildBackground();
            m_backgroundDirty = false;
            damage            = Hyprutils::Math::CRegion();
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
        }

        if (CSwitcher::get().active()) {
            damage = Hyprutils::Math::CRegion();
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
        }

        // Nothing to do: the scene is already complete and the KMS buffer
        // holds the last committed state.
        if (damage.empty() && m_presented) {
            m_needsRender = false;
            m_inPresent   = false;
            return;
        }

        // The scene stays complete at all times; only the damaged parts are
        // redrawn and copied into the KMS buffer.
        composite(m_scene.data(), m_pixelSize.x, damage);

        const int32_t rowWidth = (int32_t)m_pixelSize.x;

        auto [data, format, size] = m_buffer->beginDataPtr(0);
        if (!data) {
            std::fprintf(stderr, "[colibry] %s: failed to map the output buffer\n", m_output->name.c_str());
            m_needsRender = true;
            m_inPresent   = false;
            return;
        }

        // The buffer is our own linear XRGB8888 allocation, so the row
        // stride in bytes is exactly the row width times four.
        int         rectCount = 0;
        const auto* rects     = pixman_region32_rectangles(damage.pixman(), &rectCount);
        for (int i = 0; i < rectCount; ++i) {
            const int32_t rx0 = std::max(rects[i].x1, 0);
            const int32_t ry0 = std::max(rects[i].y1, 0);
            const int32_t rx1 = std::min(rects[i].x2, rowWidth);
            const int32_t ry1 = std::min(rects[i].y2, (int32_t)m_pixelSize.y);
            for (int32_t y = ry0; y < ry1; ++y)
                std::memcpy(reinterpret_cast<uint8_t*>(data) + (size_t)y * (size_t)rowWidth * 4 + (size_t)rx0 * 4,
                            m_scene.data() + (size_t)y * rowWidth + rx0, (size_t)(rx1 - rx0) * 4);
        }
        m_buffer->endDataPtr();

        m_output->state->addDamage(damage);
        m_output->state->setBuffer(m_buffer);

        if (!m_output->commit()) {
            m_nextCommitMs = now + COMMIT_BACKOFF_MS;
            if (m_failures < 5)
                std::fprintf(stderr, "[colibry] frame commit failed for %s\n", m_output->name.c_str());
            ++m_failures;
            m_needsRender = true;
            m_inPresent   = false;
            return;
        }

        m_fullRedraw   = false;
        m_nextCommitMs = now + COMMIT_INTERVAL_MS;
        m_failures     = 0;

        m_needsRender = false;
        m_presented   = true;
        m_inPresent   = false;

        CCompositor::get().m_display->sendFrameDone((uint32_t)now);
    }

}
