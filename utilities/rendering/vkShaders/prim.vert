// vkShaders/prim.vert — task-41 primitive vertex shader (line / triangle /
// outline pipelines share it). Vertex layout matches glBackend's color-prims
// contract: pos(2) + rgb(3), stride 5 floats. uMVP is a push-constant
// UNIFORM block (the per-draw analog of GL's glUniformMatrix4fv(uMVP)):
// composed on the CPU as P*M — model rotate-about-own-origin-then-translate
// (D2 rotator placement, m19) followed by the present transform. Vulkan NDC
// is y-DOWN like logical client space, so P carries NO y-flip (GL needed one).
#version 450

layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aColor;

layout(push_constant) uniform PushBlock
 {mat4 uMVP;
 } pc;

layout(location=0) out vec3 vColor;

void main()
 {gl_Position = pc.uMVP * vec4(aPos, 0.0, 1.0);
  vColor = aColor;
 }
