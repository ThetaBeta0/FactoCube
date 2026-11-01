#version 330 core

// Blueprint room: the inside of one big textured box. Same camera-relative /
// logarithmic-depth conventions as the terrain so blocks placed inside it depth-test
// correctly against the walls.

layout (location = 0) in vec3 aPos;       // unit cube, [-0.5, 0.5]
layout (location = 1) in vec2 aTexCoord;  // [0, 1] per face

uniform mat4  view;
uniform mat4  projection;
uniform vec3  centreRelCam;   // box centre relative to the camera
uniform float size;           // box edge length in blocks

out vec2  vUV;                // one unit per block; the fragment fract()s it to tile
out vec3  vWorldRel;
out float vFlogz;

void main()
{
    vec3 worldRel = aPos * size + centreRelCam;
    vWorldRel = worldRel;

    // Leave clip.z as the plain projection: the log depth is written per fragment from
    // vFlogz anyway, and the vertex z only feeds the hardware clipper. The terrain's
    // trick of rewriting clip.z here is fine for 1-block faces but wrong for these
    // 101-block quads — from inside the box some corners of every wall sit behind the
    // camera, and near-plane clipping against a rewritten z cuts holes in the walls.
    gl_Position = projection * view * vec4(worldRel, 1.0);
    vFlogz = 1.0 + gl_Position.w;

    vUV = aTexCoord * size;
}
