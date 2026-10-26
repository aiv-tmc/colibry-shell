// Implementation of the desktop scene passes.

#include "SceneDraw.hpp"

#include "../config/ConfigManager.hpp"
#include "../core/Compositor.hpp"
#include "../desktop/ChromeGeom.hpp"
#include "../desktop/Cursor.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Surface.hpp"
#include "GLCompositor.hpp"
#include "TitleText.hpp"

#include "Draw.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace ColibryShell {
    namespace SceneDraw {

        namespace {

            struct SShadowKey {
                int32_t w, h, r;
                bool    operator<(const SShadowKey& o) const {
                    return w < o.w || (w == o.w && (h < o.h || (h == o.h && r < o.r)));
                }
            };

            std::map<SShadowKey, uint32_t> s_shadows;
            uint32_t                       s_overlayTex = 0;
            std::vector<uint32_t>          s_overlayPixels;
            std::string                    s_overlayTitle;
            int32_t                        s_overlayW = 0;
            int32_t                        s_overlayH = 0;
            int32_t                        s_overlayHover = -1;
            uint32_t                       s_cursorTex = 0;
            std::vector<uint32_t>          s_cursorPixels;
            int32_t                        s_cursorW = 0;
            int32_t                        s_cursorH = 0;

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

            void blurPass(CGLEngine& e, uint32_t prog, uint32_t srcTex, int dst, float offset, int srcW, int srcH) {
                e.bindBlur(dst);
                glViewport(0, 0, std::max(1, srcW >> (dst + 1)), std::max(1, srcH >> (dst + 1)));
                glUseProgram(prog);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, srcTex);
                glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
                glUniform2f(glGetUniformLocation(prog, "uTexel"), 1.0f / srcW, 1.0f / srcH);
                glUniform1f(glGetUniformLocation(prog, "uOffset"), offset);
                drawUnitQuad();
            }

            // Generates the blurred silhouette for one window size on the
            // GPU and caches the result.
            uint32_t shadowTexture(CGLEngine& e, int32_t w, int32_t h, int32_t r, double opacity) {
                const SShadowKey key{w, h, r};
                const auto       it = s_shadows.find(key);
                if (it != s_shadows.end())
                    return it->second;

                e.ensureBlur(w * 2, h * 2);
                e.bindEffect();
                glViewport(0, 0, std::max(1, w / 4), std::max(1, h / 4));
                Draw::solid(e, 0, 0, 0, (float)opacity, 0, 0, (float)w, (float)h, (float)r, (float)w, (float)h);

                blurPass(e, e.progBlurDown(), e.effectTexture(), 0, 1.0f, w, h);
                blurPass(e, e.progBlurDown(), e.blurTexture(0), 1, 1.0f, w, h);
                blurPass(e, e.progBlurUp(), e.blurTexture(1), 0, 1.0f, w, h);

                const uint32_t tex = e.createTexture();
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, std::max(1, w / 2), std::max(1, h / 2), 0);

                if (s_shadows.size() > 24) {
                    e.deleteTexture(s_shadows.begin()->second);
                    s_shadows.erase(s_shadows.begin());
                }
                s_shadows[key] = tex;
                return tex;
            }

            // Uploads the titlebar overlay (title and button glyphs) only
            // when the title, geometry or hover state changed.
            uint32_t overlayTexture(CGLEngine& e, const CWindow& window, int32_t w, int32_t h) {
                const int         hover = window.hoveredTitleButton();
                const std::string& title = window.title();

                if (!s_overlayTex || s_overlayTitle != title || s_overlayW != w || s_overlayH != h || s_overlayHover != hover) {
                    TitleText::rasterizeOverlay(title, w, h, ChromeGeom::buttonsWidth(), hover, s_overlayPixels);
                    if (!s_overlayTex)
                        s_overlayTex = e.createTexture();
                    e.uploadRGBA(s_overlayTex, s_overlayPixels.data(), w, h, w);
                    s_overlayTitle = title;
                    s_overlayW     = w;
                    s_overlayH     = h;
                    s_overlayHover = hover;
                }
                return s_overlayTex;
            }

            // Fills interleaved position and UV float arrays from the cloth
            // mesh. Titlebar UVs span the bar height; content UVs span the
            // remaining area, clamped so edge vertices never sample
            // outside the texture.
            void fillMeshArrays(const SClothMesh& mesh, const Hyprutils::Math::CBox& box, double barH, std::vector<float>& positions,
                                std::vector<float>& uvBar, std::vector<float>& uvContent) {
                const double boxH = box.size().y;
                const double contentH = std::max(1.0, boxH - barH);

                const size_t count = mesh.pos.size();
                positions.resize(count * 2);
                uvBar.resize(count * 2);
                uvContent.resize(count * 2);

                for (size_t i = 0; i < count; ++i) {
                    positions[i * 2]     = (float)mesh.pos[i].x;
                    positions[i * 2 + 1] = (float)mesh.pos[i].y;

                    const double u = mesh.fx[i];
                    uvBar[i * 2]     = (float)u;
                    uvBar[i * 2 + 1] = (float)std::clamp((mesh.fy[i] * boxH) / std::max(1.0, barH), 0.0, 1.0);

                    uvContent[i * 2]     = (float)u;
                    uvContent[i * 2 + 1] = (float)std::clamp((mesh.fy[i] * boxH - barH) / contentH, 0.0, 1.0);
                }
            }

            // Draws a window through its deformed cloth mesh: the titlebar
            // rows keep the gradient shader, the content rows sample the
            // client texture, and the shadow follows the average surface
            // displacement for a grounded feel.
            void drawWindowCloth(CGLEngine& e, const CWindow& window, uint32_t contentTex, bool hasContent, float alpha, float sw, float sh) {
                const auto& mesh = window.clothMesh();
                const auto  box  = CCompositor::get().windowBox(const_cast<CWindow*>(&window));
                const auto  boxPos  = box.pos();
                const auto  boxSize = box.size();
                const int   barH = ChromeGeom::height();

                // Average displacement of the surface from its rigid rest
                // position; the shadow leans with it.
                Hyprutils::Math::Vector2D avg = {0, 0};
                for (size_t i = 0; i < mesh.pos.size(); ++i)
                    avg += mesh.pos[i] - mesh.rest[i];
                avg /= (double)mesh.pos.size();

                if (!window.isFullscreen() && CConfigManager::get().shadowEnabled()) {
                    const double opacity = CConfigManager::get().shadowOpacity() * alpha;
                    const uint32_t shadow = shadowTexture(e, window.size().x, window.size().y, CConfigManager::get().shadowRounding(), opacity);
                    const float offX = (float)(CConfigManager::get().shadowOffsetX() + avg.x * 0.6);
                    const float offY = (float)(CConfigManager::get().shadowOffsetY() + avg.y * 0.6);
                    Draw::quad(e, shadow, (float)boxPos.x - (float)boxSize.x * 0.25f + offX, (float)boxPos.y - (float)boxSize.y * 0.25f + offY,
                               (float)boxSize.x * 1.5f, (float)boxSize.y * 1.5f, 1.0f, 0.0f, sw, sh);
                }

                std::vector<float> positions, uvBar, uvContent;
                fillMeshArrays(mesh, box, barH, positions, uvBar, uvContent);

                // Titlebar rows with the gradient shader.
                const uint32_t overlay = overlayTexture(e, window, window.size().x, barH);
                const uint32_t barProg = e.progTitlebar();
                glUseProgram(barProg);
                glUniform4f(glGetUniformLocation(barProg, "uFallback"), 0.16f, 0.17f, 0.20f, 1.0f);
                glUniform1f(glGetUniformLocation(barProg, "uHasContent"), hasContent ? 1.0f : 0.0f);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, hasContent ? contentTex : overlay);
                glUniform1i(glGetUniformLocation(barProg, "uContentTex"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, overlay);
                glUniform1i(glGetUniformLocation(barProg, "uOverlayTex"), 1);
                Draw::mesh(e, barProg, overlay, positions.data(), uvBar.data(), (int)mesh.pos.size(), mesh.indicesBar.data(), (int)mesh.indicesBar.size(),
                           alpha, sw, sh);

                // Content rows with the client texture.
                if (hasContent)
                    Draw::mesh(e, e.progQuad(), contentTex, positions.data(), uvContent.data(), (int)mesh.pos.size(), mesh.indicesContent.data(),
                               (int)mesh.indicesContent.size(), alpha, sw, sh);
            }

            void drawWindow(CGLEngine& e, const CWindow& window, int32_t screenW, int32_t screenH) {
                const auto pos  = window.position();
                const auto size = window.size();
                const bool fs   = window.isFullscreen();

                const float sw = (float)screenW;
                const float sh = (float)screenH;

                const float alpha = (float)window.opacity();

                uint32_t contentTex = 0;
                int32_t  cw = 0, ch = 0;
                const bool hasContent = CGLCompositor::get().textureForSurface(window.surface(), contentTex, cw, ch);

                // While the cloth is moving, the window renders as a
                // deformed mesh instead of a plain quad.
                if (window.clothActive()) {
                    drawWindowCloth(e, window, contentTex, hasContent, alpha, sw, sh);
                    return;
                }

                const int barH  = ChromeGeom::height();
                const int round = CConfigManager::get().windowRounding();

                if (!fs && CConfigManager::get().shadowEnabled()) {
                    const double opacity = CConfigManager::get().shadowOpacity() * alpha;
                    const uint32_t shadow = shadowTexture(e, size.x, size.y, CConfigManager::get().shadowRounding(), opacity);
                    Draw::quad(e, shadow, (float)pos.x - (float)size.x * 0.25f + (float)CConfigManager::get().shadowOffsetX(),
                               (float)pos.y - (float)size.y * 0.25f + (float)CConfigManager::get().shadowOffsetY(), (float)size.x * 1.5f,
                               (float)size.y * 1.5f, alpha, 0.0f, sw, sh);
                }

                if (!fs) {
                    const uint32_t overlay = overlayTexture(e, window, size.x, barH);
                    const uint32_t prog    = e.progTitlebar();
                    glUseProgram(prog);
                    glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), (float)pos.x, (float)pos.y);
                    glUniform2f(glGetUniformLocation(prog, "uDstSize"), (float)size.x, (float)barH);
                    glUniform2f(glGetUniformLocation(prog, "uScreenSize"), sw, sh);
                    glUniform1f(glGetUniformLocation(prog, "uRadius"), (float)round);
                    glUniform2f(glGetUniformLocation(prog, "uQuadSize"), (float)size.x, (float)barH);
                    glUniform2f(glGetUniformLocation(prog, "uQuadOrigin"), (float)pos.x, (float)pos.y);
                    glUniform4f(glGetUniformLocation(prog, "uFallback"), 0.16f, 0.17f, 0.20f, 1.0f);
                    glUniform1f(glGetUniformLocation(prog, "uHasContent"), hasContent ? 1.0f : 0.0f);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, hasContent ? contentTex : overlay);
                    glUniform1i(glGetUniformLocation(prog, "uContentTex"), 0);
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, overlay);
                    glUniform1i(glGetUniformLocation(prog, "uOverlayTex"), 1);
                    const float barVerts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
                    glEnableVertexAttribArray(0);
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, barVerts);
                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, barVerts);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glDisableVertexAttribArray(0);
                    glDisableVertexAttribArray(1);
                }

                if (hasContent)
                    Draw::quad(e, contentTex, (float)pos.x, fs ? (float)pos.y : (float)pos.y + barH, (float)size.x, (float)size.y, alpha,
                               fs ? 0.0f : (float)std::min(round, (int)std::min(size.x, size.y) / 2), sw, sh);
            }

        }

        bool initTitleFont(const char* path) {
            return TitleText::init(path);
        }

        void drawWindows(CGLEngine& e, int32_t screenW, int32_t screenH) {
            for (const auto& window : CCompositor::get().m_windows) {
                if (!window->visible())
                    continue;
                drawWindow(e, *window, screenW, screenH);
            }
        }

        void drawCursor(CGLEngine& e, int32_t screenW, int32_t screenH) {
            auto& cursor = *CCompositor::get().m_cursor;

            if (!TitleText::rasterizeCursor(cursor, s_cursorPixels, s_cursorW, s_cursorH))
                return;

            if (!s_cursorTex)
                s_cursorTex = e.createTexture();
            e.uploadRGBA(s_cursorTex, s_cursorPixels.data(), s_cursorW, s_cursorH, s_cursorW);

            const auto renderPos = cursor.renderPosition();
            const auto hotspot   = cursor.hotspot();
            Draw::quad(e, s_cursorTex, (float)(renderPos.x - hotspot.x), (float)(renderPos.y - hotspot.y), (float)s_cursorW, (float)s_cursorH,
                       cursor.visibilityAlpha(), 0.0f, (float)screenW, (float)screenH);
        }

    }
}
