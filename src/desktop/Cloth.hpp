// Elastic cloth simulation for Colibry windows.
//
// Every window carries a grid of vertices spanning its full box
// (titlebar included). Vertices integrate with Verlet dynamics and are
// stitched together by distance constraints, so the surface behaves like
// a stretched sheet: the grabbed corner follows the pointer immediately
// while the rest of the window lags, overshoots and ripples. The same
// simulation runs during every automatic relocation (tiling, snapping,
// maximizing, workspace switches), which makes the material visibly
// settle into its new shape.

#pragma once

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <cstdint>
#include <vector>

namespace ColibryShell {

    // Grid resolution in cells; 8x8 cells is a good trade-off between
    // visible flex and per-frame CPU cost.
    static constexpr int CLOTH_COLS = 8;
    static constexpr int CLOTH_ROWS = 8;

    struct SClothMesh {
        // Rest configuration follows the logical window box; positions are
        // the simulated state that lags behind it.
        std::vector<Hyprutils::Math::Vector2D> rest, pos, prev;

        // Fractional coordinates of each vertex inside the box (0..1),
        // used to rebuild the rest positions after every resize.
        std::vector<float> fx, fy;

        // Triangle indices, split at the titlebar boundary so the two
        // halves can be drawn with their own shaders.
        std::vector<uint32_t> indicesBar, indicesContent;

        bool pinned = false;
        int  pinIndex = 0;

        // Grab point in window-local pixels and the absolute position the
        // pin is dragged toward.
        Hyprutils::Math::Vector2D pinLocal, pinTarget;
    };

    class CCloth {
      public:
        // Rebuilds the mesh geometry for the given box. With `scale` < 1
        // the simulated positions start compressed around the box center,
        // which produces the elastic grow-in on window open.
        void reset(const Hyprutils::Math::CBox& box, double titlebarH, double scale = 1.0);

        // Pins the vertex closest to the window-local point; subsequent
        // moveTo calls drag exactly that point.
        void grab(const Hyprutils::Math::Vector2D& local);
        void moveTo(const Hyprutils::Math::Vector2D& target);
        void release();

        // Activates the simulation so the mesh lags behind rest changes.
        void nudge();

        // Advances the simulation by a frame delta (milliseconds). The
        // caller passes the current logical box and titlebar height, which
        // define the rest configuration. While `closing` is set the rest
        // configuration shrinks toward the center by `closeAlpha`,
        // producing the shrink-out. Returns true while the surface is still
        // moving and needs repainting.
        bool step(const Hyprutils::Math::CBox& box, double titlebarH, double dtMs, bool closing, double closeAlpha);

        bool              active() const;
        const SClothMesh& mesh() const;

      private:
        void satisfy(Hyprutils::Math::Vector2D& a, Hyprutils::Math::Vector2D& b, double restLength, int indexA, int indexB);

        SClothMesh m_mesh;
        bool       m_active = false;
    };

}
