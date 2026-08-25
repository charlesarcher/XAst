// vkShaders/tex.frag — task-41 textured-pipeline fragment shader. One
// combined-image-sampler descriptor (set 0, binding 0); output = texture *
// per-vertex RGBA tint (glBackend triProgram_ semantics; drawTexture rides
// it with tint=(1,1,1,alpha)).
#version 450

layout(binding=0) uniform sampler2D uTex;

layout(location=0) in vec2 vUV;
layout(location=1) in vec4 vColor;

layout(location=0) out vec4 FragColor;

void main()
 {FragColor = texture(uTex, vUV) * vColor;
 }
