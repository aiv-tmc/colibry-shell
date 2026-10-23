// Implementation of keybind handling.

#include "Keybinds.hpp"

#include "../config/ConfigManager.hpp"
#include "../core/Compositor.hpp"
#include "Switcher.hpp"
#include "Window.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unistd.h>

namespace Colibry {

    // Shell fallback chains tried when the environment and the
    // configuration do not name a program.
    static const char* TERMINAL_FALLBACK = "foot || kitty || alacritty || xterm || weston-terminal";
    static const char* LAUNCHER_FALLBACK = "fuzzel || wofi";

    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> out;
        std::stringstream        stream(str);
        std::string              item;

        while (std::getline(stream, item, delim)) {
            const auto begin = item.find_first_not_of(" \t");
            const auto end   = item.find_last_not_of(" \t");
            out.push_back(begin == std::string::npos ? "" : item.substr(begin, end - begin + 1));
        }

        return out;
    }

    static std::string upper(std::string str) {
        for (auto& c : str)
            c = std::toupper(static_cast<unsigned char>(c));
        return str;
    }

    // Resolves a pseudo-command to a concrete shell command.
    static std::string resolveCommand(const char* envName, const std::string& configured, const char* fallback) {
        const char* env = std::getenv(envName);
        if (env && *env)
            return env;
        if (!configured.empty())
            return configured;
        return fallback;
    }

    static void spawn(const char* command) {
        const auto pid = fork();
        if (pid == 0) {
            setsid();
            execlp("/bin/sh", "sh", "-c", command, static_cast<char*>(nullptr));
            _exit(1);
        }
    }

    CKeybindManager& CKeybindManager::get() {
        static CKeybindManager s_instance;
        return s_instance;
    }

    CKeybindManager::CKeybindManager() {
        // Default bind set covering the full input parity: launchers,
        // closing, window state, focus cycling, tiling, workspaces.
        static const std::vector<const char*> DEFAULTS = {
            "SUPER+Return=exec:terminal",
            "ALT+F1=exec:terminal",
            "SUPER+D=exec:launcher",
            "SUPER+C=colibry:close",
            "SUPER+Q=colibry:close",
            "ALT+F4=colibry:close",
            "SUPER+F=colibry:fullscreen",
            "SUPER+T=colibry:togglefloating",
            "SUPER+M=colibry:minimize",
            "SUPER+Tab=colibry:switcher",
            "SUPER+J=colibry:focusnext",
            "SUPER+K=colibry:focusprev",
            "SUPER+Left=colibry:move -20 0",
            "SUPER+Right=colibry:move 20 0",
            "SUPER+Up=colibry:move 0 -20",
            "SUPER+Down=colibry:move 0 20",
            "SUPER+SHIFT+Left=colibry:resize -20 0",
            "SUPER+SHIFT+Right=colibry:resize 20 0",
            "SUPER+SHIFT+Up=colibry:resize 0 -20",
            "SUPER+SHIFT+Down=colibry:resize 0 20",
        };

        for (const auto* def : DEFAULTS)
            addBind(def);

        for (int digit = 1; digit <= 9; ++digit)
            addBind("SUPER+" + std::to_string(digit) + "=colibry:workspace " + std::to_string(digit));
        addBind("SUPER+0=colibry:workspace 10");

        for (int digit = 1; digit <= 9; ++digit)
            addBind("SUPER+SHIFT+" + std::to_string(digit) + "=colibry:movetoworkspace " + std::to_string(digit));
        addBind("SUPER+SHIFT+0=colibry:movetoworkspace 10");
    }

    void CKeybindManager::addBind(const std::string& definition) {
        const auto eq = definition.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "[colibry] bind: missing '=': %s\n", definition.c_str());
            return;
        }

        const auto combo  = definition.substr(0, eq);
        const auto action = definition.substr(eq + 1);
        const auto parts  = split(combo, '+');

        if (parts.size() < 2) {
            std::fprintf(stderr, "[colibry] bind: need at least one modifier and a key: %s\n", definition.c_str());
            return;
        }

        SBind bind;
        bind.action = action;

        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            const auto mod = upper(parts[i]);
            if (mod == "SUPER")
                bind.mods |= BIND_SUPER;
            else if (mod == "CTRL")
                bind.mods |= BIND_CTRL;
            else if (mod == "ALT")
                bind.mods |= BIND_ALT;
            else if (mod == "SHIFT")
                bind.mods |= BIND_SHIFT;
            else {
                std::fprintf(stderr, "[colibry] bind: unknown modifier '%s'\n", parts[i].c_str());
                return;
            }
        }

        bind.sym = xkb_keysym_from_name(parts.back().c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        if (bind.sym == XKB_KEY_NoSymbol) {
            std::fprintf(stderr, "[colibry] bind: unknown key '%s'\n", parts.back().c_str());
            return;
        }

        for (auto it = m_binds.begin(); it != m_binds.end(); ++it) {
            if (it->mods == bind.mods && it->sym == bind.sym) {
                *it = bind;
                return;
            }
        }
        m_binds.push_back(bind);
    }

    bool CKeybindManager::handle(xkb_keysym_t sym, uint32_t mods, bool pressed) {
        m_mods = mods;
        if (!pressed)
            return false;

        for (const auto& bind : m_binds) {
            if (bind.sym != sym || bind.mods != mods)
                continue;

            const auto args = split(bind.action, ':');
            const auto verb = args[0];

            if (verb == "exec" && args.size() >= 2) {
                const auto& config = CConfigManager::get();

                if (args[1] == "terminal")
                    spawn(resolveCommand("COLIBRY_TERMINAL", config.terminal(), TERMINAL_FALLBACK).c_str());
                else if (args[1] == "launcher")
                    spawn(resolveCommand("COLIBRY_LAUNCHER", "", LAUNCHER_FALLBACK).c_str());
                else
                    spawn(args[1].c_str());
                return true;
            }

            if (verb == "colibry") {
                auto&      compositor = CCompositor::get();
                const auto sub        = split(args.size() >= 2 ? args[1] : "", ' ');

                if (sub.empty())
                    return true;

                if (sub[0] == "close") {
                    if (compositor.m_focusedWindow)
                        compositor.m_focusedWindow->close();
                    return true;
                }
                if (sub[0] == "exit") {
                    CCompositor::requestExit();
                    return true;
                }
                if (sub[0] == "fullscreen") {
                    if (compositor.m_focusedWindow)
                        compositor.m_focusedWindow->toggleFullscreen();
                    return true;
                }
                if (sub[0] == "togglefloating") {
                    if (compositor.m_focusedWindow)
                        compositor.m_focusedWindow->toggleTiling();
                    return true;
                }
                if (sub[0] == "maximize") {
                    if (compositor.m_focusedWindow)
                        compositor.m_focusedWindow->toggleMaximized();
                    return true;
                }
                if (sub[0] == "minimize") {
                    if (compositor.m_focusedWindow)
                        compositor.m_focusedWindow->setMinimized(true);
                    return true;
                }
                if (sub[0] == "focusnext") {
                    compositor.focusNext();
                    return true;
                }
                if (sub[0] == "focusprev") {
                    compositor.focusPrev();
                    return true;
                }
                if (sub[0] == "switcher") {
                    CSwitcher::get().open();
                    return true;
                }
                if (sub[0] == "workspace" && sub.size() >= 2) {
                    compositor.switchWorkspace(std::atoi(sub[1].c_str()));
                    return true;
                }
                if (sub[0] == "movetoworkspace" && sub.size() >= 2) {
                    compositor.moveWindowToWorkspace(compositor.m_focusedWindow, std::atoi(sub[1].c_str()));
                    return true;
                }
                if (sub[0] == "move" && sub.size() >= 3) {
                    if (compositor.m_focusedWindow)
                        compositor.m_focusedWindow->moveBy({std::atof(sub[1].c_str()), std::atof(sub[2].c_str())});
                    return true;
                }
                if (sub[0] == "resize" && sub.size() >= 3) {
                    if (compositor.m_focusedWindow) {
                        const auto current = compositor.m_focusedWindow->size();
                        compositor.m_focusedWindow->resizeTo({current.x + std::atof(sub[1].c_str()), current.y + std::atof(sub[2].c_str())});
                    }
                    return true;
                }
            }

            std::fprintf(stderr, "[colibry] bind: unknown action '%s'\n", bind.action.c_str());
            return true;
        }

        return false;
    }

}
