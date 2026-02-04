#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;

layout(std140, binding = 0) uniform UBO {
  mat4 uMvp;
  mat4 uModel;
  vec4 uLightDir;
  vec4 uFillDir;
  vec4 uAmbient;
  vec4 uTexScaleOffset;
  vec4 uMisc;
} ubo;

layout(binding = 1) uniform sampler2D uTex;

layout(location = 0) out vec4 fragColor;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

void main() {
  vec3 n = normalize(vNormal);
  float ndl = abs(dot(n, normalize(ubo.uLightDir.xyz)));
  float ndl2 = abs(dot(n, normalize(ubo.uFillDir.xyz)));
  vec3 tex = (ubo.uMisc.y > 0.5) ? texture(uTex, vUV).rgb : vec3(1.0);
  vec3 lm = mix(vec3(1.0), vColor, ubo.uMisc.x);
  vec3 base = toLinear(lm) * toLinear(tex);
  vec3 lit = base * (ubo.uAmbient.xyz + ndl * 0.8 + ndl2 * 0.4);
  lit = min(lit, vec3(1.0));
  fragColor = vec4(toSrgb(lit), 1.0);
}
