// Implementation of the keybind dispatcher.

#include "Keybinds.hpp"

#include "../config/ConfigManager.hpp"
#include "../core/Compositor.hpp"
#include "../desktop/Launcher.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Display.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>

namespace ColibryShell {

    namespace {

        uint32_t parseMods(const std::string& keys) {
            uint32_t mods = 0;

            std::string upper = keys;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });

            if (upper.contains("SUPER"))
                mods |= BIND_SUPER;
            if (upper.contains("CTRL") || upper.contains("CONTROL"))
                mods |= BIND_CTRL;
            if (upper.contains("ALT") || upper.contains("MOD1"))
                mods |= BIND_ALT;
            if (upper.contains("SHIFT"))
                mods |= BIND_SHIFT;

            return mods;
        }

        std::string resolveTerminal() {
            if (const char* env = std::getenv("COLIBRYSHELL_TERMINAL"))
                if (*env)
                    return env;

            const auto& configured = CConfigManager::get().terminal();
            if (!configured.empty())
                return configured;

            return "ghostty || foot || kitty || alacritty || xterm || weston-terminal";
        }

        void spawn(const std::string& command) {
            auto& compositor = CCompositor::get();
            if (!compositor.m_display)
                return;

            const auto pid = fork();
            if (pid != 0)
                return;

            setsid();
            setenv("WAYLAND_DISPLAY", compositor.m_display->socketName().c_str(), 1);

            execlp("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
            std::fprintf(stderr, "[colibry-shell] failed to spawn '%s'\n", command.c_str());
            _exit(1);
        }

        void switchToWorkspace(int ws) {
            auto& compositor = CCompositor::get();
            while (compositor.m_currentWorkspace != ws) {
                if (compositor.m_currentWorkspace < ws)
                    compositor.m_currentWorkspace = compositor.m_currentWorkspace % 10 + 1;
                else
                    compositor.m_currentWorkspace = compositor.m_currentWorkspace - 1 < 1 ? 10 : compositor.m_currentWorkspace - 1;
            }
            compositor.applyTiling();
            compositor.focusOnWorkspace(ws);
            compositor.addFullDamage();
        }

        void moveActiveToWorkspace(int ws) {
            auto& compositor = CCompositor::get();
            if (compositor.m_focusedWindow)
                compositor.moveWindowToWorkspace(compositor.m_focusedWindow, ws);
        }

        // Keyboard window management: move and resize the focused window
        // in fixed steps, fully mouse-free.
        void moveFocused(int dx, int dy) {
            auto& compositor = CCompositor::get();
            if (compositor.m_focusedWindow && !compositor.m_focusedWindow->isFullscreen())
                compositor.m_focusedWindow->moveBy({(double)dx, (double)dy});
        }

        void resizeFocused(int dx, int dy) {
            auto& compositor = CCompositor::get();
            if (!compositor.m_focusedWindow || compositor.m_focusedWindow->isFullscreen())
                return;

            const auto size = compositor.m_focusedWindow->size();
            compositor.m_focusedWindow->resizeTo({size.x + dx, size.y + dy});
        }

        void handleAction(const std::string& action) {
            auto& compositor = CCompositor::get();

            if (action.starts_with("exec:")) {
                spawn(action.substr(5));
                return;
            }

            if (action.starts_with("workspace:")) {
                const int ws = std::atoi(action.substr(10).c_str());
                if (ws >= 1)
                    switchToWorkspace(ws);
                return;
            }

            if (action.starts_with("movetoworkspace:")) {
                const int ws = std::atoi(action.substr(16).c_str());
                if (ws >= 1)
                    moveActiveToWorkspace(ws);
                return;
            }

            if (action == "colibry:exit") {
                CCompositor::requestExit();
                return;
            }

            if (action == "colibry:launcher") {
                CLauncher::get().toggle();
                return;
            }

            if (action == "colibry:openswitcher") {
                CSwitcher::get().open();
                return;
            }

            if (action == "colibry:closeswitcher" || action == "colibry:cancelswitcher") {
                CSwitcher::get().cancel();
                return;
            }

            if (action == "colibry:nextswitcher") {
                CSwitcher::get().cycle(1);
                return;
            }

            if (action == "colibry:prevswitcher") {
                CSwitcher::get().cycle(-1);
                return;
            }

            if (action == "colibry:activateswitcher") {
                CSwitcher::get().activate();
                return;
            }

            if (action == "colibry:focusnext") {
                compositor.focusNext();
                return;
            }

            if (action == "colibry:focusprev") {
                compositor.focusPrev();
                return;
            }

            if (action == "colibry:moveleft") { moveFocused(-40, 0); return; }
            if (action == "colibry:moveright") { moveFocused(40, 0); return; }
            if (action == "colibry:moveup") { moveFocused(0, -40); return; }
            if (action == "colibry:movedown") { moveFocused(0, 40); return; }

            if (action == "colibry:resizeleft") { resizeFocused(-40, 0); return; }
            if (action == "colibry:resizeright") { resizeFocused(40, 0); return; }
            if (action == "colibry:resizeup") { resizeFocused(0, -40); return; }
            if (action == "colibry:resizedown") { resizeFocused(0, 40); return; }

            if (action == "colibry:close") {
                if (compositor.m_focusedWindow)
                    compositor.m_focusedWindow->close();
                return;
            }

            if (action == "colibry:fullscreen") {
                if (compositor.m_focusedWindow)
                    compositor.m_focusedWindow->toggleFullscreen();
                return;
            }

            if (action == "colibry:minimize") {
                if (compositor.m_focusedWindow)
                    compositor.m_focusedWindow->setMinimized(true);
                return;
            }

            if (action == "colibry:toggletiling") {
                if (compositor.m_focusedWindow) {
                    compositor.m_focusedWindow->toggleTiling();
                    compositor.applyTiling();
                }
                return;
            }
        }

        void registerDefaults(CKeybindManager& manager) {
            static const std::array<std::string, 24> DEFAULTS = {
                "SUPER+Return=exec:" + resolveTerminal(),
                "Alt+F1=exec:" + resolveTerminal(),
                "SUPER+T=exec:ghostty",
                "SUPER+E=exec:nautilus",
                "SUPER+D=colibry:launcher",
                "SUPER+Space=colibry:launcher",
                "SUPER+Tab=colibry:openswitcher",
                "SUPER+J=colibry:focusnext",
                "SUPER+K=colibry:focusprev",
                "SUPER+Left=colibry:moveleft",
                "SUPER+Right=colibry:moveright",
                "SUPER+Up=colibry:moveup",
                "SUPER+Down=colibry:movedown",
                "SUPER+Shift+Left=colibry:resizeleft",
                "SUPER+Shift+Right=colibry:resizeright",
                "SUPER+Shift+Up=colibry:resizeup",
                "SUPER+Shift+Down=colibry:resizedown",
                "SUPER+C=colibry:close",
                "SUPER+Q=colibry:close",
                "Alt+F4=colibry:close",
                "SUPER+F=colibry:fullscreen",
                "SUPER+M=colibry:minimize",
                "SUPER+Shift+T=colibry:toggletiling",
                "Ctrl+Alt+Delete=colibry:exit",
            };

            for (const auto& def : DEFAULTS)
                manager.addBind(def);

            static const std::array<const char*, 10> DIGITS = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
            for (int i = 0; i < 10; ++i) {
                manager.addBind("SUPER+" + std::string(DIGITS[i]) + "=workspace:" + std::to_string(i + 1));
                manager.addBind("SUPER+Shift+" + std::string(DIGITS[i]) + "=movetoworkspace:" + std::to_string(i + 1));
            }
        }

    }

    CKeybindManager::CKeybindManager() {
        registerDefaults(*this);
    }

    CKeybindManager& CKeybindManager::get() {
        static CKeybindManager s_instance;
        return s_instance;
    }

    void CKeybindManager::addBind(const std::string& bind) {
        const auto sep = bind.find('=');
        if (sep == std::string::npos || sep + 1 >= bind.size())
            return;

        const std::string keys   = bind.substr(0, sep);
        const std::string action = bind.substr(sep + 1);

        const auto plus = keys.rfind('+');
        if (plus == std::string::npos)
            return;

        const uint32_t modMask = parseMods(plus > 0 ? keys.substr(0, plus) : "");

        const std::string keyName = keys.substr(plus + 1);
        const xkb_keysym_t sym =
            keyName.size() == 1 ? xkb_keysym_from_name(std::string(1, (char)std::toupper((unsigned char)keyName[0])).c_str(), XKB_KEYSYM_NO_FLAGS) :
                                  xkb_keysym_from_name(keyName.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);

        if (sym == XKB_KEY_NoSymbol) {
            std::fprintf(stderr, "[colibry-shell] invalid bind key in '%s'\n", bind.c_str());
            return;
        }

        m_binds.push_back({modMask, sym, action});
    }

    bool CKeybindManager::handle(xkb_keysym_t sym, uint32_t mods, bool pressed) {
        if (!pressed)
            return false;

        for (const auto& bind : m_binds) {
            if (bind.mods == mods && bind.sym == sym) {
                handleAction(bind.action);
                return true;
            }
        }

        return false;
    }

}
