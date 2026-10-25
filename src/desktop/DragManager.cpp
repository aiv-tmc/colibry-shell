// Implementation of interactive move and resize.
//
// A drag pins the closest cloth vertex of the window to the pointer and
// moves only that point; the rest of the surface follows through the
// cloth simulation (see Cloth.hpp), which produces the elastic fabric
// look. Releasing snaps the logical position and lets the material settle.

#include "DragManager.hpp"

#include "../core/Compositor.hpp"
#include "../protocol/Surface.hpp"
#include "ChromeGeom.hpp"
#include "Cursor.hpp"
#include "Window.hpp"

#include <chrono>

namespace Colibry {

    using Hyprutils::Math::CBox;
    using Hyprutils::Math::Vector2D;

    static constexpr auto DOUBLE_CLICK_TIME = std::chrono::milliseconds(300);

    // Rubber-band resistance range for the drag target position.
    static constexpr double RUBBER_RANGE = 80.0;

    // Distance from a screen edge that proposes a snap tile.
    static constexpr double SNAP_ZONE = 24.0;

    CDragManager& CDragManager::get() {
        static CDragManager s_instance;
        return s_instance;
    }

    bool CDragManager::active() const { return m_mode != 0; }

    bool CDragManager::snapPreview(CBox& out) const {
        if (!m_hasSnap || m_mode != 1)
            return false;
        out = m_snapRect;
        return true;
    }

    static double rubber(double overshoot) {
        return RUBBER_RANGE * (1.0 - std::exp(-overshoot / RUBBER_RANGE));
    }

    CWindow* CDragManager::windowAt(Vector2D pos) {
        auto& windows = CCompositor::get().m_windows;

        for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
            const auto& window = *it;
            if (!window->visible())
                continue;

            const auto wpos  = window->position();
            const auto wsize = window->size();

            if (pos.x >= wpos.x && pos.x < wpos.x + wsize.x && pos.y >= wpos.y && pos.y < wpos.y + wsize.y + ChromeGeom::height())
                return window.get();
        }

        return nullptr;
    }

    bool CDragManager::transparentAt(CWindow* window, Vector2D pos) {
        const auto buffer = window->surface()->currentBuffer();
        if (!buffer || buffer->format() != WL_SHM_FORMAT_ARGB8888)
            return false;

        const auto wpos  = window->position();
        const int  lx    = (int)(pos.x - wpos.x);
        const int  ly    = (int)(pos.y - wpos.y - ChromeGeom::height());
        const auto bsize = buffer->size();

        if (lx < 0 || ly < 0 || lx >= (int)bsize.x || ly >= (int)bsize.y)
            return false;

        const auto* pixel = reinterpret_cast<const uint32_t*>(buffer->data() + (size_t)ly * buffer->stride() + (size_t)lx * 4);
        return (*pixel >> 24) == 0;
    }

    void CDragManager::updateSnap(const Vector2D& cursor, const Vector2D& bounds) {
        const bool left   = cursor.x < SNAP_ZONE;
        const bool right  = cursor.x > bounds.x - SNAP_ZONE;
        const bool top    = cursor.y < SNAP_ZONE;
        const bool bottom = cursor.y > bounds.y - SNAP_ZONE;

        const double halfW = bounds.x / 2.0;
        const double halfH = bounds.y / 2.0;

        if (top && left)
            m_snapRect = CBox(Vector2D(0.0, 0.0), Vector2D(halfW, halfH));
        else if (top && right)
            m_snapRect = CBox(Vector2D(halfW, 0.0), Vector2D(halfW, halfH));
        else if (bottom && left)
            m_snapRect = CBox(Vector2D(0.0, halfH), Vector2D(halfW, halfH));
        else if (bottom && right)
            m_snapRect = CBox(Vector2D(halfW, halfH), Vector2D(halfW, halfH));
        else if (top)
            m_snapRect = CBox(Vector2D(0.0, 0.0), Vector2D(bounds.x, bounds.y));
        else if (left)
            m_snapRect = CBox(Vector2D(0.0, 0.0), Vector2D(halfW, bounds.y));
        else if (right)
            m_snapRect = CBox(Vector2D(halfW, 0.0), Vector2D(halfW, bounds.y));
        else if (bottom)
            m_snapRect = CBox(Vector2D(0.0, halfH), Vector2D(bounds.x, halfH));
        else {
            m_hasSnap = false;
            return;
        }

        m_hasSnap = true;
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
        const auto hit   = ChromeGeom::hitTest(local, window->size());

        switch (hit) {
            case ChromeGeom::HIT_CLOSE:
                window->close();
                return true;
            case ChromeGeom::HIT_MINIMIZE:
                window->setMinimized(true);
                return true;
            case ChromeGeom::HIT_MAXIMIZE: window->toggleMaximized(); return true;
            case ChromeGeom::HIT_DRAG: {
                if (local.y >= ChromeGeom::height() && !transparentAt(window, pos)) {
                    if (!superHeld)
                        return false;
                } else {
                    const auto now = std::chrono::steady_clock::now();
                    if (window == m_lastClickWindow && now - m_lastClickTime < DOUBLE_CLICK_TIME) {
                        m_lastClickWindow = nullptr;
                        window->toggleMaximized();
                        return true;
                    }
                    m_lastClickWindow = window;
                    m_lastClickTime   = now;
                }

                m_window      = window;
                m_mode        = 1;
                m_startCursor = pos;
                m_startPos    = window->position();
                m_hasSnap     = false;

                // Grab the surface at the exact pointer location: only that
                // part of the cloth follows the cursor.
                window->clothGrab(pos - m_startPos);
                window->clothMoveTo(pos);

                compositor.m_cursor->setRenderOverride(pos);
                return true;
            }
            default: break;
        }

        if (hit >= ChromeGeom::HIT_RESIZE_BASE) {
            if (window->isFullscreen() || window->isTiling())
                return true;

            m_window      = window;
            m_mode        = 2;
            m_edges       = hit & ChromeGeom::HIT_RESIZE_EDGES;
            m_startCursor = pos;
            m_startSize   = window->size();
            m_hasSnap     = false;

            compositor.m_cursor->setRenderOverride(pos);
            return true;
        }

        return false;
    }

    void CDragManager::onMove(Vector2D delta) {
        auto& compositor = CCompositor::get();

        compositor.m_cursor->moveBy(delta);

        if (m_mode == 0) {
            // No grab: refresh the hovered titlebar button.
            const auto pos = compositor.m_cursor->position();
            for (auto it = compositor.m_windows.rbegin(); it != compositor.m_windows.rend(); ++it) {
                if (!(*it)->visible())
                    continue;

                const auto box = compositor.windowBox(it->get());
                if (!box.containsPoint(pos)) {
                    (*it)->setHoveredTitleButton(-1);
                    continue;
                }

                (*it)->setHoveredTitleButton(ChromeGeom::hoveredButton(pos - box.pos(), (*it)->size()));
            }
            return;
        }

        const auto pos = compositor.m_cursor->position();

        if (m_mode == 1) {
            updateSnap(pos, compositor.m_outputs.empty() ? Vector2D(1920, 1080) : compositor.m_outputs.front()->size());

            // The drag target is rubber-banded at the screen edges; the
            // cloth pin follows it while the surface stretches.
            Vector2D target = m_startPos + (pos - m_startCursor);
            const auto bounds = compositor.m_outputs.empty() ? Vector2D(1920, 1080) : compositor.m_outputs.front()->size();

            if (target.x > 0)
                target.x = rubber(target.x);
            if (target.y > 0)
                target.y = rubber(target.y);
            if (target.x < bounds.x - m_window->size().x)
                target.x = bounds.x - m_window->size().x - rubber(m_window->size().x - (bounds.x - target.x));
            if (target.y < bounds.y - m_window->size().y - ChromeGeom::height())
                target.y = bounds.y - m_window->size().y - ChromeGeom::height() -
                    rubber(m_window->size().y + ChromeGeom::height() - (bounds.y - target.y));

            m_window->clothMoveTo(target);
        } else if (m_mode == 2)
            applyResize(pos);

        compositor.requestRender();
    }

    void CDragManager::applyResize(const Vector2D& cursor) {
        const auto delta = cursor - m_startCursor;

        Vector2D size = m_startSize;

        if (m_edges & ChromeGeom::HIT_EDGE_RIGHT)
            size.x += delta.x;
        if (m_edges & ChromeGeom::HIT_EDGE_BOTTOM)
            size.y += delta.y;
        if (m_edges & ChromeGeom::HIT_EDGE_LEFT)
            size.x -= delta.x;
        if (m_edges & ChromeGeom::HIT_EDGE_TOP)
            size.y -= delta.y;

        // Resizing reconfigures the client, which commits a new buffer;
        // the cloth nudge in resizeTo makes the new shape settle.
        m_window->resizeTo(size);
    }

    void CDragManager::onRelease() {
        auto& compositor = CCompositor::get();

        if (m_window) {
            if (m_mode == 1) {
                const auto pos = compositor.m_cursor->position();

                if (m_hasSnap) {
                    m_window->applySnapBox(m_snapRect);
                    m_window->clothMoveTo(m_snapRect.pos() + (pos - m_startCursor) * 0.0);
                } else
                    m_window->applyDragCommit(m_startPos + (pos - m_startCursor));

                // Free the pin: the cloth springs into its committed shape.
                m_window->clothRelease();
            }
        }

        m_mode            = 0;
        m_edges           = 0;
        m_window          = nullptr;
        m_hasSnap         = false;
        m_lastClickWindow = nullptr;

        compositor.m_cursor->clearRenderOverride();
        compositor.m_cursor->snapRender();
        compositor.addFullDamage();
    }

    void CDragManager::stepSprings(double /*dtMs*/) {
        // Window motion physics lives in the cloth inside CWindow; the
        // frame tick steps it through CCompositor.
    }

}
