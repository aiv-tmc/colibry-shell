// Implementation of the TOML-subset configuration parser.

#include "ConfigManager.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Colibry {

    static std::string trim(const std::string& str) {
        size_t begin = 0;
        size_t end   = str.size();

        while (begin < end && std::isspace(static_cast<unsigned char>(str[begin])))
            ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(str[end - 1])))
            --end;

        return str.substr(begin, end - begin);
    }

    static std::string unquote(const std::string& str) {
        if (str.size() >= 2 && str.front() == '"' && str.back() == '"')
            return str.substr(1, str.size() - 2);
        return str;
    }

    static bool parseInt(const std::string& value, int& out) {
        try {
            out = std::stoi(value);
            return true;
        } catch (...) {
            std::fprintf(stderr, "[colibry] config: invalid integer '%s'\n", value.c_str());
            return false;
        }
    }

    CConfigManager& CConfigManager::get() {
        static CConfigManager s_instance;
        return s_instance;
    }

    void CConfigManager::load() {
        static const std::vector<std::string> files = {
            "/etc/colibry/colibry.toml",
            std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.config/colibry/colibry.toml",
        };

        for (const auto& path : files)
            parseFile(path);
    }

    void CConfigManager::parseFile(const std::string& path) {
        if (!std::filesystem::exists(path))
            return;

        std::ifstream file(path);
        if (!file) {
            std::fprintf(stderr, "[colibry] cannot open config %s\n", path.c_str());
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            const auto hash = line.find('#');
            if (hash != std::string::npos)
                line = line.substr(0, hash);

            line = trim(line);
            if (line.empty())
                continue;

            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "[colibry] config: ignoring malformed line in %s: %s\n", path.c_str(), line.c_str());
                continue;
            }

            const auto key   = trim(line.substr(0, eq));
            const auto value = trim(line.substr(eq + 1));

            if (!applyKey(key, value))
                std::fprintf(stderr, "[colibry] config: unknown key '%s' in %s\n", key.c_str(), path.c_str());
        }
    }

    bool CConfigManager::applyKey(const std::string& key, const std::string& value) {
        if (key == "wallpaper") {
            m_wallpaperPath = unquote(value);
            return true;
        }

        if (key == "wallpaper_fit") {
            m_wallpaperFit = unquote(value);
            return true;
        }

        if (key == "cursor_theme") {
            m_cursorTheme = unquote(value);
            return true;
        }

        if (key == "terminal") {
            m_terminal = unquote(value);
            return true;
        }

        if (key == "titlebar_font") {
            m_titlebarFont = unquote(value);
            return true;
        }

        if (key == "cursor_size")
            return parseInt(value, m_cursorSize);

        if (key == "follow_mouse")
            return parseInt(value, m_followMouse);

        if (key == "shadow_enabled")
            return parseInt(value, m_shadowEnabled);

        if (key == "shadow_offset_x")
            return parseInt(value, m_shadowOffsetX);

        if (key == "shadow_offset_y")
            return parseInt(value, m_shadowOffsetY);

        if (key == "shadow_radius")
            return parseInt(value, m_shadowRadius);

        if (key == "shadow_rounding")
            return parseInt(value, m_shadowRounding);

        if (key == "titlebar_height")
            return parseInt(value, m_titlebarHeight);

        if (key == "titlebar_button_width")
            return parseInt(value, m_titlebarButtonWidth);

        if (key == "shadow_opacity") {
            try {
                m_shadowOpacity = std::stod(value);
            } catch (...) {
                std::fprintf(stderr, "[colibry] config: invalid shadow_opacity '%s'\n", value.c_str());
            }
            return true;
        }

        if (key == "monitors") {
            if (value.size() < 2 || value.front() != '[' || value.back() != ']')
                return false;

            std::stringstream stream(value.substr(1, value.size() - 2));
            std::string       item;

            while (std::getline(stream, item, ',')) {
                item = trim(item);
                if (item.empty())
                    continue;

                std::stringstream fields(unquote(item));
                std::string       name, resolution, scale;

                std::getline(fields, name, ',');
                std::getline(fields, resolution, ',');
                std::getline(fields, scale, ',');

                SMonitorRule rule;
                rule.name       = trim(name);
                rule.resolution = trim(resolution);
                try {
                    rule.scale = trim(scale).empty() ? 1.0 : std::stod(trim(scale));
                } catch (...) {
                    rule.scale = 1.0;
                }

                addRule(std::move(rule));
            }
            return true;
        }

        if (key == "bind") {
            if (value.size() < 2 || value.front() != '[' || value.back() != ']')
                return false;

            std::stringstream stream(value.substr(1, value.size() - 2));
            std::string       item;

            while (std::getline(stream, item, ',')) {
                item = trim(item);
                if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
                    m_binds.push_back(item.substr(1, item.size() - 2));
            }
            return true;
        }

        return false;
    }

    void CConfigManager::addRule(SMonitorRule rule) {
        for (auto it = m_monitorRules.begin(); it != m_monitorRules.end(); ++it) {
            if (it->name == rule.name) {
                *it = rule;
                return;
            }
        }
        m_monitorRules.push_back(std::move(rule));
    }

    const std::vector<SMonitorRule>& CConfigManager::monitorRules() const {
        return m_monitorRules;
    }

    const std::string& CConfigManager::wallpaperPath() const {
        return m_wallpaperPath;
    }

    const std::string& CConfigManager::wallpaperFit() const {
        return m_wallpaperFit;
    }

    const std::string& CConfigManager::cursorTheme() const {
        return m_cursorTheme;
    }

    int CConfigManager::cursorSize() const {
        return m_cursorSize;
    }

    const std::string& CConfigManager::terminal() const {
        return m_terminal;
    }

    int CConfigManager::followMouse() const {
        return m_followMouse;
    }

    bool CConfigManager::shadowEnabled() const {
        return m_shadowEnabled != 0;
    }

    int CConfigManager::shadowOffsetX() const {
        return m_shadowOffsetX;
    }

    int CConfigManager::shadowOffsetY() const {
        return m_shadowOffsetY;
    }

    int CConfigManager::shadowRadius() const {
        return m_shadowRadius;
    }

    int CConfigManager::shadowRounding() const {
        return m_shadowRounding;
    }

    double CConfigManager::shadowOpacity() const {
        return m_shadowOpacity;
    }

    int CConfigManager::titlebarHeight() const {
        return m_titlebarHeight;
    }

    int CConfigManager::titlebarButtonWidth() const {
        return m_titlebarButtonWidth;
    }

    const std::string& CConfigManager::titlebarFont() const {
        return m_titlebarFont;
    }

    const std::vector<std::string>& CConfigManager::binds() const {
        return m_binds;
    }

}
