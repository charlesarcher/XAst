// vkShaders/tex.vert — task-41 textured-pipeline vertex shader. Vertex
// layout = glBackend's drawTriangles contract: pos(2) + uv(2) + rgba(4),
// stride 7 floats (drawTexture synthesizes the tint). uMVP push-constant
// uniform block, same composition as prim.vert.
#version 450

layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;

layout(push_constant) uniform PushBlock
 {mat4 uMVP;
 } pc;

layout(location=0) out vec2 vUV;
layout(location=1) out vec4 vColor;

void main()
 {gl_Position = pc.uMVP * vec4(aPos, 0.0, 1.0);
  vUV = aUV;
  vColor = aColor;
 }
