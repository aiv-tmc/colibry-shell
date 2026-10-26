// Wallpaper loading and drawing for the Colibry compositor.
//
// CWallpaper decodes a PNG or JPEG file (path from the TOML configuration)
// into an RGBA buffer. The output compositor draws it either with cover
// scaling (the image is enlarged until it fills the output and the overflow
// is cropped symmetrically — the default) or with fit scaling (the whole
// image is visible, centered on the background color).

#pragma once

#include <hyprutils/math/Vector2D.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ColibryShell {

    // How the wallpaper is mapped onto the output.
    enum eWallpaperFit {
        FIT_COVER, // enlarge and crop the overflow (default)
        FIT_FIT,   // show the whole image, centered
    };

    class CWallpaper {
      public:
        CWallpaper()          = default;
        ~CWallpaper()         = default;

        // Decodes the image at the given path. An empty or unreadable path
        // leaves the wallpaper empty (solid background).
        void load(const std::string& path);

        // True when a decoded image is available.
        bool hasImage() const;
        // Image size in pixels.
        Hyprutils::Math::Vector2D size() const;

        // Selects the mapping mode; invalid names fall back to cover.
        void setFit(const std::string& fit);
        void setFit(eWallpaperFit fit);

        // Draws the wallpaper into an XRGB8888 surface; every destination
        // pixel is written.
        void draw(uint32_t* pixels, int32_t stridePixels, int32_t width, int32_t height) const;

      private:
        std::vector<uint8_t>      m_rgba;
        Hyprutils::Math::Vector2D m_size;
        eWallpaperFit             m_fit = FIT_COVER;
    };

}
