// Implementation of the per-output presentation wrapper.

#include "Output.hpp"

#include "../config/ConfigManager.hpp"
#include "../desktop/DragManager.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Display.hpp"
#include "../protocol/OutputInfo.hpp"
#include "../protocol/Surface.hpp"
#include "../render/GLCompositor.hpp"
#include "../render/SolidRenderer.hpp"
#include "../wallpaper/WallpaperEngine.hpp"
#include "Compositor.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

#include <drm_fourcc.h>
#include <pixman.h>

namespace ColibryShell {

    static constexpr uint32_t BACKGROUND_COLOR = 0xFF14171C;

    static constexpr uint64_t COMMIT_INTERVAL_MS = 17;
    static constexpr uint64_t COMMIT_BACKOFF_MS  = 24;

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Puts a region back into the global damage pool box by box (the pool
    // only accepts boxes).
    static void readdDamage(const Hyprutils::Math::CRegion& damage) {
        auto& compositor = CCompositor::get();

        int         count = 0;
        const auto* rects = pixman_region32_rectangles(damage.pixman(), &count);

        for (int i = 0; i < count; ++i) {
            compositor.addDamage({Hyprutils::Math::Vector2D((double)rects[i].x1, (double)rects[i].y1),
                                  Hyprutils::Math::Vector2D((double)(rects[i].x2 - rects[i].x1), (double)(rects[i].y2 - rects[i].y1))});
        }
    }

    COutput::COutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output) : m_output(output) {
        m_destroyListener = m_output->events.destroy.listen([this]() { onBackendDestroy(); });
        m_frameListener   = m_output->events.frame.listen([this]() { onFrame(); });
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

    uint64_t COutput::lastCommitMs() const {
        return m_lastCommitMs;
    }

    void COutput::invalidateBackground() {
        m_fullRedraw  = true;
        m_needsRender = true;
    }

    void COutput::reloadBackground() {
        m_backgroundDirty = true;
        invalidateBackground();
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
            std::fprintf(stderr, "[colibry-shell] invalid monitor resolution '%s', using preferred\n", rule->resolution.c_str());
            return fallbackMode();
        }

        Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> best;
        int bestDistance = -1;
        for (const auto& m : m_output->modes) {
            const int distance = std::abs((int)m->pixelSize.x - wantW) + std::abs((int)m->pixelSize.y - wantH);
            if (!best || distance < bestDistance) {
                best         = m;
                bestDistance = distance;
            }
        }
        return best;
    }

    bool COutput::allocateBuffer(const Hyprutils::Math::Vector2D& pixelSize) {
        const auto* rule = matchingRule();

        const double scale = (rule && rule->scale > 0.0) ? rule->scale : 1.0;
        const auto   size  = Hyprutils::Math::Vector2D(std::max(1, (int)(pixelSize.x / scale)), std::max(1, (int)(pixelSize.y / scale)));

        const auto factory = CCompositor::get().renderer();
        if (!factory) {
            std::fprintf(stderr, "[colibry-shell] %s: buffer factory unavailable\n", m_output->name.c_str());
            return false;
        }

        m_buffer = factory->createLinearBuffer(size);
        if (!m_buffer) {
            std::fprintf(stderr, "[colibry-shell] failed to allocate a buffer for %s\n", m_output->name.c_str());
            return false;
        }

        m_pixelSize       = size;
        m_backgroundDirty = true;
        m_fullRedraw      = true;
        return true;
    }

    void COutput::rebuildBackground() {
        m_backgroundPixels.assign((size_t)m_pixelSize.x * m_pixelSize.y, BACKGROUND_COLOR);

        const auto& wallpaper = CCompositor::get().m_wallpaper;
        if (wallpaper && wallpaper->hasImage())
            wallpaper->draw(m_backgroundPixels.data(), m_pixelSize.x, m_pixelSize.x, m_pixelSize.y);

        CGLCompositor::get().updateBackground(m_backgroundPixels, m_pixelSize.x, m_pixelSize.y);
    }

    bool COutput::setup() {
        const auto mode = pickMode();
        if (!mode) {
            std::fprintf(stderr, "[colibry-shell] %s: no usable mode\n", m_output->name.c_str());
            return false;
        }

        if (!allocateBuffer(mode->pixelSize))
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

    void COutput::onFrame() {
        m_lastFrameEventMs = nowMs();
        recoverFrame();
    }

    void COutput::recoverFrame() {
        if (!m_framePending)
            return;

        m_framePending = false;

        // Frame callbacks fire at presentation time.
        CCompositor::get().m_display->sendFrameDone((uint32_t)nowMs());

        if (m_needsRender)
            present();
    }

    void COutput::present() {
        if (m_inPresent || m_framePending) {
            m_needsRender = true;
            return;
        }

        const auto now = nowMs();
        if (now < m_nextCommitMs) {
            m_needsRender = true;
            return;
        }
        m_inPresent = true;

        if (!m_buffer || !CGLCompositor::get().ready()) {
            m_needsRender = true;
            m_inPresent   = false;
            return;
        }

        Hyprutils::Math::CRegion damage;
        if (m_fullRedraw) {
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
        } else {
            damage = CCompositor::get().takeDamage();
        }

        if (m_backgroundDirty) {
            rebuildBackground();
            m_backgroundDirty = false;
            damage            = Hyprutils::Math::CRegion();
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
        }

        if (CSwitcher::get().active() || CDragManager::get().active())
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));

        pixman_region32_intersect_rect(damage.pixman(), damage.pixman(), 0, 0, (int)m_pixelSize.x, (int)m_pixelSize.y);

        if (damage.empty() && m_presented) {
            m_needsRender = false;
            m_inPresent   = false;
            return;
        }

        if (!CGLCompositor::get().renderFrame(m_buffer, damage)) {
            // Render failed: the damage stays unconsumed so the next
            // attempt repaints the same region (no trails).
            if (!m_fullRedraw)
                readdDamage(damage);
            m_nextCommitMs = now + COMMIT_BACKOFF_MS;
            m_needsRender  = true;
            m_inPresent    = false;
            return;
        }

        for (const auto& window : CCompositor::get().m_windows) {
            if (window->surface())
                window->surface()->clearDamage();
        }

        m_output->state->addDamage(damage);
        m_output->state->setBuffer(m_buffer);

        if (!m_output->commit()) {
            // Rejected: a page-flip is still pending. The damage goes back
            // so a deferred commit never drops a region; completion re-arms
            // presentation through recoverFrame().
            if (!m_fullRedraw)
                readdDamage(damage);
            m_nextCommitMs = now + COMMIT_BACKOFF_MS;
            if (m_failures < 5)
                std::fprintf(stderr, "[colibry-shell] frame commit deferred for %s (flip pending)\n", m_output->name.c_str());
            ++m_failures;
            m_needsRender = true;
            m_inPresent   = false;
            return;
        }

        m_framePending = true;
        m_lastCommitMs = now;

        m_fullRedraw   = false;
        m_nextCommitMs = now + COMMIT_INTERVAL_MS;
        m_failures     = 0;

        m_needsRender = false;
        m_presented   = true;
        m_inPresent   = false;
    }

}
