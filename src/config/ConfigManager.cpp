// Implementation of the TOML configuration.

#include "ConfigManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ColibryShell {

    CConfigManager& CConfigManager::get() {
        static CConfigManager s_instance;
        return s_instance;
    }

    static std::string trim(const std::string& s) {
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    static std::string unquote(const std::string& v) {
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            return v.substr(1, v.size() - 2);
        return v;
    }

    static int toInt(const std::string& v, int fallback = 0) {
        try {
            return std::stoi(trim(v));
        } catch (...) { return fallback; }
    }

    static double toDouble(const std::string& v, double fallback = 0.0) {
        try {
            return std::stod(trim(v));
        } catch (...) { return fallback; }
    }

    // Splits a TOML list of quoted strings into entries.
    static std::vector<std::string> parseList(const std::string& v) {
        std::vector<std::string> out;

        std::string current;
        bool inString = false;
        for (const char c : v) {
            if (c == '"') {
                if (inString) {
                    out.push_back(current);
                    current.clear();
                }
                inString = !inString;
                continue;
            }
            if (inString)
                current += c;
        }
        return out;
    }

    bool CConfigManager::load() {
        bool found = false;

        found |= loadFile("/etc/colibry/colibry.toml");

        const char* home = std::getenv("HOME");
        if (home)
            found |= loadFile(std::string(home) + "/.config/colibry/colibry.toml");

        // No wallpaper configured: fall back to the image shipped with the
        // shell package so the desktop is not a bare dark fill.
        if (m_wallpaperPath.empty()) {
            for (const auto& candidate : {"/usr/local/share/colibry-shell/wallpapers/wallpaper.jpg",
                                          "/usr/share/colibry-shell/wallpapers/wallpaper.jpg",
                                          "/usr/local/share/colibry-shell/wallpapers/wallpaper.png",
                                          "/usr/share/colibry-shell/wallpapers/wallpaper.png"}) {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) {
                    m_wallpaperPath = candidate;
                    break;
                }
            }
        }

        return found;
    }

    bool CConfigManager::loadFile(const std::string& path) {
        std::ifstream file(path);
        if (!file)
            return false;

        std::string section;
        std::string line;

        while (std::getline(file, line)) {
            const auto comment = line.find('#');
            if (comment != std::string::npos)
                line = line.substr(0, comment);

            line = trim(line);
            if (line.empty())
                continue;

            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            applyKey(section, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
        }

        return true;
    }

    void CConfigManager::applyKey(const std::string& section, const std::string& key, const std::string& rawValue) {
        const std::string k = section.empty() ? key : section + "_" + key;

        if (k == "wallpaper") {
            m_wallpaperPath = unquote(rawValue);
            return;
        }
        if (k == "wallpaper_fit") {
            m_wallpaperFit = unquote(rawValue);
            return;
        }
        if (k == "cursor_theme") {
            m_cursorTheme = unquote(rawValue);
            return;
        }
        if (k == "cursor_size") {
            m_cursorSize = toInt(rawValue, 24);
            return;
        }
        if (k == "cursor_speed") {
            m_cursorSpeed = toDouble(rawValue, 1.0);
            return;
        }
        if (k == "input_follow_mouse") {
            m_followMouse = toInt(rawValue, 1) != 0;
            return;
        }
        if (k == "shadow_enabled") {
            m_shadowEnabled = toInt(rawValue, 1) != 0;
            return;
        }
        if (k == "shadow_offset_x") {
            m_shadowOffsetX = toInt(rawValue, 0);
            return;
        }
        if (k == "shadow_offset_y") {
            m_shadowOffsetY = toInt(rawValue, 12);
            return;
        }
        if (k == "shadow_radius") {
            m_shadowRadius = toInt(rawValue, 24);
            return;
        }
        if (k == "shadow_rounding") {
            m_shadowRounding = toInt(rawValue, 8);
            return;
        }
        if (k == "shadow_opacity") {
            m_shadowOpacity = toDouble(rawValue, 0.45);
            return;
        }
        if (k == "titlebar_height") {
            m_titlebarHeight = std::max(16, toInt(rawValue, 32));
            return;
        }
        if (k == "titlebar_button_width") {
            m_titlebarButtonWidth = std::max(16, toInt(rawValue, 46));
            return;
        }
        if (k == "titlebar_font") {
            m_titlebarFont = unquote(rawValue);
            return;
        }
        if (k == "general_window_rounding" || k == "window_rounding") {
            m_windowRounding = std::max(0, toInt(rawValue, 10));
            return;
        }
        if (k == "general_screen_rounding" || k == "screen_rounding") {
            m_screenRounding = std::max(0, toInt(rawValue, 0));
            return;
        }
        if (k == "general_open_anim_ms" || k == "open_anim_ms") {
            m_openAnimMs = std::max<uint64_t>(0, (uint64_t)toInt(rawValue, 220));
            return;
        }
        if (k == "general_login_fade_ms" || k == "login_fade_ms") {
            m_loginFadeMs = std::max(0, toInt(rawValue, 750));
            return;
        }
        if (k == "general_login_unlock_ms" || k == "login_unlock_ms") {
            m_loginUnlockMs = std::max(0, toInt(rawValue, 250));
            return;
        }
        if (k == "terminal") {
            m_terminal = unquote(rawValue);
            return;
        }
        if (k == "monitors") {
            m_monitorRules.clear();
            for (const auto& entry : parseList(rawValue)) {
                std::stringstream ss(entry);
                std::string       name, resolution, scale;

                std::getline(ss, name, ',');
                std::getline(ss, resolution, ',');
                std::getline(ss, scale, ',');

                SMonitorRule rule;
                rule.name       = trim(name);
                rule.resolution = trim(resolution);
                rule.scale      = toDouble(trim(scale), 1.0);
                if (rule.scale <= 0.0)
                    rule.scale = 1.0;
                m_monitorRules.push_back(rule);
            }
            return;
        }
        if (k == "bind") {
            for (const auto& entry : parseList(rawValue))
                m_binds.push_back(entry);
            return;
        }
    }

    const std::string& CConfigManager::wallpaperPath() const { return m_wallpaperPath; }
    const std::string& CConfigManager::wallpaperFit() const { return m_wallpaperFit; }

    const std::string& CConfigManager::cursorTheme() const { return m_cursorTheme; }
    int                CConfigManager::cursorSize() const { return m_cursorSize; }
    double             CConfigManager::cursorSpeed() const { return m_cursorSpeed; }

    bool CConfigManager::followMouse() const { return m_followMouse; }

    bool   CConfigManager::shadowEnabled() const { return m_shadowEnabled; }
    int    CConfigManager::shadowOffsetX() const { return m_shadowOffsetX; }
    int    CConfigManager::shadowOffsetY() const { return m_shadowOffsetY; }
    int    CConfigManager::shadowRadius() const { return m_shadowRadius; }
    int    CConfigManager::shadowRounding() const { return m_shadowRounding; }
    double CConfigManager::shadowOpacity() const { return m_shadowOpacity; }

    int CConfigManager::titlebarHeight() const { return m_titlebarHeight; }
    int CConfigManager::titlebarButtonWidth() const { return m_titlebarButtonWidth; }
    const std::string& CConfigManager::titlebarFont() const { return m_titlebarFont; }

    int      CConfigManager::windowRounding() const { return m_windowRounding; }
    int      CConfigManager::screenRounding() const { return m_screenRounding; }
    uint64_t CConfigManager::openAnimMs() const { return m_openAnimMs; }
    int      CConfigManager::loginFadeMs() const { return m_loginFadeMs; }
    int      CConfigManager::loginUnlockMs() const { return m_loginUnlockMs; }

    const std::string& CConfigManager::terminal() const { return m_terminal; }

    const std::vector<SMonitorRule>& CConfigManager::monitorRules() const { return m_monitorRules; }
    const std::vector<std::string>&  CConfigManager::binds() const { return m_binds; }

}
