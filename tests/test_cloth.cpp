// Unit tests for the elastic cloth simulation.
//
// Covers: settled inactivity, nudge activation, grab/pin exactness with
// window-local coordinates, elastic convergence after release, and the
// grow-in reset.

#include "../src/desktop/Cloth.hpp"

#include <cmath>
#include <cstdio>

using namespace ColibryShell;
using Hyprutils::Math::CBox;
using Hyprutils::Math::Vector2D;

static int g_failures = 0;

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool near(double a, double b, double eps = 0.5) {
    return std::abs(a - b) <= eps;
}

int main() {
    const CBox   box(100.0, 50.0, 200.0, 150.0);
    const double barH = 20.0;

    // A freshly reset mesh at scale 1 is already settled.
    CCloth cloth;
    cloth.reset(box, barH, 1.0);
    check(!cloth.active(), "reset at scale 1 starts inactive");
    check(cloth.mesh().pos.size() == (size_t)((CLOTH_COLS + 1) * (CLOTH_ROWS + 1)), "mesh vertex count");

    // Nudging activates; with an unchanged box it settles again.
    cloth.nudge();
    check(cloth.active(), "nudge activates");
    for (int i = 0; i < 120; ++i)
        cloth.step(box, barH, 16.6, false, 1.0);
    check(!cloth.active(), "settled after nudge");

    // Grab uses window-local coordinates: the vertex nearest to the local
    // point must be the one pinned, and it lands at target + local.
    const Vector2D grabLocal(180.0, 160.0); // near the bottom-right of the box
    cloth.grab(grabLocal);
    cloth.moveTo(Vector2D(300.0, 200.0)); // window origin target

    bool moving = cloth.step(box, barH, 16.6, false, 1.0);
    check(moving, "pinned cloth keeps animating");

    const Vector2D expectedPin(300.0 + grabLocal.x, 200.0 + grabLocal.y);
    bool           pinAtTarget = false;
    for (const auto& p : cloth.mesh().pos) {
        if (near(p.x, expectedPin.x, 0.01) && near(p.y, expectedPin.y, 0.01))
            pinAtTarget = true;
    }
    check(pinAtTarget, "pinned vertex sits at target + grab offset");

    // Releasing lets the surface converge back onto the rest shape.
    cloth.release();
    for (int i = 0; i < 300; ++i)
        cloth.step(box, barH, 16.6, false, 1.0);
    check(!cloth.active(), "settled after release");
    for (size_t i = 0; i < cloth.mesh().pos.size(); ++i) {
        check(near(cloth.mesh().pos[i].x, cloth.mesh().rest[i].x, 0.5), "vertex converged (x)");
        check(near(cloth.mesh().pos[i].y, cloth.mesh().rest[i].y, 0.5), "vertex converged (y)");
    }

    // A grow-in reset starts compressed, overshoots and settles.
    CCloth grow;
    grow.reset(box, barH, 0.72);
    check(grow.active(), "grow-in starts active");
    double maxStartDev = 0.0;
    for (size_t i = 0; i < grow.mesh().pos.size(); ++i)
        maxStartDev = std::max(maxStartDev, std::abs(grow.mesh().pos[i].y - grow.mesh().rest[i].y));
    check(maxStartDev > 10.0, "grow-in starts deformed");
    for (int i = 0; i < 300; ++i)
        grow.step(box, barH, 16.6, false, 1.0);
    check(!grow.active(), "grow-in settles");

    if (g_failures == 0)
        std::fprintf(stderr, "test_cloth: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
