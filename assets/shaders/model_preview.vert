#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(std140, binding = 0) uniform UBO {
  mat4 uMvp;
  mat4 uModel;
  vec4 uCamPos;
  vec4 uLightDir;
  vec4 uFillDir;
  vec4 uBaseColor;
  vec4 uGroundColor;
  vec4 uShadowCenter;
  vec4 uShadowParams;
  vec4 uGridParams;
  vec4 uGridColor;
  vec4 uAxisColorX;
  vec4 uAxisColorY;
  vec4 uBgTop;
  vec4 uBgBottom;
  vec4 uMisc;
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vPos;

void main() {
  gl_Position = ubo.uMvp * vec4(aPos, 1.0);
  vec4 worldPos = ubo.uModel * vec4(aPos, 1.0);
  vPos = worldPos.xyz;
  vNormal = (ubo.uModel * vec4(aNormal, 0.0)).xyz;
  vUV = aUV;
}
