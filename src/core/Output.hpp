// Wrapper around a single Aquamarine output.
//
// COutput keeps one always-up-to-date CPU scene of the whole desktop plus
// one KMS buffer. Frames are damage-driven: the scene is redrawn only
// inside the dirty rectangles accumulated by the compositor, and only
// those rows are copied into the KMS buffer before the commit. Partial
// updates therefore never expose stale buffer content.

#pragma once

#include <aquamarine/buffer/Buffer.hpp>
#include <aquamarine/output/Output.hpp>

#include <cstdint>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>
#include <map>
#include <vector>

namespace Colibry {

    class CWindow;

    class COutput {
      public:
        explicit COutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output);

        bool setup();

        // Redraws the dirty parts of the scene, copies them into the KMS
        // buffer and commits one frame.
        void present();

        // Marks the output dirty; the frame timer draws it on its next
        // tick.
        void scheduleRender();

        // Forces the next frame to be a full redraw and rebuilds the cached
        // background (used when the wallpaper changes).
        void invalidateBackground();

        // True when a redraw is pending.
        bool needsRender() const;

        void onBackendDestroy();

        Hyprutils::Math::Vector2D size() const;
        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> aq() const;

      private:
        Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> pickMode() const;
        const struct SMonitorRule*                                 matchingRule() const;
        bool                                                       allocateBuffers(const Hyprutils::Math::Vector2D& pixelSize);

        // Renders the wallpaper (or the solid background) into the cached
        // background pixels.
        void rebuildBackground();

        // Redraws only the given region of the scene: cached background
        // restore, cached shadows, window contents, title bars, switcher
        // and the cursor.
        void composite(uint32_t* pixels, int32_t stridePixels, const Hyprutils::Math::CRegion& damage);

        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> m_output;

        // The single KMS buffer presented to the display.
        Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer> m_buffer;

        // The always-complete desktop scene; the KMS buffer receives only
        // the damaged rows from it.
        std::vector<uint32_t> m_scene;

        Hyprutils::Math::Vector2D m_pixelSize;

        // Cached background: wallpaper rendered once, blitted under the
        // damaged rectangles of the scene.
        std::vector<uint32_t> m_backgroundPixels;
        bool                  m_backgroundDirty = true;

        // Per-window last rendered hover button, to damage the title bar on
        // hover changes.
        std::map<CWindow*, int> m_hoverState;

        bool m_needsRender = true;
        bool m_fullRedraw  = true;
        bool m_presented   = false;
        bool m_inPresent   = false;

        uint64_t m_nextCommitMs = 0;
        int      m_failures     = 0;

        Hyprutils::Signal::CHyprSignalListener m_destroyListener;
    };

}
