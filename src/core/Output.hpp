// Per-output presentation for the Colibry compositor.
//
// COutput owns one KMS buffer and drives the GPU frame: it merges the
// accumulated damage, renders through CGLCompositor and commits the buffer
// to the CRTC. All compositing happens on the GPU; this class only
// schedules and presents. Rendering is damage-driven, so a still desktop
// produces no work at all.

#pragma once

#include <aquamarine/buffer/Buffer.hpp>
#include <aquamarine/output/Output.hpp>

#include <cstdint>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>
#include <vector>

namespace Colibry {

    struct SMonitorRule;

    class COutput {
      public:
        explicit COutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output);

        bool setup();

        void present();
        void scheduleRender();

        // Marks the next frame as fully damaged (cheap path; the wallpaper
        // is NOT re-rasterized).
        void invalidateBackground();

        // Forces a wallpaper re-rasterization and a full redraw; used when
        // the wallpaper image itself changed at runtime.
        void reloadBackground();

        bool     needsRender() const;
        uint64_t lastCommitMs() const;

        void onBackendDestroy();

        Hyprutils::Math::Vector2D                            size() const;
        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> aq() const;

      private:
        Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> pickMode() const;
        const SMonitorRule*                                        matchingRule() const;
        bool allocateBuffer(const Hyprutils::Math::Vector2D& pixelSize);
        void rebuildBackground();

        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> m_output;
        Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer> m_buffer;

        Hyprutils::Math::Vector2D m_pixelSize;

        std::vector<uint32_t> m_backgroundPixels;
        bool                  m_backgroundDirty = true;

        bool m_needsRender = true;
        bool m_fullRedraw  = true;
        bool m_presented   = false;
        bool m_inPresent   = false;

        uint64_t m_nextCommitMs = 0;
        uint64_t m_lastCommitMs = 0;
        int      m_failures     = 0;

        Hyprutils::Signal::CHyprSignalListener m_destroyListener;
    };

}
