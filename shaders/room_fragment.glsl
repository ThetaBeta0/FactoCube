#version 330 core

in vec2  vUV;
in vec3  vWorldRel;
in float vFlogz;

out vec4 FragColor;

uniform sampler2D texture1;
uniform vec2  atlasOffset;    // the tile every wall block shows
uniform float tileSize;
uniform float logFcoef;

uniform vec3  sunDir;
uniform vec3  sunColor;
uniform float ambient;

void main()
{
    // Seam-safe atlas tiling, as in the terrain shader.
    vec2 uv  = (atlasOffset + fract(vUV)) * tileSize;
    vec2 dUx = dFdx(vUV) * tileSize;
    vec2 dUy = dFdy(vUV) * tileSize;
    vec4 tex = textureGrad(texture1, uv, dUx, dUy);

    // Flat normal facing the camera (we're inside the box), lit like the terrain so the
    // floor, ceiling and walls read as distinct planes.
    vec3 N = normalize(cross(dFdx(vWorldRel), dFdy(vWorldRel)));
    if (dot(N, vWorldRel) > 0.0) N = -N;
    float diffuse = max(dot(N, sunDir), 0.0);

    FragColor = vec4(tex.rgb * (ambient + diffuse * sunColor), 1.0);
    gl_FragDepth = log2(vFlogz) * (logFcoef * 0.5);
}
