// Implementation of the compositor coordinator.

#include "Compositor.hpp"

#include "../config/ConfigManager.hpp"
#include "../desktop/Keybinds.hpp"
#include "../desktop/Layout.hpp"
#include "../desktop/Shadow.hpp"
#include "../desktop/TitleBar.hpp"
#include "../ipc/IPC.hpp"
#include "../protocol/DataDevice.hpp"
#include "../protocol/ForeignToplevel.hpp"
#include "../protocol/OutputInfo.hpp"
#include "../protocol/Surface.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <poll.h>
#include <unistd.h>

namespace Colibry {

    // Scheduler tick in milliseconds: short enough that accumulated damage
    // is picked up almost immediately, while commit pacing stays at the
    // display refresh period.
    static constexpr uint64_t FRAME_INTERVAL_MS = 4;

    static constexpr int MAX_WORKSPACE = 10;

    static volatile sig_atomic_t g_framePresented = 0;

    static const char* logPrefix(Aquamarine::eBackendLogLevel level) {
        switch (level) {
            case Aquamarine::AQ_LOG_TRACE: return "trace";
            case Aquamarine::AQ_LOG_DEBUG: return "debug";
            case Aquamarine::AQ_LOG_WARNING: return "warning";
            case Aquamarine::AQ_LOG_ERROR: return "error";
            case Aquamarine::AQ_LOG_CRITICAL: return "critical";
        }
        return "unknown";
    }

    static void backendLog(Aquamarine::eBackendLogLevel level, const std::string& message) {
        if (level <= Aquamarine::AQ_LOG_DEBUG)
            return;
        std::fprintf(stderr, "[colibry backend:%s] %s\n", logPrefix(level), message.c_str());
    }

    static void watchdogHandler(int /*sig*/) {
        static const char MSG[] = "[colibry] watchdog: no frame presented at startup, exiting\n";
        write(STDERR_FILENO, MSG, sizeof(MSG) - 1);
        _exit(1);
    }

    static std::string resolveTerminal() {
        const char* env = std::getenv("COLIBRY_TERMINAL");
        if (env && *env)
            return env;

        const auto& configured = CConfigManager::get().terminal();
        if (!configured.empty())
            return configured;

        return "foot || kitty || alacritty || xterm || weston-terminal";
    }

    static void spawnAutostart(const CDisplay* display) {
        const auto command = resolveTerminal();

        const auto pid = fork();
        if (pid != 0)
            return;

        setsid();
        setenv("WAYLAND_DISPLAY", display->socketName().c_str(), 1);

        execlp("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));

        std::fprintf(stderr, "[colibry] no terminal found; install one (pacman -S foot) or set terminal= in colibry.toml\n");
        _exit(1);
    }

    CCompositor& CCompositor::get() {
        static CCompositor s_instance;
        return s_instance;
    }

    void CCompositor::requestExit() {
        get().m_eventLoop.quit();
    }

    void CCompositor::notifyFramePresented() {
        g_framePresented = 1;
        alarm(0);
    }

    std::vector<Aquamarine::SBackendImplementationOptions> CCompositor::backendOptions() const {
        Aquamarine::SBackendImplementationOptions drm;
        drm.backendType        = Aquamarine::AQ_BACKEND_DRM;
        drm.backendRequestMode = Aquamarine::AQ_BACKEND_REQUEST_IF_AVAILABLE;

        Aquamarine::SBackendImplementationOptions wayland;
        wayland.backendType        = Aquamarine::AQ_BACKEND_WAYLAND;
        wayland.backendRequestMode = Aquamarine::AQ_BACKEND_REQUEST_IF_AVAILABLE;

        return {drm, wayland};
    }

    bool CCompositor::init() {
        std::fprintf(stderr, "[colibry] starting compositor %s\n", COLIBRY_VERSION);

        m_config = Hyprutils::Memory::makeShared<CConfigManager>();
        m_config->load();

        for (const auto& def : m_config->binds())
            CKeybindManager::get().addBind(def);

        m_wallpaper = Hyprutils::Memory::makeShared<CWallpaper>();
        m_wallpaper->setFit(m_config->wallpaperFit());
        if (!m_config->wallpaperPath().empty())
            m_wallpaper->load(m_config->wallpaperPath());

        m_shadow = Hyprutils::Memory::makeShared<CShadow>();
        m_shadow->configure(m_config->shadowEnabled(), 0xFF000000, m_config->shadowOffsetX(), m_config->shadowOffsetY(), m_config->shadowRadius(),
                            m_config->shadowRounding(), m_config->shadowOpacity());

        TitleBar::configure(m_config->titlebarHeight(), m_config->titlebarButtonWidth());
        TitleBar::initFont(m_config->titlebarFont());

        m_cursor = Hyprutils::Memory::makeShared<CCursor>();
        m_cursor->load(m_config->cursorTheme(), m_config->cursorSize());

        m_display = Hyprutils::Memory::makeShared<CDisplay>();
        if (!m_display->init()) {
            std::fprintf(stderr, "[colibry] failed to initialize the wayland display\n");
            return false;
        }

        m_seat = Hyprutils::Memory::makeShared<CSeat>();
        if (!m_seat->init(m_display.get())) {
            std::fprintf(stderr, "[colibry] failed to initialize the seat\n");
            return false;
        }

        m_xdgShell = Hyprutils::Memory::makeShared<CXdgShell>();
        if (!m_xdgShell->init(m_display.get())) {
            std::fprintf(stderr, "[colibry] failed to initialize xdg-shell\n");
            return false;
        }

        if (!CDataDeviceManager::get().init(m_display.get()))
            std::fprintf(stderr, "[colibry] clipboard unavailable\n");

        Aquamarine::SBackendOptions options;
        options.logFunction = backendLog;

        m_backend = Aquamarine::CBackend::create(backendOptions(), options);
        if (!m_backend) {
            std::fprintf(stderr, "[colibry] failed to create backend\n");
            return false;
        }

        m_newOutputListener = m_backend->events.newOutput.listen([](const Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput>& output) {
            get().onNewOutput(output);
        });

        m_newKeyboardListener = m_backend->events.newKeyboard.listen([](const Hyprutils::Memory::CSharedPointer<Aquamarine::IKeyboard>& keyboard) {
            get().m_seat->onNewKeyboard(keyboard);
        });

        m_newPointerListener = m_backend->events.newPointer.listen([](const Hyprutils::Memory::CSharedPointer<Aquamarine::IPointer>& pointer) {
            get().m_seat->onNewPointer(pointer);
        });

        if (!m_backend->start()) {
            std::fprintf(stderr, "[colibry] failed to start backend\n");
            return false;
        }

        if (m_backend->hasSession())
            std::fprintf(stderr, "[colibry] DRM session acquired\n");
        else
            std::fprintf(stderr, "[colibry] running without a DRM session (nested mode)\n");

        registerPollFDs();

        m_eventLoop.addFD(m_display->fd(), POLLIN | POLLOUT, [this](short) { m_display->dispatch(); });

        m_eventLoop.addTimer(FRAME_INTERVAL_MS, [] {
            auto& self = get();
            for (const auto& output : self.m_outputs) {
                if (output->needsRender())
                    output->present();
            }
        });

        m_ipc = Hyprutils::Memory::makeShared<CIPC>();
        if (!m_ipc->start(m_eventLoop)) {
            std::fprintf(stderr, "[colibry] IPC unavailable, continuing without it\n");
            m_ipc.reset();
        }

        spawnAutostart(m_display.get());

        std::fprintf(stderr, "[colibry] %zu output(s) active\n", m_outputs.size());
        return true;
    }

    Hyprutils::Memory::CSharedPointer<CClearRenderer> CCompositor::renderer() {
        if (m_renderer)
            return m_renderer;

        if (!m_backend || !m_backend->primaryAllocator)
            return nullptr;

        m_renderer = Hyprutils::Memory::makeShared<CClearRenderer>();
        if (!m_renderer->init(m_backend->primaryAllocator->drmFD())) {
            std::fprintf(stderr, "[colibry] failed to initialize the renderer\n");
            m_renderer.reset();
        }

        return m_renderer;
    }

    CSurface* CCompositor::surfaceAt(Hyprutils::Math::Vector2D pos) {
        for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
            const auto& window = *it;
            if (!window->visible())
                continue;

            const auto wpos  = window->position();
            const auto wsize = window->size();

            if (pos.x >= wpos.x && pos.x < wpos.x + wsize.x && pos.y >= wpos.y + TitleBar::height() && pos.y < wpos.y + wsize.y + TitleBar::height())
                return window->surface();
        }

        return nullptr;
    }

    void CCompositor::focusWindow(CWindow* window) {
        if (m_focusedWindow == window)
            return;

        CWindow* previous = m_focusedWindow;

        m_focusedWindow = window;
        m_seat->setKeyboardFocus(window ? window->surface() : nullptr);

        if (previous) {
            addDamage(windowBox(previous));
            CForeignToplevelManager::get().onWindowState(previous);
        }
        if (window) {
            addDamage(windowBox(window));
            CForeignToplevelManager::get().onWindowState(window);

            for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
                if (it->get() == window) {
                    auto raised = *it;
                    m_windows.erase(it);
                    m_windows.push_back(raised);
                    break;
                }
            }
        }

        requestRender();
    }

    void CCompositor::focusSurface(CSurface* surface) {
        if (!surface)
            return;

        for (const auto& window : m_windows) {
            if (window->surface() == surface && window->visible()) {
                focusWindow(window.get());
                return;
            }
        }
    }

    void CCompositor::focusNext() {
        for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
            if (!(*it)->visible())
                continue;
            if (it->get() == m_focusedWindow)
                continue;

            if (m_focusedWindow) {
                auto after = std::find_if(m_windows.rbegin(), m_windows.rend(), [this](const auto& w) { return w.get() == m_focusedWindow; });
                if (after != m_windows.rend()) {
                    auto next = after + 1;
                    if (next == m_windows.rend())
                        next = m_windows.rbegin();
                    (*next)->setMinimized(false);
                    focusWindow(next->get());
                    return;
                }
            }

            (*it)->setMinimized(false);
            focusWindow(it->get());
            return;
        }

        focusWindow(nullptr);
    }

    void CCompositor::focusPrev() {
        for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
            if (!(*it)->visible())
                continue;
            if (it->get() == m_focusedWindow)
                continue;

            if (m_focusedWindow) {
                auto after = std::find_if(m_windows.begin(), m_windows.end(), [this](const auto& w) { return w.get() == m_focusedWindow; });
                if (after != m_windows.end()) {
                    auto next = after + 1;
                    if (next == m_windows.end())
                        next = m_windows.begin();
                    (*next)->setMinimized(false);
                    focusWindow(next->get());
                    return;
                }
            }

            (*it)->setMinimized(false);
            focusWindow(it->get());
            return;
        }

        focusWindow(nullptr);
    }

    void CCompositor::removeWindow(CWindow* window) {
        if (m_shadow)
            m_shadow->invalidate(window);

        if (m_focusedWindow == window) {
            m_focusedWindow = nullptr;
            m_seat->setKeyboardFocus(nullptr);
        }

        for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
            if (it->get() == window) {
                m_windows.erase(it);
                break;
            }
        }

        addFullDamage();
    }

    int CCompositor::currentWorkspace() const {
        return m_currentWorkspace;
    }

    void CCompositor::switchWorkspace(int workspace) {
        workspace = std::clamp(workspace, 1, MAX_WORKSPACE);
        if (workspace == m_currentWorkspace)
            return;

        m_currentWorkspace = workspace;
        std::fprintf(stderr, "[colibry] workspace %d\n", workspace);

        CWindow* top = nullptr;
        for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
            if ((*it)->visible()) {
                top = it->get();
                break;
            }
        }
        focusWindow(top);

        applyTiling();
        refreshForeignToplevel();
        addFullDamage();
    }

    void CCompositor::moveWindowToWorkspace(CWindow* window, int workspace) {
        if (!window)
            return;

        workspace = std::clamp(workspace, 1, MAX_WORKSPACE);
        window->setWorkspace(workspace);

        if (workspace == m_currentWorkspace)
            applyTiling();
        addFullDamage();
    }

    bool CCompositor::workspaceHasTiling(int workspace) const {
        for (const auto& window : m_windows) {
            if (window->workspace() == workspace && window->isTiling())
                return true;
        }
        return false;
    }

    int CCompositor::firstEmptyWorkspace() const {
        for (int candidate = 1; candidate <= MAX_WORKSPACE; ++candidate) {
            bool empty = true;
            for (const auto& window : m_windows) {
                if (window->mapped() && window->workspace() == candidate) {
                    empty = false;
                    break;
                }
            }
            if (empty)
                return candidate;
        }
        return MAX_WORKSPACE;
    }

    void CCompositor::applyTiling() {
        Layout::applyTiling();
    }

    void CCompositor::refreshForeignToplevel() {
        for (const auto& window : m_windows)
            CForeignToplevelManager::get().onWindowState(window.get());
    }

    void CCompositor::addDamage(const Hyprutils::Math::CBox& box) {
        m_damage.add(box);
        requestRender();
    }

    void CCompositor::addFullDamage() {
        for (const auto& output : m_outputs) {
            output->invalidateBackground();
            output->scheduleRender();
        }
        m_damage.add(Hyprutils::Math::CBox({0, 0}, {100000, 100000}));
    }

    Hyprutils::Math::CRegion CCompositor::takeDamage() {
        // Returns exactly the accumulated region (possibly empty) and
        // clears it; an empty region means nothing needs redrawing.
        Hyprutils::Math::CRegion taken = m_damage;
        m_damage.clear();
        return taken;
    }

    Hyprutils::Math::CBox CCompositor::windowBox(const CWindow* window) const {
        if (window->isFullscreen())
            return {window->position().x, window->position().y, window->size().x, window->size().y};

        return {window->position().x, window->position().y, window->size().x, window->size().y + TitleBar::height()};
    }

    void CCompositor::requestRender() {
        for (const auto& output : m_outputs)
            output->scheduleRender();
    }

    void CCompositor::registerPollFDs() {
        for (const auto& pollFD : m_backend->getPollFDs()) {
            m_eventLoop.addFD(pollFD->fd, POLLIN, [pollFD](short) { pollFD->onSignal(); });
        }
    }

    void CCompositor::onNewOutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output) {
        std::fprintf(stderr, "[colibry] output '%s' appeared\n", output->name.c_str());

        auto ctx = Hyprutils::Memory::makeShared<COutput>(output);

        if (ctx->setup()) {
            COutputInfo::get().addOutput(output->name, ctx->size());

            if (m_outputs.empty()) {
                m_cursor->setBounds(ctx->size());
                m_cursor->setPosition(ctx->size() / 2.0);
                m_cursor->snapRender();
            }
            m_outputs.push_back(ctx);
        }
    }

    int CCompositor::run() {
        std::signal(SIGINT, [](int) { requestExit(); });
        std::signal(SIGTERM, [](int) { requestExit(); });

        std::signal(SIGALRM, watchdogHandler);
        if (!g_framePresented)
            alarm(20);

        std::fprintf(stderr, "[colibry] entering event loop\n");

        return m_eventLoop.run();
    }

    void CCompositor::shutdown() {
        std::fprintf(stderr, "[colibry] shutting down\n");

        m_windows.clear();
        m_outputs.clear();
        m_ipc.reset();
        m_renderer.reset();
        m_xdgShell.reset();
        m_seat.reset();
        m_shadow.reset();
        m_cursor.reset();
        m_wallpaper.reset();
        if (m_display)
            m_display->destroy();
        m_display.reset();
        m_backend.reset();
    }

}
