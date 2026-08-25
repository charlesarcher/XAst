// vkShaders/masked.frag — task-42 masked textured-pipeline fragment shader
// (the D6 VK half). Two combined-image-sampler descriptors: binding 0 =
// content, binding 1 = R8 coverage mask. Fragment discarded where the mask
// is off (<0.5) — the exact analog of glBackend's maskedProgram_
// ("if (texture(uMask, vUV).r < 0.5) discard; FragColor = texture(uContent,
// vUV);"). Vertex stage is the shared tex.vert (stride-8 [x y u v r g b a];
// the tint output is ignored here, matching GL where the masked program
// reads no vertex color either).
#version 450

layout(binding=0) uniform sampler2D uContent;
layout(binding=1) uniform sampler2D uMask;

layout(location=0) in vec2 vUV;
layout(location=1) in vec4 vColor;

layout(location=0) out vec4 FragColor;

void main()
 {if (texture(uMask, vUV).r < 0.5)
    discard;
  FragColor = texture(uContent, vUV);
 }
