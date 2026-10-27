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

    static void regionAdd(Hyprutils::Math::CRegion& dst, const Hyprutils::Math::CRegion& src) {
        pixman_region32_union(dst.pixman(), dst.pixman(), src.pixman());
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

    int COutput::pendingDamageRects() const {
        int count = 0;
        pixman_region32_rectangles(m_pendingDamage[m_bufferIndex].pixman(), &count);
        return count;
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

    bool COutput::allocateBuffers(const Hyprutils::Math::Vector2D& pixelSize) {
        const auto* rule = matchingRule();

        const double scale = (rule && rule->scale > 0.0) ? rule->scale : 1.0;
        const auto   size  = Hyprutils::Math::Vector2D(std::max(1, (int)(pixelSize.x / scale)), std::max(1, (int)(pixelSize.y / scale)));

        const auto factory = CCompositor::get().renderer();
        if (!factory) {
            std::fprintf(stderr, "[colibry-shell] %s: buffer factory unavailable\n", m_output->name.c_str());
            return false;
        }

        for (int i = 0; i < m_bufferCount; ++i) {
            m_buffers[i] = factory->createLinearBuffer(size);
            if (!m_buffers[i]) {
                std::fprintf(stderr, "[colibry-shell] failed to allocate buffer %d for %s\n", i, m_output->name.c_str());
                return false;
            }
        }

        m_pixelSize       = size;
        m_bufferIndex     = 0;
        m_backgroundDirty = true;
        m_fullRedraw      = true;
        return true;
    }

    bool COutput::growBufferPool() {
        if (m_bufferCount >= MAX_BUFFERS)
            return false;

        const auto factory = CCompositor::get().renderer();
        if (!factory)
            return false;

        const auto buffer = factory->createLinearBuffer(m_pixelSize);
        if (!buffer)
            return false;

        m_buffers[m_bufferCount] = buffer;
        m_pendingDamage[m_bufferCount].clear();
        ++m_bufferCount;

        std::fprintf(stderr, "[colibry-shell] %s: buffer pool grown to %d\n", m_output->name.c_str(), m_bufferCount);
        return true;
    }

    void COutput::rebuildBackground() {
        m_backgroundPixels.assign((size_t)m_pixelSize.x * m_pixelSize.y, BACKGROUND_COLOR);

        const auto& wallpaper = CCompositor::get().m_wallpaper;
        if (wallpaper && wallpaper->hasImage())
            wallpaper->draw(m_backgroundPixels.data(), m_pixelSize.x, m_pixelSize.x, m_pixelSize.y);

        CGLCompositor::get().updateBackground(m_backgroundPixels, m_pixelSize.x, m_pixelSize.y);
    }

    // Attaches static HDR10 metadata (PQ, P3-D65, 1000-nit peak) when HDR
    // is configured and the installed Aquamarine exposes the HDR output
    // API; without that API the build compiles this path out and the
    // output stays in SDR.
    void COutput::tryEnableHDR() {
        m_hdrActive = false;

        if (!CConfigManager::get().hdrEnabled())
            return;

#ifdef COLIBRYSHELL_AQ_HDR
        if (!m_output->supportsHDR()) {
            std::fprintf(stderr, "[colibry-shell] %s: HDR requested but the panel does not report support\n", m_output->name.c_str());
            return;
        }

        auto metadata                    = Hyprutils::Memory::makeShared<Aquamarine::SHDRMetadata>();
        metadata->type                   = Aquamarine::eDataBlockType::TYPE1;
        metadata->displayPrimariesRX     = 34000;
        metadata->displayPrimariesRY     = 16000;
        metadata->displayPrimariesGX     = 13250;
        metadata->displayPrimariesGY     = 34500;
        metadata->displayPrimariesBX     = 7500;
        metadata->displayPrimariesBY     = 3000;
        metadata->whitePointX            = 15635;
        metadata->whitePointY            = 16450;
        metadata->maxLuminance           = 1000 * 10000;
        metadata->maxFrameAverageLuminance = 400 * 10000;
        metadata->minLuminance           = 5;
        metadata->maxCLL                 = 1000;
        metadata->maxFALL                = 400;

        m_output->state->hdrMetadata = metadata;

        m_hdrActive = true;
        std::fprintf(stderr, "[colibry-shell] %s: HDR10 metadata enabled (PQ, 1000 nits peak)\n", m_output->name.c_str());
#else
        std::fprintf(stderr, "[colibry-shell] %s: HDR requested but this Aquamarine build has no HDR output API\n", m_output->name.c_str());
#endif
    }

    bool COutput::setup() {
        const auto mode = pickMode();
        if (!mode) {
            std::fprintf(stderr, "[colibry-shell] %s: no usable mode\n", m_output->name.c_str());
            return false;
        }

        if (!allocateBuffers(mode->pixelSize))
            return false;

        m_output->state->setFormat(DRM_FORMAT_XRGB8888);
        m_output->state->setEnabled(true);
        m_output->state->setMode(mode);

        tryEnableHDR();

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
        ++m_frameEvents;
        recoverFrame();
    }

    void COutput::recoverFrame() {
        if (!m_framePending)
            return;

        ++m_safetyNets;

        m_framePending = false;

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

        bool anyBuffer = true;
        for (int i = 0; i < m_bufferCount; ++i)
            anyBuffer = anyBuffer && m_buffers[i];
        if (!anyBuffer || !CGLCompositor::get().ready()) {
            m_needsRender = true;
            m_inPresent   = false;
            return;
        }

        const int index = m_bufferIndex;

        Hyprutils::Math::CRegion damage;
        if (m_fullRedraw) {
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
        } else {
            damage = CCompositor::get().takeDamage();
        }
        regionAdd(damage, m_pendingDamage[index]);
        m_pendingDamage[index].clear();

        if (m_backgroundDirty) {
            rebuildBackground();
            m_backgroundDirty = false;
            damage            = Hyprutils::Math::CRegion();
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));
            for (int i = 0; i < MAX_BUFFERS; ++i)
                m_pendingDamage[i].clear();
        }

        if (CSwitcher::get().active() || CDragManager::get().active())
            damage.add(Hyprutils::Math::CBox({0, 0}, m_pixelSize));

        pixman_region32_intersect_rect(damage.pixman(), damage.pixman(), 0, 0, (int)m_pixelSize.x, (int)m_pixelSize.y);

        if (damage.empty() && m_presented) {
            m_needsRender = false;
            m_inPresent   = false;
            return;
        }

        if (!CGLCompositor::get().renderFrame(m_buffers[index], damage)) {
            regionAdd(m_pendingDamage[index], damage);
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
        m_output->state->setBuffer(m_buffers[index]);

        if (!m_output->commit()) {
            ++m_rejections;
            regionAdd(m_pendingDamage[index], damage);
            m_nextCommitMs = now + 250;
            m_needsRender  = true;
            m_inPresent    = false;

            if (m_rejections % 3 == 0)
                growBufferPool();
            return;
        }

        m_framePending = true;
        m_lastCommitMs = now;

        for (int i = 0; i < m_bufferCount; ++i) {
            if (i != index)
                regionAdd(m_pendingDamage[i], damage);
        }

        m_bufferIndex = (m_bufferIndex + 1) % m_bufferCount;

        m_fullRedraw   = false;
        m_nextCommitMs = now + COMMIT_INTERVAL_MS;

        m_needsRender = false;
        m_presented   = true;
        m_inPresent   = false;
    }

}
