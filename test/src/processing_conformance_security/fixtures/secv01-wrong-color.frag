#version 450

// SECV-01 counter-test fixture (Phase 12 Plan 02, D-11): deliberately-wrong
// fragment shader for RenderProcessor's SECV-01 wrong-result-still-diverges
// counter-test. Structurally identical to
// processing_conformance_regression/fixtures/passthrough.frag but outputs a
// materially different, still well-formed solid color -- mid-gray
// [0.5, 0.5, 0.5, 1.0] instead of passthrough.frag's solid white
// [1.0, 1.0, 1.0, 1.0] -- standing in for a substituted/corrupted render
// shader constant. Referenced with "type": "glsl" (not "spirv") so it is
// compiled and spirv-val-validated at test runtime by the real
// ShaderCompiler; no precompiled .spv is needed.
// Every pixel is deterministic: RGBA8 (128, 128, 128, 255).

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(0.5, 0.5, 0.5, 1.0);
}
