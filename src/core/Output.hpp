// Per-output presentation for the ColibryShell compositor.
//
// COutput owns a small pool of KMS buffers (three by default, growing to
// four under sustained commit rejections) and rotates through it; every
// buffer carries the damage backlog it missed, so each commit renders a
// complete, tear-free frame. When HDR is enabled in the config and the
// panel reports support, the output state carries static HDR10 metadata
// (PQ transfer, D65 white, 1000-nit peak); otherwise the output stays in
// SDR and the decision is logged.

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

        bool presented() const { return m_presented; }
        bool framePending() const { return m_framePending; }
        bool hdrActive() const { return m_hdrActive; }

        void invalidateBackground();
        void reloadBackground();

        bool     needsRender() const;
        uint64_t lastCommitMs() const;

        uint64_t frameEvents() const { return m_frameEvents; }
        uint64_t safetyNets() const { return m_safetyNets; }
        uint64_t rejections() const { return m_rejections; }
        int      bufferCount() const { return m_bufferCount; }
        int      pendingDamageRects() const;

        void recoverFrame();

        void onBackendDestroy();

        Hyprutils::Math::Vector2D                              size() const;
        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> aq() const;

      private:
        static constexpr int MAX_BUFFERS = 4;

        Hyprutils::Memory::CSharedPointer<Aquamarine::SOutputMode> pickMode() const;
        const SMonitorRule*                                        matchingRule() const;
        bool allocateBuffers(const Hyprutils::Math::Vector2D& pixelSize);
        bool growBufferPool();
        void rebuildBackground();
        void onFrame();
        void tryEnableHDR();

        Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> m_output;

        Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer> m_buffers[MAX_BUFFERS];
        int                                                    m_bufferCount = 3;
        int                                                    m_bufferIndex  = 0;

        Hyprutils::Math::CRegion m_pendingDamage[MAX_BUFFERS];

        Hyprutils::Math::Vector2D m_pixelSize;

        std::vector<uint32_t> m_backgroundPixels;
        bool                  m_backgroundDirty = true;

        bool m_hdrActive = false;

        bool m_needsRender  = true;
        bool m_fullRedraw   = true;
        bool m_presented    = false;
        bool m_inPresent    = false;
        bool m_framePending = false;

        uint64_t m_nextCommitMs     = 0;
        uint64_t m_lastCommitMs     = 0;
        uint64_t m_lastFrameEventMs = 0;

        uint64_t m_frameEvents = 0;
        uint64_t m_safetyNets  = 0;
        uint64_t m_rejections  = 0;

        Hyprutils::Signal::CHyprSignalListener m_destroyListener;
        Hyprutils::Signal::CHyprSignalListener m_frameListener;
    };

}
