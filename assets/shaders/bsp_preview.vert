#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;
layout(location = 4) in vec2 aUV2;

layout(std140, binding = 0) uniform UBO {
  mat4 uMvp;
  mat4 uModel;
  vec4 uLightDir;
  vec4 uFillDir;
  vec4 uAmbient;
  vec4 uTexScaleOffset;
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
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vPos;
layout(location = 4) out vec2 vUV2;

void main() {
  gl_Position = ubo.uMvp * vec4(aPos, 1.0);
  vNormal = (ubo.uModel * vec4(aNormal, 0.0)).xyz;
  vColor = aColor;
  vUV = aUV * ubo.uTexScaleOffset.xy + ubo.uTexScaleOffset.zw;
  vPos = (ubo.uModel * vec4(aPos, 1.0)).xyz;
  vUV2 = aUV2;
}
