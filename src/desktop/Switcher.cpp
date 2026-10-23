// Implementation of the window switcher overlay.

#include "Switcher.hpp"

#include "../core/Compositor.hpp"
#include "TitleBar.hpp"
#include "Window.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace Colibry {

    CSwitcher& CSwitcher::get() {
        static CSwitcher s_instance;
        return s_instance;
    }

    bool CSwitcher::active() const {
        return m_active;
    }

    void CSwitcher::open() {
        auto& compositor = CCompositor::get();

        m_entries.clear();
        for (const auto& window : compositor.m_windows) {
            if (window->mapped() && window->workspace() == compositor.currentWorkspace())
                m_entries.push_back(window.get());
        }

        if (m_entries.empty())
            return;

        m_index = 0;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i] == compositor.m_focusedWindow) {
                m_index = (int)(i + 1) % (int)m_entries.size();
                break;
            }
        }

        m_active = true;
        compositor.addFullDamage();
    }

    void CSwitcher::cycle(int direction) {
        if (m_entries.empty()) {
            cancel();
            return;
        }

        const int count = (int)m_entries.size();
        m_index         = (m_index + direction % count + count) % count;
        CCompositor::get().addFullDamage();
    }

    void CSwitcher::activate() {
        auto& compositor = CCompositor::get();

        if (m_index >= 0 && m_index < (int)m_entries.size()) {
            CWindow* window = m_entries[m_index];
            if (window && window->mapped()) {
                window->setMinimized(false);
                compositor.focusWindow(window);
            }
        }

        cancel();
    }

    void CSwitcher::cancel() {
        if (!m_active)
            return;

        m_active   = false;
        m_entries.clear();
        CCompositor::get().addFullDamage();
    }

    void CSwitcher::draw(uint32_t* pixels, int32_t stridePixels, int32_t screenWidth, int32_t screenHeight) const {
        if (!m_active)
            return;

        std::vector<const CWindow*> rows;
        for (const CWindow* window : m_entries) {
            if (window && window->mapped())
                rows.push_back(window);
        }
        if (rows.empty())
            return;

        const int rowH = TitleBar::height();

        // Dims the desktop by halving every pixel.
        for (int32_t y = 0; y < screenHeight; ++y) {
            uint32_t* row = pixels + y * stridePixels;
            for (int32_t x = 0; x < screenWidth; ++x)
                row[x] = ((row[x] & 0xFEFEFE) >> 1) | 0xFF000000;
        }

        const int32_t pad    = 12;
        const int32_t panelW = std::min(screenWidth - 80, 480);
        const int32_t panelH = (int32_t)rows.size() * rowH + 2 * pad;
        const int32_t panelX = (screenWidth - panelW) / 2;
        const int32_t panelY = (screenHeight - panelH) / 2;

        for (int32_t y = panelY; y < panelY + panelH; ++y) {
            for (int32_t x = panelX; x < panelX + panelW; ++x) {
                const bool border = y == panelY || y == panelY + panelH - 1 || x == panelX || x == panelX + panelW - 1;
                if (x >= 0 && y >= 0 && x < screenWidth && y < screenHeight)
                    pixels[y * stridePixels + x] = border ? 0xFF606060 : 0xFF2B2B2B;
            }
        }

        const int clampedIndex = std::clamp(m_index, 0, (int)rows.size() - 1);

        for (size_t i = 0; i < rows.size(); ++i) {
            const int32_t rowY     = panelY + pad + (int32_t)i * rowH;
            const bool  selected = (int)i == clampedIndex;

            if (selected) {
                for (int32_t y = rowY; y < rowY + rowH; ++y)
                    for (int32_t x = panelX + 1; x < panelX + panelW - 1; ++x)
                        pixels[y * stridePixels + x] = 0xFF505050;
            }

            std::string label = rows[i]->title().empty() ? rows[i]->appId() : rows[i]->title();
            if (label.empty())
                label = "(untitled)";
            if (rows[i]->minimized())
                label += "  — minimized";

            TitleBar::drawText(pixels, stridePixels, screenWidth, screenHeight, panelX + 12, rowY, label.c_str(), panelW - 24,
                               selected ? 0xFFFFFFFF : 0xFFAAAAAA);
        }
    }

}
