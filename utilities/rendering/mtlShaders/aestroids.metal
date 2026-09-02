// aestroids.metal — MSL port of VK SPIR-V shaders for the Metal backend.
// Ported from: vkShaders/prim.vert, prim.frag, tex.vert, tex.frag, masked.frag
// Metal 3 / MSL 3.2 — no doubles, no dynamic allocation, MSAA sample count = 1.
//
// Vertex input contract (two layouts, one struct):
//   Primitives (drawLine/drawPolygon/drawRect):
//     pos(2) + color(3), stride 20 bytes — uv attribute inactive, defaults to 0.
//   Textured (drawTexture/drawTextureMasked):
//     pos(2) + uv(2) + color(4), stride 28 bytes — all attributes active.
//
// Binding convention (matches bridge-side layout):
//   [[stage_in]]  ← vertex descriptor buffer (slot 0)
//   [[buffer(1)]] ← MVP uniform (constant address space; slot 0 is stage_in)
//   [[texture(N)]]← texture slots
//   [[sampler(0)]]← nearest sampler (constexpr, baked into shader)
//
// Vulkan NDC is y-down like Metal NDC — NO y-flip in the vertex shader.

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Vertex input struct — wider layout encompasses both primitive and textured
// formats. Primitive draws leave uv [[attribute(1)]] unbound (defaults to 0).
// When the vertex descriptor provides RGB32Float for color but the struct has
// float4, Metal fills RGB and sets A = 1.0 (implicit conversion).
// ---------------------------------------------------------------------------
struct AestroidsVertIn {
    float2 pos   [[attribute(0)]];
    float2 uv    [[attribute(1)]];
    float4 color [[attribute(2)]];
};

// ---------------------------------------------------------------------------
// Vertex output / fragment input — shared across all pipelines. The rasterizer
// interpolates all fields; each fragment function uses the subset it needs.
// ---------------------------------------------------------------------------
struct AestroidsVertOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

// ---------------------------------------------------------------------------
// Sampler: nearest filtering, clamp-to-edge, normalized coordinates.
// Baked as constexpr — no MTLSamplerState required on the CPU side.
// ---------------------------------------------------------------------------
constexpr sampler texSampler(coord::normalized,
                              address::clamp_to_edge,
                              filter::nearest);

// ===========================================================================
// aestroidsVert — vertex shader shared by all draw calls.
//   MVP matrix at [[buffer(1)]]; stage_in reads from vertex descriptor buffer (slot 0).
//   Writes position, uv, and color for downstream fragment functions.
// ===========================================================================
vertex AestroidsVertOut aestroidsVert(AestroidsVertIn in [[stage_in]],
                                       constant float4x4& uMVP [[buffer(1)]])
{
    AestroidsVertOut out;
    out.position = uMVP * float4(in.pos, 0.0, 1.0);
    out.uv       = in.uv;
    out.color    = in.color;
    return out;
}

// ===========================================================================
// aestroidsFragSolid — opaque vertex color (line / triangle / outline).
//   Alpha forced to 1.0; matches VK prim.frag: vec4(vColor, 1.0).
// ===========================================================================
fragment float4 aestroidsFragSolid(AestroidsVertOut in [[stage_in]])
{
    return float4(in.color.rgb, 1.0);
}

// ===========================================================================
// aestroidsFragTex — textured with per-vertex RGBA tint.
//   Matches VK tex.frag: texture(uTex, vUV) * vColor.
//   drawTexture sets tint = (1,1,1,alpha) so texture color passes through
//   with only the alpha channel modulated.
// ===========================================================================
fragment float4 aestroidsFragTex(AestroidsVertOut in [[stage_in]],
                                  texture2d<float> uTex [[texture(0)]])
{
    return uTex.sample(texSampler, in.uv) * in.color;
}

// ===========================================================================
// aestroidsFragMasked — two textures: content (slot 0) + R8 mask (slot 1).
//   Discard fragment where mask.r < 0.5 — exact analog of VK masked.frag.
//   Vertex color tint is ignored (matches GL maskedProgram_ semantics).
// ===========================================================================
fragment float4 aestroidsFragMasked(AestroidsVertOut in [[stage_in]],
                                     texture2d<float> uContent [[texture(0)]],
                                     texture2d<float> uMask    [[texture(1)]])
{
    if (uMask.sample(texSampler, in.uv).r < 0.5)
        discard_fragment();
    return uContent.sample(texSampler, in.uv);
}
