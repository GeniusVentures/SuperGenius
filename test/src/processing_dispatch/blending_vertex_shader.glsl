#version 450

precision highp float;

layout(location = 0) in float inPosX;
layout(location = 1) in float inPosY;

void main()
{
    gl_Position = vec4(inPosX, inPosY, 0.0, 1.0);
}
