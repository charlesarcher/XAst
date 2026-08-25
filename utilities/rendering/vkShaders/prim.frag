// vkShaders/prim.frag — task-41 primitive fragment shader (line / triangle /
// outline pipelines share it). Opaque vertex color; alpha=1 keeps the
// SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend (enabled pipeline-wide to mirror
// glBackend's global GL_BLEND) inert for primitives.
#version 450

layout(location=0) in vec3 vColor;

layout(location=0) out vec4 FragColor;

void main()
 {FragColor = vec4(vColor, 1.0);
 }
