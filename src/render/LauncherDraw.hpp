// GPU frame pass for the application launcher overlay.
//
// The launcher takes over the screen like the switcher: the desktop is
// captured into an offscreen scene, blurred through the Kawase pyramid and
// darkened; a translucent search bar and a grid of rounded application
// tiles sit on top, with the selected tile highlighted by a white border.

#pragma once

#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace Aquamarine { class IBuffer; }

namespace Colibry {

    class CGLEngine;

    namespace LauncherDraw {

        bool renderFrame(CGLEngine& e, const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target,
                         const Hyprutils::Math::CRegion& damage);

    }

}
