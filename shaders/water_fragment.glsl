#version 330 core

// Water surface shading: per-block tile variety, sun lighting with a specular glint,
// view-angle (fresnel) opacity, distance fog, logarithmic depth (all matching terrain).

in vec3  vWorldRel;   // camera-relative position (camera at origin)
in vec2  vWavePos;    // wave-space XZ; fract() = in-tile UV, floor() = which world block
in vec3  vNormal;     // analytic wave normal from the vertex shader
in float vFlogz;

out vec4 FragColor;

uniform sampler2D texture1;   // shared texture atlas
uniform float tileSize;       // one atlas tile as a fraction of the atlas (1/16)
uniform vec2  tileBase;       // bottom-left of the 2x2 water-tile block in the atlas
uniform float waterAlpha;     // base opacity = 1 - WATER_TRANSPARENCY (constants.h)
uniform float depthBias;      // per-LOD depth nudge; finer rings win ring overlaps

uniform float logFcoef;
uniform vec3  sunDir;
uniform vec3  sunColor;
uniform float ambient;
uniform vec3  fogColor;
uniform float fogStart;
uniform float fogEnd;

void main()
{
    // Each 1x1 world block hashes its coordinates to one of the four water tiles
    // (tileBase + {0,1}x{0,1}), breaking up the repeating pattern at every LOD.
    ivec2 cell = ivec2(floor(vWavePos));
    uint h = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u;
    h ^= h >> 13u; h *= 0x85ebca6bu; h ^= h >> 16u;
    vec2 tile = tileBase + vec2(float(h & 1u), float((h >> 1u) & 1u));

    // Explicit gradients keep mipmapping correct across the fract() seams (as terrain does).
    vec2 uv  = (tile + fract(vWavePos)) * tileSize;
    vec2 dUx = dFdx(vWavePos) * tileSize;
    vec2 dUy = dFdy(vWavePos) * tileSize;
    vec4 tex = textureGrad(texture1, uv, dUx, dUy);

    // Diffuse from the smooth wave normal, plus a sun glint so the surface reads as water.
    vec3  N       = normalize(vNormal);
    float diffuse = max(dot(N, sunDir), 0.0);
    vec3  eye     = -normalize(vWorldRel);                     // fragment -> camera
    float spec    = pow(max(dot(N, normalize(eye + sunDir)), 0.0), 64.0);
    vec3  lit     = tex.rgb * (ambient + diffuse * sunColor) + sunColor * spec * 0.6;

    // Fresnel: water gets more opaque at grazing angles, like real water.
    float fresnel = pow(1.0 - max(dot(N, eye), 0.0), 3.0);
    float alpha   = waterAlpha + (1.0 - waterAlpha) * fresnel * 0.8;

    float dist = length(vWorldRel);
    float fog  = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    FragColor = vec4(mix(lit, fogColor, fog), alpha);

    gl_FragDepth = log2(vFlogz) * (logFcoef * 0.5) + depthBias;
}
