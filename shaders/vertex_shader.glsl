#version 330 core

// Copy of vertex_shader.glsl used by test.cpp. Adds camera-relative world position +
// logarithmic-depth output for the lit/fogged fragment shader. Original is left untouched.

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

layout (location = 2) in vec4 instanceModel0;
layout (location = 3) in vec4 instanceModel1;
layout (location = 4) in vec4 instanceModel2;
layout (location = 5) in vec4 instanceModel3;
layout (location = 6) in vec2 instanceAtlasOffset;

uniform mat4  view;          // camera rotation only; cmera sits at the origin
uniform mat4  projection;
uniform vec3  chunkOrigin;   // chunk world origin relative to the camera (floating origin)
uniform float logFcoef;      // 2.0 / log2(far + 1.0), for the logarithmic depth buffer

out vec2  vTileCoord;        // tile-space UV (repeats once per voxel); fragment wraps it
flat out vec2 vAtlasOffset;  // atlas tile to sample
out vec3  vWorldRel;         // camera-relative world position (camera at origin) — fog + normal
out float vFlogz;            // 1 + clip.w, used to write logarithmic depth in the fragment

void main()
{
    mat4 model = mat4(instanceModel0, instanceModel1, instanceModel2, instanceModel3);
    vec3 worldRel = (model * vec4(aPos, 1.0)).xyz + chunkOrigin;
    vWorldRel = worldRel;

    vec4 clip = projection * view * vec4(worldRel, 1.0);

    // Logarithmic depth: distributes depth precision logarithmically so a tiny near plane and a
    // huge far plane coexist without z-fighting in the distance. Depth is finished per-fragment.
    vFlogz = 1.0 + clip.w;
    clip.z = (log2(max(1e-6, vFlogz)) * logFcoef - 1.0) * clip.w;
    gl_Position = clip;

    const float TILES_PER_VOXEL = 1.0;   // 1 tile per voxel (crisp near, uniform across LODs)
    vTileCoord   = aTexCoord * TILES_PER_VOXEL;
    vAtlasOffset = instanceAtlasOffset;
}
