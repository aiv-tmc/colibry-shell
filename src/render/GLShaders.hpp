// GLES2 shader sources for the Colibry GPU renderer.
//
// Every visual primitive of the desktop is drawn by one of these programs:
// textured quads with signed-distance rounded corners (window content,
// wallpaper, cursor, switcher cards), the titlebar gradient that derives
// its colors from the window content texture, the Kawase blur chain used
// by overlays, the selection falloff dim used by the switcher and the
// screen corner mask. All geometry is emitted as a single unit quad per
// primitive; positioning happens in the vertex shader through uniforms.

#pragma once

namespace Colibry {

    // Shared vertex shader: places a unit quad at uDstOrigin/uDstSize in
    // desktop pixel coordinates (top-left origin) and converts to NDC with
    // a Y flip for the GL bottom-left origin. The same unit coordinates
    // serve as texture UVs.
    static const char* QUAD_VERT = "attribute vec2 aPos;"
                                   "attribute vec2 aUV;"
                                   "varying vec2 vUV;"
                                   "uniform vec2 uDstOrigin;"
                                   "uniform vec2 uDstSize;"
                                   "uniform vec2 uScreenSize;"
                                   "void main() {"
                                   "  vUV = aUV;"
                                   "  vec2 px = uDstOrigin + aPos * uDstSize;"
                                   "  vec2 ndc = px / uScreenSize * 2.0 - 1.0;"
                                   "  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);"
                                   "}";

    // Textured quad with anti-aliased rounded corners. The corner radius is
    // evaluated as a signed distance in pixel units, giving true vector
    // rounding with smooth coverage instead of per-pixel clipping.
    static const char* QUAD_FRAG = "precision mediump float;"
                                   "varying vec2 vUV;"
                                   "uniform sampler2D uTex;"
                                   "uniform float uAlpha;"
                                   "uniform float uRadius;"
                                   "uniform vec2 uQuadSize;"
                                   "uniform vec2 uQuadOrigin;"
                                   "float roundedDist(vec2 p) {"
                                   "  vec2 b = uQuadSize * 0.5 - vec2(uRadius);"
                                   "  vec2 q = abs(p - uQuadOrigin - uQuadSize * 0.5) - b;"
                                   "  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - uRadius;"
                                   "}"
                                   "void main() {"
                                   "  vec4 c = texture2D(uTex, vUV);"
                                   "  float cov = 1.0;"
                                   "  if (uRadius > 0.5) {"
                                   "    float d = roundedDist(gl_FragCoord.xy - vec2(0.5));"
                                   "    cov = clamp(0.5 - d, 0.0, 1.0);"
                                   "  }"
                                   "  gl_FragColor = vec4(c.rgb, c.a * uAlpha * cov);"
                                   "}";

    // Solid colored quad with the same signed-distance rounding; used for
    // the window shadow silhouette and flat accent fills.
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
                                    "  gl_FragColor = vec4(uColor.rgb, uColor.a * cov);"
                                    "}";

    // Titlebar: a vertical gradient that visually continues the window.
    // The top of the bar is tinted with the average color of the window's
    // topmost pixel row; the bottom of the bar blends into a horizontally
    // averaged, vertically widened sample of the window's top strip, so the
    // bar reads as a blurred extension of the client content. Text and
    // button glyphs arrive in uOverlayTex with premultiplied alpha.
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
        "    cTop += texture2D(uContentTex, vec2(x, 0.002)).rgb;"
        "    cBot += texture2D(uContentTex, vec2(x, 0.06)).rgb;"
        "    cBot += texture2D(uContentTex, vec2(x, 0.12)).rgb;"
        "  }"
        "  cTop /= 4.0;"
        "  cBot /= 8.0;"
        "  vec3 base = mix(cTop, cBot, smoothstep(0.0, 1.0, vUV.y));"
        "  base = mix(uFallback.rgb, base, uHasContent);"
        "  vec4 ov = texture2D(uOverlayTex, vUV);"
        "  vec3 col = mix(base, ov.rgb, ov.a);"
        "  float cov = 1.0;"
        "  if (uRadius > 0.5) {"
        "    float d = roundedTopDist(gl_FragCoord.xy - vec2(0.5));"
        "    cov = clamp(0.5 - d, 0.0, 1.0);"
        "  }"
        "  gl_FragColor = vec4(col, cov);"
        "}";

    // Kawase downsample: four diagonal taps averaged. Repeated with growing
    // offsets it builds a cheap, wide blur pyramid.
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

    // Kawase upsample: diagonal taps weighted 1, axis taps weighted 2,
    // normalized by 12. Pulling the pyramid back up reconstructs a smooth
    // blur without banding.
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

    // Selection falloff dim: black whose alpha is uDark inside the
    // selection box and decays to zero over uFalloff pixels of signed
    // distance from the box, so the far side of the screen stays untouched.
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

    // Screen corner mask: everything outside the rounded screen rectangle
    // is blended towards pure black, making the corners real, untouchable
    // desktop borders rendered as true quarter-circle arcs.
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
