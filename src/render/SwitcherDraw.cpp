// Implementation of the switcher GPU frame pass.

#include "SwitcherDraw.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Surface.hpp"
#include "GLCompositor.hpp"
#include "SceneDraw.hpp"
#include "TitleText.hpp"

#include "Draw.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <vector>

namespace ColibryShell {
    namespace SwitcherDraw {

        namespace {

            uint32_t s_labelTex = 0;

            // Runs the Kawase pyramid over the captured scene: three
            // downsamples followed by two upsamples, leaving the final
            // blurred image in blur level 0.
            void blurScene(CGLEngine& e, int32_t w, int32_t h) {
                const float texel[] = {1.0f / w, 1.0f / h};

                auto runPass = [&](uint32_t prog, uint32_t srcTex, int level, float offset) {
                    e.bindBlur(level);
                    glViewport(0, 0, std::max(1, w >> (level + 1)), std::max(1, h >> (level + 1)));
                    glUseProgram(prog);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, srcTex);
                    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
                    glUniform2f(glGetUniformLocation(prog, "uTexel"), texel[0], texel[1]);
                    glUniform1f(glGetUniformLocation(prog, "uOffset"), offset);
                    const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
                    glEnableVertexAttribArray(0);
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glDisableVertexAttribArray(0);
                    glDisableVertexAttribArray(1);
                };

                runPass(e.progBlurDown(), e.sceneTexture(), 0, 1.0f);
                runPass(e.progBlurDown(), e.blurTexture(0), 1, 1.0f);
                runPass(e.progBlurDown(), e.blurTexture(1), 2, 1.0f);
                runPass(e.progBlurUp(), e.blurTexture(2), 1, 1.0f);
                runPass(e.progBlurUp(), e.blurTexture(1), 0, 1.0f);
            }

            // Draws one switcher card: white translucent background, white
            // border, live (dimmed when unselected) window content.
            void drawCard(CGLEngine& e, const CWindow& window, float x, float y, float w, float h, bool selected, float sw, float sh) {
                const float radius = 14.0f;

                // White border: a slightly larger white quad behind the card.
                Draw::solid(e, 1, 1, 1, 0.9f, x - 2, y - 2, w + 4, h + 4, radius + 2, sw, sh);

                // Card body: translucent white, stronger when selected.
                Draw::solid(e, 1, 1, 1, selected ? 0.30f : 0.16f, x, y, w, h, radius, sw, sh);

                // Window content preview clipped to the card.
                uint32_t tex = 0;
                int32_t  cw = 0, ch = 0;
                if (CGLCompositor::get().textureForSurface(window.surface(), tex, cw, ch)) {
                    Draw::quad(e, tex, x + 6, y + 6, w - 12, h - 12, selected ? 1.0f : 0.45f, radius - 4, sw, sh);
                }

                // Title label under the card.
                std::vector<uint32_t> pixels;
                int32_t               lw = 0, lh = 0;
                TitleText::rasterizeLabel(window.title(), (int32_t)w + 40, pixels, lw, lh);
                if (!pixels.empty()) {
                    if (!s_labelTex)
                        s_labelTex = e.createTexture();
                    e.uploadRGBA(s_labelTex, pixels.data(), lw, lh, lw);
                    Draw::quad(e, s_labelTex, x + w / 2 - lw / 2, y + h + 8, (float)lw, (float)lh, selected ? 1.0f : 0.6f, 0.0f, sw, sh);
                }
            }

        }

        bool renderFrame(CGLEngine& e, const Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>& target,
                         const Hyprutils::Math::CRegion& damage) {
            auto& compositor = CCompositor::get();
            const int32_t w  = target->size.x;
            const int32_t h  = target->size.y;
            const float   sw = (float)w;
            const float   sh = (float)h;

            e.makeCurrent();
            if (!e.bindTarget(target))
                return false;

            glViewport(0, 0, w, h);
            glEnable(GL_SCISSOR_TEST);

            // Capture the unblurred desktop for the overlay backdrop.
            e.bindScene(w, h);
            glViewport(0, 0, w, h);
            CGLCompositor::get().engine(); // no-op; scene uses same context
            // Background + windows reuse the scene passes at scene size.
            SceneDraw::drawWindows(e, w, h);

            e.ensureBlur(w, h);
            blurScene(e, w, h);

            // Selection geometry for the falloff dim.
            auto&       switcher  = CSwitcher::get();
            const auto& entries   = switcher.entries();
            const int   selected  = switcher.selected();
            const int   count     = (int)entries.size();
            const float cardW     = 240.0f;
            const float cardH     = 150.0f;
            const float gap       = 24.0f;
            const float rowW      = count * cardW + (count - 1) * gap;
            const float rowX      = (sw - rowW) / 2.0f;
            const float rowY      = sh * 0.5f - cardH / 2.0f;

            const float selX = rowX + selected * (cardW + gap);

            int         rectCount = 0;
            const auto* rects     = pixman_region32_rectangles(damage.pixman(), &rectCount);

            for (int i = 0; i < rectCount; ++i) {
                glScissor(rects[i].x1, h - rects[i].y2, rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);

                // Blurred desktop backdrop.
                Draw::quad(e, e.blurTexture(0), 0, 0, w, h, 1.0f, 0.0f, sw, sh);

                // Gradient dim: strongest around the selected card.
                const uint32_t prog = e.progDim();
                glUseProgram(prog);
                glUniform4f(glGetUniformLocation(prog, "uBox"), selX - 20, rowY - 20, cardW + 40, cardH + 60);
                glUniform1f(glGetUniformLocation(prog, "uFalloff"), std::max(sw, sh) * 0.6f);
                glUniform1f(glGetUniformLocation(prog, "uDark"), 0.55f);
                glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), 0, 0);
                glUniform2f(glGetUniformLocation(prog, "uDstSize"), sw, sh);
                glUniform2f(glGetUniformLocation(prog, "uScreenSize"), sw, sh);
                const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
                glEnableVertexAttribArray(0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableVertexAttribArray(0);
                glDisableVertexAttribArray(1);

                for (int c = 0; c < count; ++c) {
                    if (!entries[c])
                        continue;
                    drawCard(e, *entries[c], rowX + c * (cardW + gap), rowY, cardW, cardH, c == selected, sw, sh);
                }

                SceneDraw::drawCursor(e, w, h);
            }

            glDisable(GL_SCISSOR_TEST);
            return true;
        }

    }
}
