#version 450

precision highp float;

layout(location = 0) in float inPosX;
layout(location = 1) in float inPosY;
layout(location = 2) in float inPosZ;
layout(location = 3) in float inNormX;
layout(location = 4) in float inNormY;
layout(location = 5) in float inNormZ;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outFragPos;

void main()
{
    vec3 pos = vec3(inPosX, inPosY, inPosZ);
    vec3 normal = vec3(inNormX, inNormY, inNormZ);

    gl_Position = vec4(pos, 1.0);

    outNormal = normal;
    outFragPos = pos;
}
