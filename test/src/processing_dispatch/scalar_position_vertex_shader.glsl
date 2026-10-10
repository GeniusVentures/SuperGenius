#version 450

precision highp float;

layout(location = 0) in float inPosition;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 0.0, 1.0);
    gl_PointSize = 4.0;
}
