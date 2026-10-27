// Immediate-mode drawing helpers shared by every scene pass.

#pragma once

#include "GLEngine.hpp"

#include <GLES2/gl2.h>

namespace ColibryShell {
    namespace Draw {

        inline void setFlip(uint32_t prog, CGLEngine& e) {
            glUniform2f(glGetUniformLocation(prog, "uFlip"), e.flipX(), e.flipY());
        }

        inline void quad(CGLEngine& e, uint32_t tex, float x, float y, float w, float h, float alpha, float radius, float screenW, float screenH,
                         float roundTop = 1.0f) {
            const uint32_t prog = e.progQuad();

            glUseProgram(prog);
            glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), x, y);
            glUniform2f(glGetUniformLocation(prog, "uDstSize"), w, h);
            glUniform2f(glGetUniformLocation(prog, "uScreenSize"), screenW, screenH);
            glUniform1f(glGetUniformLocation(prog, "uAlpha"), alpha);
            glUniform1f(glGetUniformLocation(prog, "uRadius"), radius);
            glUniform1f(glGetUniformLocation(prog, "uRoundTop"), roundTop);
            glUniform2f(glGetUniformLocation(prog, "uQuadSize"), w, h);
            glUniform2f(glGetUniformLocation(prog, "uQuadOrigin"), x, y);
            setFlip(prog, e);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(glGetUniformLocation(prog, "uTex"), 0);

            const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
        }

        inline void solid(CGLEngine& e, float r, float g, float b, float a, float x, float y, float w, float h, float radius, float screenW, float screenH) {
            const uint32_t prog = e.progSolid();

            glUseProgram(prog);
            glUniform4f(glGetUniformLocation(prog, "uColor"), r, g, b, a);
            glUniform1f(glGetUniformLocation(prog, "uRadius"), radius);
            glUniform2f(glGetUniformLocation(prog, "uQuadSize"), w, h);
            glUniform2f(glGetUniformLocation(prog, "uQuadOrigin"), x, y);
            glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), x, y);
            glUniform2f(glGetUniformLocation(prog, "uDstSize"), w, h);
            glUniform2f(glGetUniformLocation(prog, "uScreenSize"), screenW, screenH);
            setFlip(prog, e);

            const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
        }

        // Draws an indexed mesh with full chrome context: the signed-
        // distance uniforms describe the logical quad (window box) the
        // mesh deforms, so rounding follows the surface while it flexes.
        inline void mesh(CGLEngine& e, uint32_t prog, uint32_t tex, const float* positions, const float* uvs, int vertexCount, const uint32_t* indices,
                         int indexCount, float alpha, float screenW, float screenH, float radius, float quadX, float quadY, float quadW, float quadH,
                         float roundTop = 1.0f) {
            glUseProgram(prog);
            glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), 0.0f, 0.0f);
            glUniform2f(glGetUniformLocation(prog, "uDstSize"), 1.0f, 1.0f);
            glUniform2f(glGetUniformLocation(prog, "uScreenSize"), screenW, screenH);
            glUniform1f(glGetUniformLocation(prog, "uAlpha"), alpha);
            glUniform1f(glGetUniformLocation(prog, "uRadius"), radius);
            glUniform1f(glGetUniformLocation(prog, "uRoundTop"), roundTop);
            glUniform2f(glGetUniformLocation(prog, "uQuadSize"), quadW, quadH);
            glUniform2f(glGetUniformLocation(prog, "uQuadOrigin"), quadX, quadY);
            setFlip(prog, e);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(glGetUniformLocation(prog, "uTex"), 0);

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs);
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, indices);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
        }

    }
}
