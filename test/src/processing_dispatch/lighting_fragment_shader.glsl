#version 450
precision highp float;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inFragPos;

layout(push_constant) uniform Lighting {
    vec3 lightDir;      // normalized, world space
    vec3 lightColor;
    vec3 viewPos;
} u;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 N = normalize(inNormal);
    vec3 L = normalize(-u.lightDir);
    float diff = max(dot(N, L), 0.0);

    vec3 V = normalize(u.viewPos - inFragPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 32.0);   // real transcendental FP work (pow)

    vec3 result = (diff + spec) * u.lightColor;
    outColor = vec4(result, 1.0);
}
