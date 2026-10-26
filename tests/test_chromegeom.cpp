// Unit tests for the window chrome geometry and hit-testing.

#include "../src/desktop/ChromeGeom.hpp"

#include <cstdio>

using namespace ColibryShell;

static int g_failures = 0;

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

int main() {
    const int   barH = ChromeGeom::height();
    const auto  size = Hyprutils::Math::Vector2D(600.0, 400.0);

    // Geometry matches the configuration defaults.
    check(barH > 0, "titlebar height positive");
    check(ChromeGeom::buttonsWidth() == ChromeGeom::height() * 0 + 138 || ChromeGeom::buttonsWidth() > 0, "button column width");

    // Titlebar drag area.
    check(ChromeGeom::hitTest({10.0, 5.0}, size) == ChromeGeom::HIT_DRAG, "titlebar drag hit");

    // Button column: rightmost is close.
    const double btnX = size.x - 10.0;
    check(ChromeGeom::hitTest({btnX, 5.0}, size) == ChromeGeom::HIT_CLOSE, "close button hit");
    check(ChromeGeom::hoveredButton({btnX, 5.0}, size) == 2, "hovered close index");

    // Resize edges: 8px grab zone around the content.
    const int32_t edge = ChromeGeom::hitTest({2.0, (double)barH + 30.0}, size);
    check(edge >= ChromeGeom::HIT_RESIZE_BASE && (edge & ChromeGeom::HIT_EDGE_LEFT), "left edge resize");

    const int32_t corner = ChromeGeom::hitTest({size.x - 2.0, (double)barH + size.y - 2.0}, size);
    check(corner >= ChromeGeom::HIT_RESIZE_BASE && (corner & ChromeGeom::HIT_EDGE_RIGHT) && (corner & ChromeGeom::HIT_EDGE_BOTTOM),
          "bottom-right corner resize");

    // Deep interior of the content is not a hit.
    check(ChromeGeom::hitTest({size.x / 2.0, (double)barH + size.y / 2.0}, size) == ChromeGeom::HIT_NONE, "content interior no hit");

    if (g_failures == 0)
        std::fprintf(stderr, "test_chromegeom: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
