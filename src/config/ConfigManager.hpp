// Configuration for the Colibry compositor.
//
// The configuration is a TOML file read from /etc/colibry/colibry.toml and
// then overridden by ~/.config/colibry/colibry.toml. Supported keys:
//   wallpaper      = "/absolute/path/to/image.png"  (PNG or JPEG)
//   wallpaper_fit  = "cover" | "fit"                (default "cover")
//   cursor_theme   = "Adwaita"                      (empty = system default)
//   cursor_size    = 24
//   terminal       = "foot"
//   follow_mouse   = 1                              (0 = click-to-focus only)
//   shadow_enabled = 1
//   shadow_offset_x / shadow_offset_y               (default 0 / 12)
//   shadow_radius    = 24
//   shadow_rounding  = 8
//   shadow_opacity   = 0.45
//   titlebar_height        = 32                     (bar height in pixels)
//   titlebar_button_width  = 46                     (button width in pixels)
//   titlebar_font          = "/path/to/font.ttf"    (empty = auto-detect)
//   monitors       = ["eDP-1,1920x1200,1"]
//   bind           = ["SUPER+B=exec:firefox"]

#pragma once

#include <string>
#include <vector>

#ifndef COLIBRY_VERSION
#define COLIBRY_VERSION "0.1.0"
#endif

namespace Colibry {

    // One monitor rule; an empty name matches every output.
    struct SMonitorRule {
        std::string name;       // output name from the backend, empty = wildcard
        std::string resolution; // "preferred" or "WxH"
        double      scale = 1.0; // render scale for the output buffer
    };

    class CConfigManager {
      public:
        static CConfigManager& get();

        // Parses the system and user configuration files.
        void load();

        const std::vector<SMonitorRule>& monitorRules() const;

        const std::string& wallpaperPath() const;
        const std::string& wallpaperFit() const;
        const std::string& cursorTheme() const;
        int                cursorSize() const;
        const std::string& terminal() const;
        int                followMouse() const;

        bool   shadowEnabled() const;
        int    shadowOffsetX() const;
        int    shadowOffsetY() const;
        int    shadowRadius() const;
        int    shadowRounding() const;
        double shadowOpacity() const;

        // Title bar configuration.
        int                titlebarHeight() const;
        int                titlebarButtonWidth() const;
        const std::string& titlebarFont() const;

        const std::vector<std::string>& binds() const;

      private:
        void parseFile(const std::string& path);
        bool applyKey(const std::string& key, const std::string& value);
        void addRule(SMonitorRule rule);

        std::vector<SMonitorRule> m_monitorRules;
        std::vector<std::string>  m_binds;
        std::string               m_wallpaperPath;
        std::string               m_wallpaperFit       = "cover";
        std::string               m_cursorTheme;
        std::string               m_terminal;
        std::string               m_titlebarFont;
        int                       m_cursorSize         = 0;
        int                       m_followMouse        = 1;
        int                       m_shadowEnabled      = 1;
        int                       m_shadowOffsetX      = 0;
        int                       m_shadowOffsetY      = 12;
        int                       m_shadowRadius       = 24;
        int                       m_shadowRounding     = 8;
        double                    m_shadowOpacity      = 0.45;
        int                       m_titlebarHeight     = 32;
        int                       m_titlebarButtonWidth = 46;
    };

}
