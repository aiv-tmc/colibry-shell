// GPU frame pass for the window switcher overlay.
//
// While the switcher is active the whole desktop is composed into an
// offscreen scene target, blurred through the Kawase pyramid, and then
// redrawn under a selection falloff dim: the area near the selected card
// darkens most, the far edge of the screen stays untouched. Cards are
// white translucent rounded rectangles with a white border showing the
// live window content.

#pragma once

#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace Aquamarine { class IBuffer; }

namespace Colibry {

    class CGLEngine;

    namespace SwitcherDraw {

        // Renders one full switcher frame (scene capture, blur, dim, cards,
        // mask, cursor) into the KMS target. Returns false on failure.
        bool renderFrame(CGLEngine& e, const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target,
                         const Hyprutils::Math::CRegion& damage);

    }

}
