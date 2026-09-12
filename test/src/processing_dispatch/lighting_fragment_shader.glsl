#version 450
precision highp float;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inFragPos;

// Member order is alphabetical (lightColor, lightDir, viewPos), NOT the more
// natural declaration order -- this MUST match RenderProcessor::ResolveUniforms's
// std::map key-sorted iteration order (processing_processor_render.cpp:922-924,
// "matches SerializeRenderPassConfig()'s own iteration order", DETV-01), which is
// how each uniform's packed bytes land in the push-constant buffer. A declaration
// order that doesn't match the alphabetical packing order silently reads each
// uniform's bytes into the WRONG struct field (Phase 17-06 Rule 1 finding: the
// original lightDir/lightColor/viewPos declaration order caused lightColor's
// bytes to be read as lightDir and vice-versa, producing a solid-black output --
// since lightColor/lightDir happened to be parallel vectors, normalize() erased
// the corruption this fixture's own SECV-01 counter-test needs to detect).
layout(push_constant) uniform Lighting {
    vec3 lightColor;
    vec3 lightDir;      // normalized, world space
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
