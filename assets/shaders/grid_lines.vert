#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

layout(std140, binding = 0) uniform UBO {
  mat4 uMvp;
} ubo;

layout(location = 0) out vec4 vColor;

void main() {
  gl_Position = ubo.uMvp * vec4(aPos, 1.0);
  vColor = aColor;
}

