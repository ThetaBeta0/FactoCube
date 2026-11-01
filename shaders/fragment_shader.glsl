#version 330 core

// Copy of fragment_shader.glsl used by test.cpp. Adds directional (sun) lighting, distance
// fog, and a logarithmic depth write. Original is left untouched.

in vec2  vTileCoord;
flat in vec2 vAtlasOffset;
in vec3  vWorldRel;     // camera-relative world position (camera at origin)
in float vFlogz;        // 1 + clip.w from the vertex shader

out vec4 FragColor;

uniform sampler2D texture1;   // texture atlas
uniform float tileSize;       // size of one atlas tile (fraction of the atlas)
uniform float logFcoef;       // 2.0 / log2(far + 1.0)
uniform float depthBias;      // per-chunk depth nudge (coarser LODs pushed back; replaces glPolygonOffset)

uniform vec3  sunDir;         // direction TO the sun (normalized, world space)
uniform vec3  sunColor;       // sun diffuse contribution
uniform float ambient;        // ambient light floor so shaded faces aren't black

uniform vec3  fogColor;       // distance fog / sky colour
uniform float fogStart;       // distance where fog begins (world blocks)
uniform float fogEnd;         // distance where fog is full

void main()
{
    // Atlas tiling: wrap into a single tile, sample with explicit derivatives so mipmapping
    // stays correct across the fract() seams.
    vec2 uv  = (vAtlasOffset + fract(vTileCoord)) * tileSize;
    vec2 dUx = dFdx(vTileCoord) * tileSize;
    vec2 dUy = dFdy(vTileCoord) * tileSize;
    vec4 tex = textureGrad(texture1, uv, dUx, dUy);

    // Flat per-face normal from screen-space derivatives of the world position. We only render
    // front faces, so flip it to face the camera (camera is at the origin, so the view vector to
    // this fragment is vWorldRel).
    vec3 N = normalize(cross(dFdx(vWorldRel), dFdy(vWorldRel)));
    if (dot(N, vWorldRel) > 0.0) N = -N;

    float diffuse = max(dot(N, sunDir), 0.0);
    vec3  lit     = tex.rgb * (ambient + diffuse * sunColor);

    // Linear distance fog toward the sky colour.
    float dist = length(vWorldRel);
    float fog  = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    vec3  color = mix(lit, fogColor, fog);

    FragColor = vec4(color, tex.a);

    // Finish the logarithmic depth here (plus the per-LOD bias that keeps coarser rings behind
    // finer ones where they overlap — manual depth writes ignore glPolygonOffset).
    gl_FragDepth = log2(vFlogz) * (logFcoef * 0.5) + depthBias;
}
