// Implementation of the elastic cloth simulation.

#include "Cloth.hpp"

#include <algorithm>
#include <cmath>

namespace Colibry {

    using Hyprutils::Math::CBox;
    using Hyprutils::Math::Vector2D;

    // Verlet velocity retention: slightly below 1 keeps the wobble alive
    // for a moment before the constraints bleed the energy off.
    static constexpr double CLOTH_DAMPING = 0.982;

    // Relaxation passes over the distance constraints per frame.
    static constexpr int CLOTH_ITERATIONS = 3;

    // Fraction of the rest gap closed per frame: soft enough to ripple,
    // stiff enough to never look loose.
    static constexpr double CLOTH_PULL = 0.16;

    static int vertexCount() {
        return (CLOTH_COLS + 1) * (CLOTH_ROWS + 1);
    }

    void CCloth::reset(const CBox& box, double titlebarH, double scale) {
        const int count = vertexCount();
        const int vertsX = CLOTH_COLS + 1;

        m_mesh.rest.resize(count);
        m_mesh.pos.resize(count);
        m_mesh.prev.resize(count);
        m_mesh.fx.resize(count);
        m_mesh.fy.resize(count);

        const Vector2D boxPos  = box.pos();
        const Vector2D boxSize = box.size();
        const Vector2D center  = boxPos + boxSize / 2.0;

        for (int y = 0; y <= CLOTH_ROWS; ++y) {
            for (int x = 0; x <= CLOTH_COLS; ++x) {
                const int i = y * vertsX + x;

                const double u = (double)x / CLOTH_COLS;
                const double v = (double)y / CLOTH_ROWS;

                m_mesh.fx[i] = (float)u;
                m_mesh.fy[i] = (float)v;

                const Vector2D r(boxPos.x + u * boxSize.x, boxPos.y + v * boxSize.y);
                m_mesh.rest[i] = r;
                m_mesh.pos[i]  = center + (r - center) * scale;
                m_mesh.prev[i] = m_mesh.pos[i];
            }
        }

        if (m_mesh.indicesBar.empty()) {
            for (int y = 0; y < CLOTH_ROWS; ++y) {
                for (int x = 0; x < CLOTH_COLS; ++x) {
                    const uint32_t a = (uint32_t)(y * vertsX + x);
                    const uint32_t b = a + 1;
                    const uint32_t c = a + vertsX;
                    const uint32_t d = c + 1;

                    const double cellBottom = (double)(y + 1) / CLOTH_ROWS * boxSize.y;
                    auto& target            = cellBottom <= titlebarH + 1.0 ? m_mesh.indicesBar : m_mesh.indicesContent;

                    target.insert(target.end(), {a, c, b, b, c, d});
                }
            }
        }

        m_active = scale < 0.999;
    }

    void CCloth::grab(const Vector2D& local) {
        m_mesh.pinned = true;

        double best = 1e18;
        for (int i = 0; i < vertexCount(); ++i) {
            const double dx = m_mesh.rest[i].x - local.x;
            const double dy = m_mesh.rest[i].y - local.y;
            const double d  = dx * dx + dy * dy;
            if (d < best) {
                best            = d;
                m_mesh.pinIndex = i;
            }
        }

        m_mesh.pinLocal = local;
    }

    void CCloth::moveTo(const Vector2D& target) {
        m_mesh.pinTarget = target;
    }

    void CCloth::release() {
        m_mesh.pinned = false;
    }

    void CCloth::nudge() {
        if (m_mesh.pos.empty())
            reset(CBox(Vector2D(0.0, 0.0), Vector2D(100.0, 100.0)), 32.0, 1.0);
        m_active = true;
    }

    bool CCloth::active() const {
        return m_active;
    }

    const SClothMesh& CCloth::mesh() const {
        return m_mesh;
    }

    void CCloth::satisfy(Vector2D& a, Vector2D& b, double restLength, int indexA, int indexB) {
        if (indexA == m_mesh.pinIndex && m_mesh.pinned)
            return;
        if (indexB == m_mesh.pinIndex && m_mesh.pinned)
            return;

        const Vector2D delta = b - a;
        const double dist    = std::hypot(delta.x, delta.y);
        if (dist < 1e-6)
            return;

        const double diff = (dist - restLength) / dist / 2.0;
        a += delta * diff;
        b -= delta * diff;
    }

    bool CCloth::step(const CBox& box, double titlebarH, double dtMs, bool closing, double closeAlpha) {
        if (!m_active)
            return false;

        const int    vertsX = CLOTH_COLS + 1;
        const double dt     = std::min(dtMs, 33.0) / 16.667;

        const Vector2D boxPos  = box.pos();
        const Vector2D boxSize = box.size();

        // Rebuild the rest configuration from the logical box; while
        // closing it collapses toward the center by the fade factor.
        const Vector2D center = boxPos + boxSize / 2.0;
        for (int i = 0; i < vertexCount(); ++i) {
            Vector2D r(boxPos.x + m_mesh.fx[i] * boxSize.x, boxPos.y + m_mesh.fy[i] * boxSize.y);
            if (closing)
                r = center + (r - center) * std::max(0.35, closeAlpha);
            m_mesh.rest[i] = r;
        }

        // Verlet integration with a soft pull toward rest: inertia keeps
        // the overshoot, the pull keeps the sheet coherent.
        double maxDev = 0.0;
        for (int i = 0; i < vertexCount(); ++i) {
            if (m_mesh.pinned && i == m_mesh.pinIndex) {
                m_mesh.pos[i]  = m_mesh.pinTarget;
                m_mesh.prev[i] = m_mesh.pinTarget;
                continue;
            }

            const Vector2D vel = (m_mesh.pos[i] - m_mesh.prev[i]) * CLOTH_DAMPING;
            m_mesh.prev[i]     = m_mesh.pos[i];
            m_mesh.pos[i] += vel;
            m_mesh.pos[i] += (m_mesh.rest[i] - m_mesh.pos[i]) * CLOTH_PULL * dt;

            maxDev = std::max(maxDev, std::abs(m_mesh.pos[i].x - m_mesh.rest[i].x));
            maxDev = std::max(maxDev, std::abs(m_mesh.pos[i].y - m_mesh.rest[i].y));
        }

        // Distance constraints keep adjacent vertices at their rest
        // spacing, which is what makes the surface read as one material.
        const double cellW = boxSize.x / CLOTH_COLS;
        const double cellH = boxSize.y / CLOTH_ROWS;

        for (int iteration = 0; iteration < CLOTH_ITERATIONS; ++iteration) {
            for (int y = 0; y <= CLOTH_ROWS; ++y) {
                for (int x = 0; x <= CLOTH_COLS; ++x) {
                    const int i = y * vertsX + x;
                    if (x < CLOTH_COLS)
                        satisfy(m_mesh.pos[i], m_mesh.pos[i + 1], cellW, i, i + 1);
                    if (y < CLOTH_ROWS)
                        satisfy(m_mesh.pos[i], m_mesh.pos[i + vertsX], cellH, i, i + vertsX);
                }
            }
        }

        if (m_mesh.pinned)
            return true;

        if (maxDev < 0.35 && !closing) {
            m_active = false;
            for (int i = 0; i < vertexCount(); ++i)
                m_mesh.pos[i] = m_mesh.rest[i];
            return false;
        }

        return true;
    }

}
