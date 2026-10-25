// Implementation of the desktop window.

#include "Window.hpp"

#include "../core/Compositor.hpp"
#include "../core/Output.hpp"
#include "ChromeGeom.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include <hyprutils/memory/SharedPtr.hpp>

#include <wayland-server.h>

namespace Colibry {

    using Hyprutils::Math::CBox;
    using Hyprutils::Math::Vector2D;

    static const Vector2D MIN_SIZE = {80, 60};

    // xdg_toplevel protocol constants.
    static constexpr uint32_t XDG_TOPLEVEL_EVENT_CONFIGURE = 0;
    static constexpr uint32_t XDG_TOPLEVEL_EVENT_CLOSE     = 1;
    static constexpr uint32_t XDG_TOPLEVEL_STATE_MAXIMIZED  = 1;
    static constexpr uint32_t XDG_TOPLEVEL_STATE_FULLSCREEN = 2;

    // Closing animation length and the grace period before an unresponsive
    // client is force-removed.
    static constexpr double CLOSE_FADE_MS = 150.0;
    static constexpr double CLOSE_FORCE_MS = 400.0;

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    CWindow::CWindow(CSurface* surface, wl_resource* toplevelResource, wl_resource* xdgSurfaceResource) :
        m_surface(surface), m_toplevelResource(toplevelResource), m_xdgSurfaceResource(xdgSurfaceResource) {
        m_surface->onCommit = [this](CSurface*) { onSurfaceCommit(); };

        // The window joins the desktop model at construction time so that
        // requests arriving before the first commit already see it.
        m_self = Hyprutils::Memory::CSharedPointer<CWindow>(this);
        CCompositor::get().m_windows.push_back(m_self);
    }

    CWindow::~CWindow() {
        if (m_surface)
            m_surface->onCommit = nullptr;
        CCompositor::get().removeWindow(this);
    }

    CSurface* CWindow::surface() const { return m_surface; }
    bool      CWindow::mapped() const { return m_mapped; }

    bool CWindow::visible() const {
        return m_mapped && !m_minimized && m_workspace == CCompositor::get().currentWorkspace();
    }

    bool CWindow::minimized() const { return m_minimized; }
    Vector2D CWindow::position() const { return m_position; }
    Vector2D CWindow::size() const { return m_size; }
    int      CWindow::workspace() const { return m_workspace; }

    void CWindow::setWorkspace(int workspace) {
        m_workspace = workspace;
        CCompositor::get().refreshForeignToplevel();
    }

    bool CWindow::isTiling() const { return m_tiling; }

    void CWindow::toggleTiling() {
        m_forceTiling = !m_forceTiling;
        m_tiling      = m_forceTiling;
        CCompositor::get().applyTiling();
        CCompositor::get().addDamage(CCompositor::get().windowBox(this));
        CCompositor::get().refreshForeignToplevel();
    }

    bool CWindow::isFullscreen() const { return m_fullscreen; }

    void CWindow::setFullscreen(bool fullscreen) {
        if (m_fullscreen == fullscreen)
            return;

        clothNudge();

        if (fullscreen) {
            m_prevPosition = m_position;
            m_prevSize     = m_size;
            m_prevTiling   = m_tiling;

            const auto screen = CCompositor::get().m_outputs.empty() ? Vector2D(1920, 1080) : CCompositor::get().m_outputs.front()->size();
            m_position = {0, 0};
            m_size     = screen;
            m_tiling   = false;

            sendConfigure(m_size, {XDG_TOPLEVEL_STATE_FULLSCREEN});
        } else {
            m_position = m_prevPosition;
            m_size     = m_prevSize;
            m_tiling   = m_prevTiling;
            sendConfigure(m_size, {});
        }

        m_fullscreen = fullscreen;
        CCompositor::get().addFullDamage();
        CCompositor::get().refreshForeignToplevel();
    }

    void CWindow::toggleFullscreen() { setFullscreen(!m_fullscreen); }

    void CWindow::openTiledFullscreen() {
        m_forceTiling = true;
        m_tiling      = true;
        setFullscreen(true);
    }

    bool CWindow::maximized() const { return m_maximized; }

    void CWindow::toggleMaximized() {
        if (m_fullscreen)
            return;

        clothNudge();

        if (!m_maximized) {
            m_prevPosition = m_position;
            m_prevSize     = m_size;
            const auto screen = CCompositor::get().m_outputs.empty() ? Vector2D(1920, 1080) : CCompositor::get().m_outputs.front()->size();
            m_position = {0, 0};
            m_size     = Vector2D(screen.x, screen.y - ChromeGeom::height());
            sendConfigure(m_size, {XDG_TOPLEVEL_STATE_MAXIMIZED});
        } else {
            m_position = m_prevPosition;
            m_size     = m_prevSize;
            sendConfigure(m_size, {});
        }

        m_maximized = !m_maximized;
        CCompositor::get().addFullDamage();
    }

    void   CWindow::setAppId(const std::string& appId) { m_appId = appId; }
    const std::string& CWindow::appId() const { return m_appId; }

    void CWindow::close() {
        if (m_closing)
            return;
        m_closing = true;
        clothNudge();
        CCompositor::get().requestRender();
    }

    void CWindow::sendClose() {
        if (m_toplevelResource)
            wl_resource_post_event(m_toplevelResource, XDG_TOPLEVEL_EVENT_CLOSE);
    }

    bool CWindow::closeExpired(uint64_t now) const {
        return m_closeSent && now - m_closeSentMs > (uint64_t)CLOSE_FORCE_MS;
    }

    Vector2D CWindow::clampedPosition(Vector2D pos) const {
        const auto screen = CCompositor::get().m_outputs.empty() ? Vector2D(1920, 1080) : CCompositor::get().m_outputs.front()->size();
        pos.x = std::clamp(pos.x, screen.x - m_size.x - 40.0, 40.0);
        pos.y = std::clamp(pos.y, screen.y - m_size.y - ChromeGeom::height() - 40.0, 0.0);
        return pos;
    }

    void CWindow::setPosition(Vector2D pos) {
        m_position = clampedPosition(pos);
        clothNudge();
        CCompositor::get().addDamage(CCompositor::get().windowBox(this));
    }

    void CWindow::setDragPosition(Vector2D pos) {
        m_position = pos;
        CCompositor::get().requestRender();
    }

    void CWindow::applyDragCommit(const Vector2D& pos) {
        setDragPosition(pos);
        clothNudge();
    }

    void CWindow::applySnapBox(const CBox& box) {
        setDragPosition(box.pos());
        resizeTo(box.size());
        configure(box.size(), {});
        clothNudge();
    }

    void CWindow::moveBy(Vector2D delta) { setPosition(m_position + delta); }

    void CWindow::resizeTo(Vector2D size) {
        m_size = Vector2D(std::max(MIN_SIZE.x, size.x), std::max(MIN_SIZE.y, size.y));

        if (m_mapped && !m_tiling && !m_fullscreen)
            sendConfigure(m_size, m_maximized ? std::vector<uint32_t>{XDG_TOPLEVEL_STATE_MAXIMIZED} : std::vector<uint32_t>{});

        clothNudge();
        CCompositor::get().addDamage(CCompositor::get().windowBox(this));
    }

    void CWindow::setMinimized(bool minimized) {
        if (m_minimized == minimized)
            return;
        m_minimized = minimized;
        CCompositor::get().addFullDamage();
        CCompositor::get().refreshForeignToplevel();
    }

    void CWindow::configure(Vector2D size, const std::vector<uint32_t>& states) { sendConfigure(size, states); }

    void CWindow::sendConfigure(Vector2D size, const std::vector<uint32_t>& states) {
        wl_client* client = wl_resource_get_client(m_xdgSurfaceResource);

        struct wl_array stateArray;
        wl_array_init(&stateArray);
        for (const uint32_t state : states) {
            uint32_t* entry = static_cast<uint32_t*>(wl_array_add(&stateArray, sizeof(uint32_t)));
            if (entry)
                *entry = state;
        }

        wl_resource_post_event(m_toplevelResource, XDG_TOPLEVEL_EVENT_CONFIGURE, (uint32_t)size.x, (uint32_t)size.y, &stateArray);
        wl_resource_post_event(m_xdgSurfaceResource, 0, wl_display_next_serial(wl_client_get_display(client)));

        wl_array_release(&stateArray);
    }

    void              CWindow::setTitle(const std::string& title) { m_title = title; }
    const std::string& CWindow::title() const { return m_title; }

    double CWindow::openProgress() const { return m_openProgress; }

    void CWindow::setOpenProgress(double progress) {
        const double next = std::clamp(progress, 0.0, 1.0);
        if (next == m_openProgress)
            return;
        m_openProgress = next;
        CCompositor::get().addDamage(CCompositor::get().windowBox(this));
    }

    double CWindow::opacity() const { return m_openProgress * m_closeAlpha; }

    int  CWindow::hoveredTitleButton() const { return m_hoveredButton; }

    void CWindow::setHoveredTitleButton(int button) {
        if (m_hoveredButton == button)
            return;
        m_hoveredButton = button;
        CCompositor::get().requestRender();
    }

    void   CWindow::clothGrab(const Vector2D& local) { m_cloth.grab(local); }
    void   CWindow::clothMoveTo(const Vector2D& target) { m_cloth.moveTo(target); }
    void   CWindow::clothRelease() { m_cloth.release(); }
    void   CWindow::clothNudge() { if (m_mapped) m_cloth.nudge(); }
    bool   CWindow::clothActive() const { return m_cloth.active(); }
    const SClothMesh& CWindow::clothMesh() const { return m_cloth.mesh(); }

    bool CWindow::clothStep(double dtMs) {
        if (m_closing) {
            m_closeAlpha = std::max(0.0, m_closeAlpha - dtMs / CLOSE_FADE_MS);
            if (m_closeAlpha == 0.0 && !m_closeSent) {
                sendClose();
                m_closeSent   = true;
                m_closeSentMs = nowMs();
            }
        }

        const auto box = CCompositor::get().windowBox(this);
        const bool moving = m_cloth.step(box, ChromeGeom::height(), dtMs, m_closing, m_closeAlpha);

        if (moving) {
            // Damage the box plus slack for the maximum ripple excursion.
            CCompositor::get().addDamage(CBox(box.pos() - Vector2D(64, 64), box.size() + Vector2D(128, 128)));
        }

        return moving || m_closing;
    }

    void CWindow::onMapped() {
        m_mapped = true;

        const auto screen = CCompositor::get().m_outputs.empty() ? Vector2D(1920, 1080) : CCompositor::get().m_outputs.front()->size();
        m_position = (screen - Vector2D(m_size.x, m_size.y + ChromeGeom::height())) / 2.0;
        m_position = clampedPosition(m_position);

        if (m_forceTiling && m_fullscreen)
            m_workspace = CCompositor::get().firstEmptyWorkspace();
        else
            m_workspace = CCompositor::get().currentWorkspace();

        m_openProgress = 0.0;

        // The surface grows in from 72% scale with an elastic overshoot.
        m_cloth.reset(CCompositor::get().windowBox(this), ChromeGeom::height(), 0.72);

        CCompositor::get().addFullDamage();
        CCompositor::get().focusWindow(this);
        CCompositor::get().refreshForeignToplevel();
    }

    void CWindow::onSurfaceCommit() {
        if (!m_mapped) {
            if (m_surface->mapped())
                onMapped();
            return;
        }

        if (!m_surface->mapped()) {
            m_mapped = false;
            CCompositor::get().removeWindow(this);
            return;
        }

        if (!m_tiling && !m_fullscreen && !m_maximized) {
            const auto buffer = m_surface->currentBuffer();
            if (buffer && buffer->size() != m_size) {
                m_size = buffer->size();
                clothNudge();
                CCompositor::get().addDamage(CCompositor::get().windowBox(this));
            }
        }

        CCompositor::get().requestRender();
    }

}
