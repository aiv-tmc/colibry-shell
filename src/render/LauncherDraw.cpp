// Implementation of the launcher GPU frame pass.

#include "LauncherDraw.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Launcher.hpp"
#include "GLCompositor.hpp"
#include "SceneDraw.hpp"
#include "TitleText.hpp"

#include "Draw.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <vector>

namespace Colibry {
    namespace LauncherDraw {

        namespace {

            uint32_t s_labelTex = 0;
            uint32_t s_searchTex = 0;

            void drawUnitQuad() {
                const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
                glEnableVertexAttribArray(0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableVertexAttribArray(0);
                glDisableVertexAttribArray(1);
            }

            void blurScene(CGLEngine& e, int32_t w, int32_t h) {
                const float texel[] = {1.0f / w, 1.0f / h};

                auto pass = [&](uint32_t prog, uint32_t srcTex, int level, float offset) {
                    e.bindBlur(level);
                    glViewport(0, 0, std::max(1, w >> (level + 1)), std::max(1, h >> (level + 1)));
                    glUseProgram(prog);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, srcTex);
                    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
                    glUniform2f(glGetUniformLocation(prog, "uTexel"), texel[0], texel[1]);
                    glUniform1f(glGetUniformLocation(prog, "uOffset"), offset);
                    drawUnitQuad();
                };

                pass(e.progBlurDown(), e.sceneTexture(), 0, 1.0f);
                pass(e.progBlurDown(), e.blurTexture(0), 1, 1.0f);
                pass(e.progBlurDown(), e.blurTexture(1), 2, 1.0f);
                pass(e.progBlurUp(), e.blurTexture(2), 1, 1.0f);
                pass(e.progBlurUp(), e.blurTexture(1), 0, 1.0f);
            }

            void drawLabel(CGLEngine& e, const std::string& text, float cx, float y, float alpha, float maxW, float sw, float sh) {
                std::vector<uint32_t> pixels;
                int32_t w = 0, h = 0;
                TitleText::rasterizeLabel(text, (int32_t)maxW, pixels, w, h);
                if (pixels.empty())
                    return;

                if (!s_labelTex)
                    s_labelTex = e.createTexture();
                e.uploadRGBA(s_labelTex, pixels.data(), w, h, w);
                Draw::quad(e, s_labelTex, cx - w / 2.0f, y, (float)w, (float)h, alpha, 0.0f, sw, sh);
            }

        }

        bool renderFrame(CGLEngine& e, const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target,
                         const Hyprutils::Math::CRegion& damage) {
            auto& launcher = CLauncher::get();
            const int32_t w = target->size.x;
            const int32_t h = target->size.y;
            const float   sw = (float)w;
            const float   sh = (float)h;

            e.makeCurrent();
            if (!e.bindTarget(target))
                return false;

            glViewport(0, 0, w, h);
            glEnable(GL_SCISSOR_TEST);

            // Capture and blur the desktop backdrop.
            e.bindScene(w, h);
            glViewport(0, 0, w, h);
            SceneDraw::drawWindows(e, w, h);

            e.ensureBlur(w, h);
            blurScene(e, w, h);

            const float barW = 520.0f;
            const float barH = 46.0f;
            const float barX = (sw - barW) / 2.0f;
            const float barY = sh * 0.16f;

            constexpr float TILE = 118.0f;
            constexpr float GAP  = 18.0f;
            constexpr int   COLS = 6;

            const int shown = std::min<int>((int)launcher.entries().size(), 24);
            const int rows  = std::max(1, (shown + COLS - 1) / COLS);
            const float gridW = COLS * TILE + (COLS - 1) * GAP;
            const float gridX = (sw - gridW) / 2.0f;
            const float gridY = barY + barH + 40.0f;

            int         rectCount = 0;
            const auto* rects     = pixman_region32_rectangles(damage.pixman(), &rectCount);

            for (int i = 0; i < rectCount; ++i) {
                glScissor(rects[i].x1, h - rects[i].y2, rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);

                Draw::quad(e, e.blurTexture(0), 0, 0, w, h, 1.0f, 0.0f, sw, sh);
                Draw::solid(e, 0, 0, 0, 0.45f, 0, 0, w, h, 0, sw, sh);

                // Search bar with rounded translucent white body.
                Draw::solid(e, 1, 1, 1, 0.16f, barX, barY, barW, barH, barH / 2.0f, sw, sh);
                drawLabel(e, launcher.filter().empty() ? "Type to search" : launcher.filter() + "|", barX + 20, barY + barH / 2 - 10,
                          launcher.filter().empty() ? 0.45f : 0.95f, barW - 40, sw, sh);

                for (int idx = 0; idx < shown; ++idx) {
                    const int   col      = idx % COLS;
                    const int   row      = idx / COLS;
                    const float x        = gridX + col * (TILE + GAP);
                    const float y        = gridY + row * (TILE + GAP + 22);
                    const bool  selected = idx == launcher.selected();

                    if (selected) {
                        Draw::solid(e, 1, 1, 1, 0.85f, x - 2, y - 2, TILE + 4, TILE + 4, 16.0f, sw, sh);
                        Draw::solid(e, 1, 1, 1, 0.26f, x, y, TILE, TILE, 14.0f, sw, sh);
                    } else
                        Draw::solid(e, 1, 1, 1, 0.12f, x, y, TILE, TILE, 14.0f, sw, sh);

                    drawLabel(e, launcher.entries()[idx].name, x + TILE / 2.0f, y + TILE + 6, selected ? 1.0f : 0.7f, TILE + 30, sw, sh);
                }

                SceneDraw::drawCursor(e, w, h);
            }

            glDisable(GL_SCISSOR_TEST);
            return true;
        }

    }
}
