// Implementation of interactive window move and resize.

#include "DragManager.hpp"

#include "../core/Compositor.hpp"
#include "../protocol/Buffer.hpp"
#include "../protocol/Surface.hpp"
#include "TitleBar.hpp"
#include "Window.hpp"

#include <algorithm>
#include <cstdio>

namespace Colibry {

    // Maximum delay between two title bar presses counted as one double
    // click.
    static constexpr auto DOUBLE_CLICK_TIME = std::chrono::milliseconds(300);

    CDragManager& CDragManager::get() {
        static CDragManager s_instance;
        return s_instance;
    }

    bool CDragManager::active() const {
        return m_mode != 0;
    }

    CWindow* CDragManager::windowAt(Hyprutils::Math::Vector2D pos) {
        auto& windows = CCompositor::get().m_windows;

        for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
            const auto& window = *it;
            if (!window->visible())
                continue;

            const auto wpos  = window->position();
            const auto wsize = window->size();

            if (pos.x >= wpos.x && pos.x < wpos.x + wsize.x && pos.y >= wpos.y && pos.y < wpos.y + wsize.y + TitleBar::height())
                return window.get();
        }

        return nullptr;
    }

    bool CDragManager::transparentAt(CWindow* window, Hyprutils::Math::Vector2D pos) {
        const auto buffer = window->surface()->currentBuffer();
        if (!buffer || buffer->format() != WL_SHM_FORMAT_ARGB8888)
            return false;

        const auto wpos  = window->position();
        const int  lx    = (int)(pos.x - wpos.x);
        const int  ly    = (int)(pos.y - wpos.y - TitleBar::height());
        const auto bsize = buffer->size();

        if (lx < 0 || ly < 0 || lx >= (int)bsize.x || ly >= (int)bsize.y)
            return false;

        const auto* pixel = reinterpret_cast<const uint32_t*>(buffer->data() + (size_t)ly * buffer->stride() + (size_t)lx * 4);
        return (*pixel >> 24) == 0;
    }

    bool CDragManager::onPress(uint32_t /*timeMs*/, bool superHeld) {
        auto& compositor = CCompositor::get();
        const auto pos   = compositor.m_cursor->position();

        CWindow* window = windowAt(pos);
        if (!window) {
            compositor.focusWindow(nullptr);
            return false;
        }

        compositor.focusWindow(window);

        const auto wpos  = window->position();
        const auto local = pos - wpos;
        const auto hit   = TitleBar::hitTest(local, window->size());

        switch (hit) {
            case TitleBar::HIT_CLOSE:
                window->close();
                return true;
            case TitleBar::HIT_MINIMIZE:
                window->setMinimized(true);
                return true;
            case TitleBar::HIT_MAXIMIZE:
                window->toggleMaximized();
                return true;
            case TitleBar::HIT_DRAG: {
                if (local.y >= TitleBar::height() && !transparentAt(window, pos)) {
                    // Opaque client content reaches the client, unless
                    // SUPER is held: then it drags the window from any
                    // point.
                    if (!superHeld)
                        return false;

                    m_window      = window;
                    m_mode        = 1;
                    m_startCursor = pos;
                    m_startPos    = window->position();
                    return true;
                }

                // Double click on the title bar toggles maximization.
                const auto now = std::chrono::steady_clock::now();
                if (window == m_lastClickWindow && now - m_lastClickTime < DOUBLE_CLICK_TIME) {
                    m_lastClickWindow = nullptr;
                    window->toggleMaximized();
                    return true;
                }
                m_lastClickWindow = window;
                m_lastClickTime   = now;

                m_window      = window;
                m_mode        = 1;
                m_startCursor = pos;
                m_startPos    = window->position();
                return true;
            }
            default:
                if (hit & TitleBar::HIT_RESIZE_BASE) {
                    m_window      = window;
                    m_mode        = 2;
                    m_edges       = hit & 0xF00;
                    m_startCursor = pos;
                    m_startPos    = window->position();
                    m_startSize   = window->size();
                    return true;
                }
                return false;
        }
    }

    void CDragManager::onMove(Hyprutils::Math::Vector2D delta) {
        if (!m_window || !m_mode)
            return;

        auto&      cursor = CCompositor::get().m_cursor;
        const auto pos    = cursor->position();

        if (m_mode == 1) {
            m_window->setPosition({m_startPos.x + pos.x - m_startCursor.x, m_startPos.y + pos.y - m_startCursor.y});
            return;
        }

        // Resizes: the size follows the dragged edges; left/top edges also
        // move the window so the opposite edge stays in place.
        const double dx = pos.x - m_startCursor.x;
        const double dy = pos.y - m_startCursor.y;

        double newW = m_startSize.x;
        double newH = m_startSize.y;

        if (m_edges & 0x200)
            newW += dx;
        if (m_edges & 0x100)
            newW -= dx;
        if (m_edges & 0x800)
            newH += dy;
        if (m_edges & 0x400)
            newH -= dy;

        newW = std::max(100.0, newW);
        newH = std::max(100.0, newH);

        double newX = m_startPos.x;
        double newY = m_startPos.y;
        if (m_edges & 0x100)
            newX = m_startPos.x + (m_startSize.x - newW);
        if (m_edges & 0x400)
            newY = m_startPos.y + (m_startSize.y - newH);

        m_window->setPosition({newX, newY});
        m_window->resizeTo({newW, newH});

        // No size indicator is drawn during resizing, per the shell rules.
    }

    void CDragManager::onRelease() {
        m_window = nullptr;
        m_mode   = 0;
        m_edges  = 0;
    }

}
