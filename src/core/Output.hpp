// Per-output presentation for the ColibryShell compositor.
//
// COutput owns one KMS buffer and drives the GPU frame: it merges the
// accumulated damage, renders through CGLCompositor and commits the buffer
// to the CRTC. Commits are page-flip aware: while a flip is pending no new
// commit is attempted. Frame completion is signalled either by the
// backend's frame event or, as a fallback for stacks that never deliver
// it, by the frame timer in CCompositor; that is the point where
// wl_surface.frame done callbacks go to clients. Rendering is
// damage-driven, so a still desktop produces no work.

#pragma once

#include <aquamarine/buffer/Buffer.hpp>
#include <aquamarine/output/Output.hpp>

#include <cstdint>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>
#include <vector>

namespace ColibryShell {

    struct SMonitorRule;

    class COutput {
      public:
        explicit COutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output);

        bool setup();

        void present();
        void scheduleRender();

        // True once the first frame committed; outputs that appear before
        // the GPU renderer is ready stay unsetup until it is.
        bool presented() const { return m_presented; }

        // True while a page-flip for the last commit is still in flight.
        bool framePending() const { return m_framePending; }

        // Marks the next frame as fully damaged (cheap path; the wallpaper
        // is NOT re-rasterized).
        void invalidateBackground();

        // Forces a wallpaper re-rasterization and a full redraw; used when
        // the wallpaper image itself changed at runtime.
        void reloadBackground();

        bool     needsRender() const;
        uint64_t lastCommitMs() const;

        // Treats the pending flip as completed: fires frame-done to
        // clients and, with pending damage, commits the next frame. Called
        // by the backend's frame event and by the frame timer fallback.
        void recoverFrame();

        void onBackendDestroy();

        Hyprutils::Math::Vector2D                              size() const;
        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> aq() const;

      private:
        Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> pickMode() const;
        const SMonitorRule*                                        matchingRule() const;
        bool allocateBuffer(const Hyprutils::Math::Vector2D& pixelSize);
        void rebuildBackground();
        void onFrame();

        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> m_output;
        Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer> m_buffer;

        Hyprutils::Math::Vector2D m_pixelSize;

        std::vector<uint32_t> m_backgroundPixels;
        bool                  m_backgroundDirty = true;

        bool m_needsRender  = true;
        bool m_fullRedraw   = true;
        bool m_presented    = false;
        bool m_inPresent    = false;
        bool m_framePending = false;

        uint64_t m_nextCommitMs     = 0;
        uint64_t m_lastCommitMs     = 0;
        uint64_t m_lastFrameEventMs = 0;
        int      m_failures         = 0;

        Hyprutils::Signal::CHyprSignalListener m_destroyListener;
        Hyprutils::Signal::CHyprSignalListener m_frameListener;
    };

}
