#version 330 core

// Water surface: a flat corner-node grid per chunk, animated here entirely on the GPU.
// The wave field is a sum of sines evaluated in "wave space" — absolute world XZ modulo
// wavePeriod (waveOrigin carries the chunk's world offset) — so neighbouring chunks and
// duplicated corner nodes always compute identical heights: the surface never cracks.

layout (location = 0) in vec3 aData;  // .xy = chunk-local XZ (blocks), .z = shore pin factor

uniform mat4  view;             // camera rotation only (floating origin)
uniform mat4  projection;
uniform vec2  chunkOriginXZ;    // chunk world origin relative to the camera, XZ
uniform float seaLevelRelCam;   // WATER_SURFACE_Y - cameraPos.y (same for every chunk)
uniform float logFcoef;         // logarithmic depth coefficient (matches terrain)
uniform float time;             // seconds; the only per-frame animation input

uniform vec2  waveOrigin;       // chunk world origin XZ mod wavePeriod (seamless phase)
uniform float waveAmplitude;    // peak bob height in blocks (constants.h)
uniform float waveFadeStart;    // waves start shrinking at this camera distance...
uniform float waveFadeEnd;      // ...and are perfectly flat beyond this one
uniform float wavePeriod;       // wave-field repeat length in blocks
uniform float waveSpeed;        // global animation speed multiplier

out vec3  vWorldRel;   // camera-relative position, for fog/lighting
out vec2  vWavePos;    // wave-space XZ, for texture tiling + per-block tile hash
out vec3  vNormal;     // analytic wave normal (smooth, no derivative tricks needed)
out float vFlogz;

// Three superimposed sine waves: a main swell, a broad cross swell, and fine ripples.
// Direction vectors are INTEGER pairs so every component is exactly periodic over
// wavePeriod — edit freely, but keep them integers or chunk seams will show.
const vec2  WAVE_DIR[3]   = vec2[3](vec2(21.0, 9.0), vec2(-7.0, 12.0), vec2(34.0, -25.0));
const float WAVE_AMP[3]   = float[3](0.50, 0.30, 0.20);   // relative amplitudes, sum = 1
const float WAVE_SPEED[3] = float[3](1.1, 1.7, 2.6);      // rad/s per component

void main()
{
    vec2 wavePos = waveOrigin + aData.xy;
    vWavePos = wavePos;

    // Distance fade: full waves near the camera, dead flat past waveFadeEnd (which keeps
    // overlapping LOD rings coplanar). Shore pin: nodes touching land never move.
    vec3 flatRel = vec3(aData.x + chunkOriginXZ.x, seaLevelRelCam, aData.y + chunkOriginXZ.y);
    float fade = 1.0 - smoothstep(waveFadeStart, waveFadeEnd, length(flatRel));
    float amp  = waveAmplitude * aData.z * fade;

    float height = 0.0;
    vec2  slope  = vec2(0.0);
    float t = time * waveSpeed;
    for (int i = 0; i < 3; ++i) {
        vec2  k     = (6.28318530718 / wavePeriod) * WAVE_DIR[i];
        float phase = dot(k, wavePos) + WAVE_SPEED[i] * t;
        height += WAVE_AMP[i] * sin(phase);
        slope  += WAVE_AMP[i] * cos(phase) * k;
    }

    vWorldRel = flatRel + vec3(0.0, amp * height, 0.0);
    vNormal   = normalize(vec3(-amp * slope.x, 1.0, -amp * slope.y));

    vec4 clip = projection * view * vec4(vWorldRel, 1.0);
    vFlogz = 1.0 + clip.w;
    clip.z = (log2(max(1e-6, vFlogz)) * logFcoef - 1.0) * clip.w;
    gl_Position = clip;
}
