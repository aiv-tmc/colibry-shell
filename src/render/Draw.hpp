// Immediate-mode drawing helpers shared by every scene pass.
//
// Each helper draws geometry through the given program with the engine's
// shared vertex shader. `quad` positions a unit quad through uniforms;
// `mesh` feeds client-side vertex buffers (positions in desktop pixels)
// through the same shader with an identity transform, which lets the
// elastic cloth deform windows without any extra shader.

#pragma once

#include "GLEngine.hpp"

#include <GLES2/gl2.h>

namespace ColibryShell {
    namespace Draw {

        // Draws a textured quad with optional signed-distance rounded corners.
        inline void quad(CGLEngine& e, uint32_t tex, float x, float y, float w, float h, float alpha, float radius, float screenW, float screenH) {
            const uint32_t prog = e.progQuad();

            glUseProgram(prog);
            glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), x, y);
            glUniform2f(glGetUniformLocation(prog, "uDstSize"), w, h);
            glUniform2f(glGetUniformLocation(prog, "uScreenSize"), screenW, screenH);
            glUniform1f(glGetUniformLocation(prog, "uAlpha"), alpha);
            glUniform1f(glGetUniformLocation(prog, "uRadius"), radius);
            glUniform2f(glGetUniformLocation(prog, "uQuadSize"), w, h);
            glUniform2f(glGetUniformLocation(prog, "uQuadOrigin"), x, y);

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

        // Draws a solid colored quad with optional rounded corners.
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

            const float verts[] = {0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0};
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
        }

        // Draws an indexed triangle mesh. `positions` holds interleaved
        // pixel coordinates (x, y per vertex), `uvs` interleaved texture
        // coordinates; both are client-side float arrays of `vertexCount`
        // vertices. The program's transform uniforms are set to identity so
        // `positions` land on screen verbatim, which is what the cloth
        // deformation needs.
        inline void mesh(CGLEngine& e, uint32_t prog, uint32_t tex, const float* positions, const float* uvs, int vertexCount, const uint32_t* indices,
                         int indexCount, float alpha, float screenW, float screenH) {
            glUseProgram(prog);
            glUniform2f(glGetUniformLocation(prog, "uDstOrigin"), 0.0f, 0.0f);
            glUniform2f(glGetUniformLocation(prog, "uDstSize"), 1.0f, 1.0f);
            glUniform2f(glGetUniformLocation(prog, "uScreenSize"), screenW, screenH);
            glUniform1f(glGetUniformLocation(prog, "uAlpha"), alpha);
            glUniform1f(glGetUniformLocation(prog, "uRadius"), 0.0f);
            glUniform2f(glGetUniformLocation(prog, "uQuadSize"), screenW, screenH);
            glUniform2f(glGetUniformLocation(prog, "uQuadOrigin"), 0.0f, 0.0f);

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
