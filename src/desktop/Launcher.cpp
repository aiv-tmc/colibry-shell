// Implementation of the application launcher overlay.

#include "Launcher.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Keybinds.hpp"
#include "../protocol/Display.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace Colibry {

    CLauncher& CLauncher::get() {
        static CLauncher s_instance;
        return s_instance;
    }

    bool CLauncher::init() {
        rescan();
        return !m_apps.empty();
    }

    bool CLauncher::active() const {
        return m_active;
    }

    const std::string& CLauncher::filter() const {
        return m_filter;
    }

    const std::vector<SLauncherEntry>& CLauncher::entries() const {
        return m_apps;
    }

    int CLauncher::selected() const {
        return m_selected;
    }

    void CLauncher::rescan() {
        m_apps.clear();

        const std::vector<std::string> dirs = {
            "/usr/share/applications",
            "/usr/local/share/applications",
            std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.local/share/applications",
        };

        for (const auto& dir : dirs) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.path().extension() != ".desktop")
                    continue;

                std::ifstream file(entry.path());
                if (!file)
                    continue;

                SLauncherEntry app;
                bool isApplication = false, noDisplay = false, hidden = false;

                std::string line;
                while (std::getline(file, line)) {
                    if (line.starts_with("Name="))
                        app.name = line.substr(5);
                    else if (line.starts_with("Exec="))
                        app.exec = line.substr(5);
                    else if (line.starts_with("Icon="))
                        app.icon = line.substr(5);
                    else if (line.starts_with("Type="))
                        isApplication = line.substr(5) == "Application";
                    else if (line.starts_with("NoDisplay="))
                        noDisplay = line.substr(10) == "true";
                    else if (line.starts_with("Hidden="))
                        hidden = line.substr(7) == "true";
                }

                if (!isApplication || noDisplay || hidden || app.name.empty() || app.exec.empty())
                    continue;

                // Drop desktop field codes the shell cannot resolve.
                std::string cleaned;
                for (size_t i = 0; i < app.exec.size(); ++i) {
                    if (app.exec[i] == '%' && i + 1 < app.exec.size()) {
                        const char code = app.exec[i + 1];
                        if (code == '%') {
                            cleaned += '%';
                            ++i;
                            continue;
                        }
                        if (std::strchr("fFuUdDnNickvm", code)) {
                            ++i;
                            continue;
                        }
                    }
                    cleaned += app.exec[i];
                }
                app.exec = cleaned;

                m_apps.push_back(std::move(app));
            }
        }

        std::sort(m_apps.begin(), m_apps.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        refilter();
    }

    void CLauncher::refilter() {
        m_filtered.clear();
        m_selected = 0;

        std::string needle = m_filter;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        for (int i = 0; i < (int)m_apps.size(); ++i) {
            std::string hay = m_apps[i].name;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            // Fuzzy subsequence match keeps the launcher forgiving.
            size_t pos = 0;
            bool   ok  = true;
            for (const char c : needle) {
                pos = hay.find(c, pos);
                if (pos == std::string::npos) {
                    ok = false;
                    break;
                }
                ++pos;
            }

            if (ok)
                m_filtered.push_back(i);
        }

        if (m_selected >= (int)m_filtered.size())
            m_selected = std::max(0, (int)m_filtered.size() - 1);
    }

    void CLauncher::open() {
        if (m_active)
            return;
        m_active = true;
        CCompositor::get().addFullDamage();
    }

    void CLauncher::close() {
        if (!m_active)
            return;
        m_active = false;
        m_filter.clear();
        refilter();
        CCompositor::get().addFullDamage();
    }

    void CLauncher::toggle() {
        if (m_active)
            close();
        else
            open();
    }

    void CLauncher::onKey(uint32_t sym, uint32_t mods, bool pressed) {
        if (!pressed)
            return;

        switch (sym) {
            case XKB_KEY_Escape: close(); return;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                if (!m_filtered.empty())
                    launch(m_apps[m_filtered[m_selected]]);
                close();
                return;
            case XKB_KEY_BackSpace:
                if (!m_filter.empty()) {
                    m_filter.pop_back();
                    refilter();
                    CCompositor::get().requestRender();
                }
                return;
            case XKB_KEY_Up:
            case XKB_KEY_Left:
                if (!m_filtered.empty())
                    m_selected = (m_selected + (int)m_filtered.size() - 1) % (int)m_filtered.size();
                CCompositor::get().requestRender();
                return;
            case XKB_KEY_Down:
            case XKB_KEY_Right:
            case XKB_KEY_Tab:
                if (!m_filtered.empty())
                    m_selected = (m_selected + 1) % (int)m_filtered.size();
                CCompositor::get().requestRender();
                return;
            default: break;
        }

        // Printable characters extend the filter.
        char utf8[8] = {0};
        if (xkb_keysym_to_utf8(sym, utf8, sizeof(utf8)) > 0) {
            m_filter += utf8;
            refilter();
            CCompositor::get().requestRender();
        }

        (void)mods;
    }

    void CLauncher::launch(const SLauncherEntry& entry) {
        auto& compositor = CCompositor::get();
        if (!compositor.m_display)
            return;

        const auto pid = fork();
        if (pid != 0)
            return;

        setsid();
        setenv("WAYLAND_DISPLAY", compositor.m_display->socketName().c_str(), 1);

        execlp("/bin/sh", "sh", "-c", entry.exec.c_str(), static_cast<char*>(nullptr));
        _exit(1);
    }

}
