// Implementation of the compositor coordinator.

#include "Compositor.hpp"

#include "../config/ConfigManager.hpp"
#include "../desktop/ChromeGeom.hpp"
#include "../desktop/Cursor.hpp"
#include "../desktop/DragManager.hpp"
#include "../desktop/Keybinds.hpp"
#include "../desktop/Launcher.hpp"
#include "../desktop/Window.hpp"
#include "../ipc/IPC.hpp"
#include "../protocol/DataDevice.hpp"
#include "../protocol/Display.hpp"
#include "../protocol/ForeignToplevel.hpp"
#include "../protocol/OutputInfo.hpp"
#include "../protocol/Seat.hpp"
#include "../protocol/Surface.hpp"
#include "../protocol/XdgShell.hpp"
#include "../render/GLCompositor.hpp"
#include "../render/SceneDraw.hpp"
#include "../render/SolidRenderer.hpp"
#include "../wallpaper/WallpaperEngine.hpp"

#include <aquamarine/backend/Backend.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <execinfo.h>

#include <poll.h>
#include <unistd.h>

namespace ColibryShell {

    using Hyprutils::Math::CBox;
    using Hyprutils::Math::CRegion;
    using Hyprutils::Math::Vector2D;

    static constexpr uint64_t FRAME_INTERVAL_MS = 4;
    static constexpr int      MAX_WORKSPACE     = 10;

    // First safety-net grace, then backoff doubles on every net fired in a
    // row while frame events are missing (starvation detector in the tick).
    static constexpr uint64_t FRAME_COMPLETION_MS = 150;

    static uint64_t s_lastTickMs = 0;

    // Starvation state: consecutive timer-driven completions without a
    // real frame event between them.
    static uint64_t s_starvations      = 0;
    static uint64_t s_lastFrameEvents  = 0;
    static uint64_t s_safetyBackoffMs  = FRAME_COMPLETION_MS;

    static volatile sig_atomic_t g_framePresented = 0;

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

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
        std::fprintf(stderr, "[colibry-shell backend:%s] %s\n", logPrefix(level), message.c_str());
    }

    static void watchdogHandler(int) {
        static const char MSG[] = "[colibry-shell] watchdog: startup stalled, exiting\n";
        write(STDERR_FILENO, MSG, sizeof(MSG) - 1);
        _exit(1);
    }

    static void crashHandler(int sig) {
        void* frames[64];
        const int count = backtrace(frames, 64);
        std::fprintf(stderr, "[colibry-shell] crash: signal %d, stack trace:\n", sig);
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
        std::signal(sig, SIG_DFL);
        raise(sig);
    }

    static std::string resolveTerminal() {
        if (const char* env = std::getenv("COLIBRYSHELL_TERMINAL"))
            if (*env)
                return env;

        const auto& configured = CConfigManager::get().terminal();
        if (!configured.empty())
            return configured;

        return "ghostty || foot || kitty || alacritty || xterm || weston-terminal";
    }

    static void spawnAutostart(const CDisplay* display) {
        const auto command = resolveTerminal();

        const auto pid = fork();
        if (pid != 0)
            return;

        setsid();
        setenv("WAYLAND_DISPLAY", display->socketName().c_str(), 1);

        execlp("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));

        std::fprintf(stderr, "[colibry-shell] no terminal found; set terminal= in colibry.toml\n");
        _exit(1);
    }

    CCompositor& CCompositor::get() {
        static CCompositor s_instance;
        return s_instance;
    }

    CCompositor::CCompositor()  = default;
    CCompositor::~CCompositor() = default;

    Hyprutils::Memory::CSharedPointer<CClearRenderer> CCompositor::renderer() {
        if (m_renderer)
            return m_renderer;

        if (!m_backend || !m_backend->primaryAllocator) {
            std::fprintf(stderr, "[colibry-shell] no DRM allocator available\n");
            return nullptr;
        }

        m_renderer = Hyprutils::Memory::makeShared<CClearRenderer>();
        if (!m_renderer->init(m_backend->primaryAllocator->drmFD())) {
            std::fprintf(stderr, "[colibry-shell] failed to initialize the buffer factory\n");
            m_renderer.reset();
        }

        return m_renderer;
    }

    void     CCompositor::requestExit() { get().m_eventLoop.quit(); }
    bool     CCompositor::isShuttingDown() const { return m_shutdown; }
    void     CCompositor::notifyFramePresented() { g_framePresented = 1; alarm(0); }
    uint64_t CCompositor::startMs() const { return m_startMs; }
    uint64_t CCompositor::currentMs() const { return nowMs(); }

    double CCompositor::fadeFactor(uint64_t now) const {
        const int fadeMs = m_config ? m_config->loginFadeMs() : 2500;
        if (fadeMs <= 0)
            return 1.0;

        const double t = std::clamp((double)(now - m_startMs) / (double)fadeMs, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    bool CCompositor::inputLocked() const {
        const int unlockMs = m_config ? m_config->loginUnlockMs() : 250;
        return nowMs() < m_startMs + (uint64_t)unlockMs;
    }

    bool CCompositor::init() {
        std::fprintf(stderr, "[colibry-shell] starting compositor %s\n", COLIBRYSHELL_VERSION);

        std::signal(SIGALRM, watchdogHandler);
        alarm(20);

        unsetenv("WAYLAND_DISPLAY");

        m_config = Hyprutils::Memory::makeShared<CConfigManager>();
        m_config->load();

        for (const auto& def : m_config->binds())
            CKeybindManager::get().addBind(def);

        m_wallpaper = Hyprutils::Memory::makeShared<CWallpaper>();
        m_wallpaper->setFit(m_config->wallpaperFit());
        if (!m_config->wallpaperPath().empty())
            m_wallpaper->load(m_config->wallpaperPath());

        m_cursor = Hyprutils::Memory::makeShared<CCursor>();
        m_cursor->load(m_config->cursorTheme(), m_config->cursorSize());
        m_cursor->setSpeed(m_config->cursorSpeed());
        m_cursor->setCornerRadius(m_config->screenRounding());

        m_display = Hyprutils::Memory::makeShared<CDisplay>();
        if (!m_display->init()) {
            std::fprintf(stderr, "[colibry-shell] failed to initialize the wayland display\n");
            return false;
        }

        m_seat = Hyprutils::Memory::makeShared<CSeat>();
        if (!m_seat->init(m_display.get())) {
            std::fprintf(stderr, "[colibry-shell] failed to initialize the seat\n");
            return false;
        }

        m_xdgShell = Hyprutils::Memory::makeShared<CXdgShell>();
        if (!m_xdgShell->init(m_display.get())) {
            std::fprintf(stderr, "[colibry-shell] failed to initialize xdg-shell\n");
            return false;
        }

        if (!CDataDeviceManager::get().init(m_display.get()))
            std::fprintf(stderr, "[colibry-shell] clipboard unavailable\n");

        Aquamarine::SBackendImplementationOptions drm;
        drm.backendType        = Aquamarine::AQ_BACKEND_DRM;
        drm.backendRequestMode = Aquamarine::AQ_BACKEND_REQUEST_IF_AVAILABLE;

        Aquamarine::SBackendOptions options;
        options.logFunction = backendLog;

        m_backend = Aquamarine::CBackend::create({drm}, options);
        if (!m_backend) {
            std::fprintf(stderr, "[colibry-shell] failed to create the backend\n");
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
            std::fprintf(stderr, "[colibry-shell] failed to start the backend\n");
            return false;
        }

        if (!m_backend->primaryAllocator) {
            std::fprintf(stderr, "[colibry-shell] GPU rendering unavailable; nested sessions are not supported\n");
            return false;
        }

        if (!CGLCompositor::get().init(m_backend->primaryAllocator->drmFD())) {
            std::fprintf(stderr, "[colibry-shell] GPU renderer failed to initialize\n");
            return false;
        }

        CGLCompositor::get().setScreenRounding(m_config->screenRounding());
        if (!SceneDraw::initTitleFont(m_config->titlebarFont().c_str()))
            std::fprintf(stderr, "[colibry-shell] titlebar font unavailable; titles will be blank\n");

        m_startMs = nowMs();

        for (const auto& output : m_outputs) {
            if (output->presented())
                continue;
            if (!output->setup())
                std::fprintf(stderr, "[colibry-shell] failed to set up output %s\n", output->aq()->name.c_str());
        }

        if (m_outputs.empty() || !m_outputs.front()->presented()) {
            std::fprintf(stderr, "[colibry-shell] no outputs presented, aborting\n");
            return false;
        }

        if (!CLauncher::get().init())
            std::fprintf(stderr, "[colibry-shell] launcher: no applications found\n");

        registerPollFDs();

        m_eventLoop.addFD(m_display->fd(), POLLIN | POLLOUT, [this](short) { m_display->dispatch(); });

        m_eventLoop.addTimer(FRAME_INTERVAL_MS, [] {
            auto& self = get();

            const auto now = nowMs();
            const auto dt  = s_lastTickMs ? now - s_lastTickMs : (uint64_t)FRAME_INTERVAL_MS;
            s_lastTickMs   = now;

            const bool fading = self.fadeFactor(now) < 1.0;
            if (fading)
                self.addFullDamage();

            bool animating = self.m_cursor->step(now) || fading;

            if (animating)
                self.addDamage(self.m_cursor->damageBox());

            CDragManager::get().stepSprings((double)dt);

            const uint64_t animMs = std::max<uint64_t>(1, self.m_config->openAnimMs());
            std::vector<CWindow*> expired;

            for (const auto& window : self.m_windows) {
                if (window->clothStep((double)dt))
                    animating = true;

                if (window->closeExpired(now))
                    expired.push_back(window.get());

                const double progress = window->openProgress();
                if (progress >= 1.0 || !window->mapped())
                    continue;
                window->setOpenProgress(std::min(1.0, progress + (double)dt / (double)animMs));
                animating = true;
            }

            for (auto* window : expired)
                self.removeWindow(window);

            for (const auto& output : self.m_outputs) {
                // Starvation tracking: a timer completion that fires while
                // the backend's frame-event counter has not advanced means
                // events stopped flowing; back off exponentially instead of
                // hammering the GPU with frames that get rejected.
                const uint64_t events = output->frameEvents();
                if (output->framePending() && now - output->lastCommitMs() > s_safetyBackoffMs) {
                    ++s_starvations;
                    if (events == s_lastFrameEvents) {
                        s_safetyBackoffMs = std::min<uint64_t>(s_safetyBackoffMs * 2, 2000);
                    } else {
                        s_safetyBackoffMs = FRAME_COMPLETION_MS;
                    }
                    output->recoverFrame();
                }
                s_lastFrameEvents = events;

                if (output->needsRender())
                    output->present();
            }
        });

        m_eventLoop.addTimer(2000, [] {
            auto& self = get();

            const auto now = nowMs();
            for (const auto& output : self.m_outputs) {
                // Liveness telemetry: whichever counter stops advancing in
                // the last line before a freeze names the wedged stage.
                std::fprintf(stderr,
                             "[colibry-shell] alive: commit=%llums frames=%llu nets=%llu reject=%llu pending=%d windows=%zu cursor=%.0f,%.0f\n",
                             (unsigned long long)(now - output->lastCommitMs()), (unsigned long long)output->frameEvents(),
                             (unsigned long long)output->safetyNets(), (unsigned long long)output->rejections(), output->pendingDamageRects(),
                             self.m_windows.size(), self.m_cursor->position().x, self.m_cursor->position().y);

                if (output->needsRender() && now - output->lastCommitMs() > 1500)
                    self.addFullDamage();
            }
        });

        m_ipc = Hyprutils::Memory::makeShared<CIPC>();
        if (!m_ipc->start(m_eventLoop)) {
            std::fprintf(stderr, "[colibry-shell] IPC unavailable, continuing without it\n");
            m_ipc.reset();
        }

        spawnAutostart(m_display.get());

        std::fprintf(stderr, "[colibry-shell] %zu output(s) active\n", m_outputs.size());
        return true;
    }

    void CCompositor::registerPollFDs() {
        for (const auto& pollFD : m_backend->getPollFDs())
            m_eventLoop.addFD(pollFD->fd, POLLIN, [pollFD](short) { pollFD->onSignal(); });
    }

    void CCompositor::onNewOutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output) {
        std::fprintf(stderr, "[colibry-shell] output '%s' appeared\n", output->name.c_str());

        const auto ctx = Hyprutils::Memory::makeShared<COutput>(output);

        if (m_outputs.empty()) {
            const auto mode = output->preferredMode() ? output->preferredMode() :
                                                        (output->modes.empty() ? nullptr : output->modes.front());
            if (mode) {
                m_cursor->setBounds(mode->pixelSize);
                m_cursor->setPosition(mode->pixelSize / 2.0);
                m_cursor->snapRender();
            }
        }

        if (CGLCompositor::get().ready()) {
            if (!ctx->setup())
                std::fprintf(stderr, "[colibry-shell] failed to set up output %s\n", output->name.c_str());
            else
                COutputInfo::get().addOutput(output->name, ctx->size());
        }

        m_outputs.push_back(ctx);
    }

    int CCompositor::run() {
        std::signal(SIGINT, [](int) { requestExit(); });
        std::signal(SIGTERM, [](int) { requestExit(); });

        std::signal(SIGSEGV, crashHandler);
        std::signal(SIGABRT, crashHandler);
        std::signal(SIGBUS, crashHandler);

        std::signal(SIGUSR1, [](int) { requestExit(); });

        std::fprintf(stderr, "[colibry-shell] entering event loop\n");
        const int code = m_eventLoop.run();
        m_shutdown     = true;
        return code;
    }

    void CCompositor::shutdown() {
        std::fprintf(stderr, "[colibry-shell] shutting down\n");
        m_shutdown      = true;
        m_focusedWindow = nullptr;

        { auto windows = std::exchange(m_windows, {}); }

        m_ipc.reset();
        m_renderer.reset();
        m_xdgShell.reset();
        m_seat.reset();
        m_cursor.reset();
        m_wallpaper.reset();
        if (m_display)
            m_display->destroy();
        m_display.reset();

        m_backend.reset();
        m_outputs.clear();
    }

    void CCompositor::requestRender() {
        for (const auto& output : m_outputs)
            output->scheduleRender();
    }

    void CCompositor::addDamage(const CBox& box) {
        m_damage.add(box);
        requestRender();
    }

    void CCompositor::addFullDamage() {
        m_damage.add(CBox({0, 0}, {100000, 100000}));
        requestRender();
    }

    CRegion CCompositor::takeDamage() {
        CRegion taken = m_damage;
        m_damage.clear();
        return taken;
    }

    void CCompositor::raise(CWindow* window, bool mapped) {
        const auto it = std::find_if(m_windows.begin(), m_windows.end(), [window](const auto& w) { return w.get() == window; });
        if (it == m_windows.end())
            return;

        if (mapped)
            addDamage(windowBox(window));

        const auto windowRef = *it;
        m_windows.erase(it);
        m_windows.push_back(windowRef);

        if (mapped)
            addDamage(windowBox(window));
    }

    CBox CCompositor::windowBox(CWindow* window) const {
        const auto pos  = window->position();
        const auto size = window->size();
        return CBox(pos, {size.x, size.y + (window->isFullscreen() ? 0.0 : (double)ChromeGeom::height())});
    }

    void CCompositor::focusWindow(CWindow* window) {
        if (m_focusedWindow == window) {
            if (window)
                raise(window, true);
            return;
        }

        if (m_focusedWindow)
            addDamage(windowBox(m_focusedWindow));

        m_focusedWindow = window;

        if (window) {
            raise(window, true);
            m_seat->setKeyboardFocus(window->surface());
            addDamage(windowBox(window));
        } else
            m_seat->setKeyboardFocus(nullptr);

        refreshForeignToplevel();
        requestRender();
    }

    void CCompositor::focusNext() {
        std::vector<CWindow*> candidates;
        for (const auto& w : m_windows)
            if (w->visible())
                candidates.push_back(w.get());

        if (candidates.empty()) {
            focusWindow(nullptr);
            return;
        }

        const auto it = std::find(candidates.begin(), candidates.end(), m_focusedWindow);
        if (it == candidates.end()) {
            focusWindow(candidates.front());
            return;
        }

        const size_t next = (std::distance(candidates.begin(), it) + 1) % candidates.size();
        candidates[next]->setMinimized(false);
        focusWindow(candidates[next]);
    }

    void CCompositor::focusPrev() {
        std::vector<CWindow*> candidates;
        for (const auto& w : m_windows)
            if (w->visible())
                candidates.push_back(w.get());

        if (candidates.empty()) {
            focusWindow(nullptr);
            return;
        }

        const auto it = std::find(candidates.begin(), candidates.end(), m_focusedWindow);
        if (it == candidates.end()) {
            focusWindow(candidates.back());
            return;
        }

        const size_t next = (std::distance(candidates.begin(), it) + candidates.size() - 1) % candidates.size();
        candidates[next]->setMinimized(false);
        focusWindow(candidates[next]);
    }

    void CCompositor::removeWindow(CWindow* window) {
        if (window->surface())
            CGLCompositor::get().dropSurface(window->surface());

        const auto it = std::find_if(m_windows.begin(), m_windows.end(), [window](const auto& w) { return w.get() == window; });
        if (it != m_windows.end()) {
            addDamage(windowBox(window));
            m_windows.erase(it);
        }

        if (m_focusedWindow == window) {
            m_focusedWindow = nullptr;
            for (auto rit = m_windows.rbegin(); rit != m_windows.rend(); ++rit) {
                if ((*rit)->visible()) {
                    focusWindow(rit->get());
                    return;
                }
            }
            m_seat->setKeyboardFocus(nullptr);
        }

        refreshForeignToplevel();
        requestRender();
    }

    CSurface* CCompositor::surfaceAt(const Vector2D& pos) const {
        for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
            if (!(*it)->visible())
                continue;

            const auto wpos  = (*it)->position();
            const auto wsize = (*it)->size();

            if (pos.x >= wpos.x && pos.x < wpos.x + wsize.x && pos.y >= wpos.y + ChromeGeom::height() && pos.y < wpos.y + wsize.y + ChromeGeom::height())
                return (*it)->surface();
        }
        return nullptr;
    }

    void CCompositor::moveWindowToWorkspace(CWindow* window, int workspace) {
        const bool wasFocused = m_focusedWindow == window;
        window->setWorkspace(workspace);
        if (wasFocused) {
            m_focusedWindow = nullptr;
            m_seat->setKeyboardFocus(nullptr);
        }
        addFullDamage();
        focusOnWorkspace(m_currentWorkspace);
    }

    void CCompositor::switchWorkspace(int workspace) {
        while (m_currentWorkspace != workspace) {
            if (m_currentWorkspace < workspace)
                m_currentWorkspace = m_currentWorkspace % MAX_WORKSPACE + 1;
            else
                m_currentWorkspace = m_currentWorkspace - 1 < 1 ? MAX_WORKSPACE : m_currentWorkspace - 1;
        }

        applyTiling();
        focusOnWorkspace(workspace);
        addFullDamage();
    }

    void CCompositor::applyTiling() {
        std::vector<CWindow*> tiling;
        for (const auto& w : m_windows)
            if (w->visible() && w->isTiling())
                tiling.push_back(w.get());

        if (tiling.empty())
            return;

        const auto screen = m_outputs.empty() ? Vector2D(1920, 1080) : m_outputs.front()->size();
        const auto work   = Vector2D(screen.x, screen.y - ChromeGeom::height());

        if (tiling.size() == 1) {
            tiling.front()->setPosition({0, 0});
            tiling.front()->configure(Vector2D(work.x, work.y), {});
            tiling.front()->resizeTo(Vector2D(work.x, work.y));
            return;
        }

        const double masterW = screen.x * 0.55;

        tiling.front()->setPosition({0, 0});
        tiling.front()->configure(Vector2D(masterW, work.y), {1});
        tiling.front()->resizeTo(Vector2D(masterW, work.y));

        const double stackW = screen.x - masterW;
        const double stackH = work.y / (tiling.size() - 1);
        for (size_t i = 1; i < tiling.size(); ++i) {
            tiling[i]->setPosition(Vector2D(masterW, (double)(i - 1) * stackH));
            tiling[i]->configure(Vector2D(stackW, stackH), {1});
            tiling[i]->resizeTo(Vector2D(stackW, stackH));
        }
    }

    void CCompositor::toggleTilingMode() { applyTiling(); }

    void CCompositor::focusOnWorkspace(int workspace) {
        for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
            if ((*it)->visible() && (*it)->workspace() == workspace) {
                focusWindow(it->get());
                return;
            }
        }
        focusWindow(nullptr);
    }

    int CCompositor::currentWorkspace() const { return m_currentWorkspace; }

    bool CCompositor::workspaceHasTiling() const {
        for (const auto& w : m_windows)
            if (w->mapped() && !w->isFullscreen() && w->isTiling())
                return true;
        return false;
    }

    int CCompositor::firstEmptyWorkspace() const {
        for (int ws = 1; ws <= MAX_WORKSPACE; ++ws) {
            bool used = false;
            for (const auto& w : m_windows) {
                if (w->mapped() && w->workspace() == ws) {
                    used = true;
                    break;
                }
            }
            if (!used)
                return ws;
        }
        return m_currentWorkspace;
    }

    void CCompositor::refreshForeignToplevel() {
        for (const auto& w : m_windows)
            CForeignToplevelManager::get().onWindowState(w.get());
    }

}
