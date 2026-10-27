// GLES2 shader sources for the ColibryShell GPU renderer.
//
// Byte-order convention: every texture is uploaded from native 32-bit
// 0xAARRGGBB pixels; GL_RGBA reads them byte-swapped, so the fragment
// shaders swap the channels back. Blending is premultiplied
// (GL_ONE, GL_ONE_MINUS_SRC_ALPHA). Positions are desktop pixels with a
// top-left origin, so gl_FragCoord.y grows downward.

#pragma once

namespace ColibryShell {

    static const char* QUAD_VERT = "attribute vec2 aPos;"
                                   "attribute vec2 aUV;"
                                   "varying vec2 vUV;"
                                   "uniform vec2 uDstOrigin;"
                                   "uniform vec2 uDstSize;"
                                   "uniform vec2 uScreenSize;"
                                   "uniform vec2 uFlip;"
                                   "void main() {"
                                   "  vUV = aUV;"
                                   "  vec2 px = uDstOrigin + aPos * uDstSize;"
                                   "  vec2 ndc = (px / uScreenSize * 2.0 - 1.0) * uFlip;"
                                   "  gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);"
                                   "}";

    // Textured quad with signed-distance rounded corners. uRoundTop = 1
    // rounds all four corners; uRoundTop = 0 rounds only the bottom two
    // (window content: its top edge tucks squarely under the titlebar).
    // Desktop y grows downward, so "bottom half" means the pixels with
    // the larger gl_FragCoord.y.
    static const char* QUAD_FRAG = "precision mediump float;"
                                   "varying vec2 vUV;"
                                   "uniform sampler2D uTex;"
                                   "uniform float uAlpha;"
                                   "uniform float uRadius;"
                                   "uniform float uRoundTop;"
                                   "uniform vec2 uQuadSize;"
                                   "uniform vec2 uQuadOrigin;"
                                   "float roundedDist(vec2 p) {"
                                   "  vec2 b = uQuadSize * 0.5 - vec2(uRadius);"
                                   "  vec2 q = abs(p - uQuadOrigin - uQuadSize * 0.5) - b;"
                                   "  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - uRadius;"
                                   "}"
                                   "float rectDist(vec2 p) {"
                                   "  vec2 q = abs(p - uQuadOrigin - uQuadSize * 0.5) - uQuadSize * 0.5;"
                                   "  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0);"
                                   "}"
                                   "void main() {"
                                   "  vec4 c = texture2D(uTex, vUV);"
                                   "  c.rgb = c.bgr;"
                                   "  float cov = 1.0;"
                                   "  if (uRadius > 0.5) {"
                                   "    float d = roundedDist(gl_FragCoord.xy - vec2(0.5));"
                                   "    if (uRoundTop < 0.5 && gl_FragCoord.y < uQuadOrigin.y + uQuadSize.y * 0.5)"
                                   "      d = rectDist(gl_FragCoord.xy - vec2(0.5));"
                                   "    cov = clamp(0.5 - d, 0.0, 1.0);"
                                   "  }"
                                   "  gl_FragColor = vec4(c.rgb * uAlpha * cov, c.a * uAlpha * cov);"
                                   "}";

    static const char* SOLID_FRAG = "precision mediump float;"
                                    "uniform vec4 uColor;"
                                    "uniform float uRadius;"
                                    "uniform vec2 uQuadSize;"
                                    "uniform vec2 uQuadOrigin;"
                                    "float roundedDist(vec2 p) {"
                                    "  vec2 b = uQuadSize * 0.5 - vec2(uRadius);"
                                    "  vec2 q = abs(p - uQuadOrigin - uQuadSize * 0.5) - b;"
                                    "  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - uRadius;"
                                    "}"
                                    "void main() {"
                                    "  float cov = 1.0;"
                                    "  if (uRadius > 0.5) {"
                                    "    float d = roundedDist(gl_FragCoord.xy - vec2(0.5));"
                                    "    cov = clamp(0.5 - d, 0.0, 1.0);"
                                    "  }"
                                    "  gl_FragColor = vec4(uColor.rgb * uColor.a * cov, uColor.a * cov);"
                                    "}";

    // Titlebar gradient derived from the window's top pixels, composited
    // with the glyph overlay. The top corners (small desktop y) are
    // rounded, the bottom edge stays square to meet the content.
    static const char* TITLEBAR_FRAG =
        "precision mediump float;"
        "varying vec2 vUV;"
        "uniform sampler2D uContentTex;"
        "uniform sampler2D uOverlayTex;"
        "uniform vec4 uFallback;"
        "uniform float uHasContent;"
        "uniform float uRadius;"
        "uniform vec2 uQuadSize;"
        "uniform vec2 uQuadOrigin;"
        "vec3 contentSample(vec2 uv) { return texture2D(uContentTex, uv).bgr; }"
        "float roundedTopDist(vec2 p) {"
        "  vec2 c = uQuadOrigin + uQuadSize * 0.5;"
        "  vec2 half_ = uQuadSize * 0.5;"
        "  vec2 qR = abs(p - c) - (half_ - vec2(uRadius));"
        "  float dRound = length(max(qR, vec2(0.0))) + min(max(qR.x, qR.y), 0.0) - uRadius;"
        "  vec2 qB = abs(p - c) - half_;"
        "  float dRect = length(max(qB, vec2(0.0))) + min(max(qB.x, qB.y), 0.0);"
        "  if (p.y > uQuadOrigin.y + uRadius) return dRect;"
        "  return dRound;"
        "}"
        "void main() {"
        "  vec3 cTop = vec3(0.0);"
        "  vec3 cBot = vec3(0.0);"
        "  for (int i = 0; i < 4; i++) {"
        "    float x = (float(i) + 0.5) / 4.0;"
        "    cTop += contentSample(vec2(x, 0.002));"
        "    cBot += contentSample(vec2(x, 0.06));"
        "    cBot += contentSample(vec2(x, 0.12));"
        "  }"
        "  cTop /= 4.0;"
        "  cBot /= 8.0;"
        "  vec3 base = mix(cTop, cBot, smoothstep(0.0, 1.0, vUV.y));"
        "  base = mix(uFallback.rgb, base, uHasContent);"
        "  vec4 ov = texture2D(uOverlayTex, vUV);"
        "  vec3 col = base * (1.0 - ov.a) + ov.rgb;"
        "  float cov = 1.0;"
        "  if (uRadius > 0.5) {"
        "    float d = roundedTopDist(gl_FragCoord.xy - vec2(0.5));"
        "    cov = clamp(0.5 - d, 0.0, 1.0);"
        "  }"
        "  gl_FragColor = vec4(col * cov, cov);"
        "}";

    static const char* BLUR_DOWN_FRAG = "precision mediump float;"
                                        "varying vec2 vUV;"
                                        "uniform sampler2D uTex;"
                                        "uniform vec2 uTexel;"
                                        "uniform float uOffset;"
                                        "void main() {"
                                        "  vec2 o = uTexel * uOffset;"
                                        "  vec3 c = texture2D(uTex, vUV + vec2(-o.x, -o.y)).rgb"
                                        "         + texture2D(uTex, vUV + vec2( o.x, -o.y)).rgb"
                                        "         + texture2D(uTex, vUV + vec2(-o.x,  o.y)).rgb"
                                        "         + texture2D(uTex, vUV + vec2( o.x,  o.y)).rgb;"
                                        "  gl_FragColor = vec4(c * 0.25, 1.0);"
                                        "}";

    static const char* BLUR_UP_FRAG = "precision mediump float;"
                                      "varying vec2 vUV;"
                                      "uniform sampler2D uTex;"
                                      "uniform vec2 uTexel;"
                                      "uniform float uOffset;"
                                      "void main() {"
                                      "  vec2 o = uTexel * uOffset;"
                                      "  vec3 c = texture2D(uTex, vUV + vec2(-o.x, -o.y)).rgb"
                                      "         + texture2D(uTex, vUV + vec2( o.x, -o.y)).rgb"
                                      "         + texture2D(uTex, vUV + vec2(-o.x,  o.y)).rgb"
                                      "         + texture2D(uTex, vUV + vec2( o.x,  o.y)).rgb"
                                      "         + 2.0 * texture2D(uTex, vUV + vec2(-o.x, 0.0)).rgb"
                                      "         + 2.0 * texture2D(uTex, vUV + vec2( o.x, 0.0)).rgb"
                                      "         + 2.0 * texture2D(uTex, vUV + vec2(0.0, -o.y)).rgb"
                                      "         + 2.0 * texture2D(uTex, vUV + vec2(0.0,  o.y)).rgb;"
                                      "  gl_FragColor = vec4(c / 12.0, 1.0);"
                                      "}";

    static const char* DIM_FRAG = "precision mediump float;"
                                  "uniform vec4 uBox;"
                                  "uniform float uFalloff;"
                                  "uniform float uDark;"
                                  "float sdBox(vec2 p, vec2 c, vec2 b) {"
                                  "  vec2 d = abs(p - c) - b;"
                                  "  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);"
                                  "}"
                                  "void main() {"
                                  "  float d = sdBox(gl_FragCoord.xy - vec2(0.5), uBox.xy + uBox.zw * 0.5, uBox.zw * 0.5);"
                                  "  float a = uDark * clamp(1.0 - d / uFalloff, 0.0, 1.0);"
                                  "  gl_FragColor = vec4(0.0, 0.0, 0.0, a);"
                                  "}";

    static const char* MASK_FRAG = "precision mediump float;"
                                   "uniform vec2 uScreenSize;"
                                   "uniform float uRadius;"
                                   "float roundedDist(vec2 p) {"
                                   "  vec2 half_ = (uScreenSize - 1.0) * 0.5;"
                                   "  vec2 q = abs(p - half_) - (half_ - vec2(uRadius));"
                                   "  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - uRadius;"
                                   "}"
                                   "void main() {"
                                   "  float d = roundedDist(gl_FragCoord.xy - vec2(0.5));"
                                   "  float cov = 1.0;"
                                   "  if (uRadius > 0.5) cov = clamp(0.5 - d, 0.0, 1.0);"
                                   "  gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0 - cov);"
                                   "}";

}
