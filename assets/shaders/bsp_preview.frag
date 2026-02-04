#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vPos;

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

layout(binding = 1) uniform sampler2D uTex;

layout(location = 0) out vec4 fragColor;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

void main() {
  if (ubo.uMisc.z > 0.5) {
    float t = clamp(vUV.y, 0.0, 1.0);
    vec3 col = mix(ubo.uBgBottom.rgb, ubo.uBgTop.rgb, t);
    fragColor = vec4(col, 1.0);
    return;
  }

  vec3 n = normalize(vNormal);
  float ndl = abs(dot(n, normalize(ubo.uLightDir.xyz)));
  float ndl2 = abs(dot(n, normalize(ubo.uFillDir.xyz)));

  if (ubo.uShadowParams.w > 0.5) {
    if (ubo.uGridParams.x > 0.5) {
      vec3 baseGrid = toLinear(ubo.uGroundColor.rgb);
      float scale = max(ubo.uGridParams.y, 0.001);
      vec2 coord = vPos.xy / scale;
      vec2 cell = abs(fract(coord) - 0.5);
      float line = step(cell.x, 0.02) + step(cell.y, 0.02);
      line = clamp(line, 0.0, 1.0);
      float axisX = step(abs(vPos.x / scale), 0.04);
      float axisY = step(abs(vPos.y / scale), 0.04);
      vec3 col = baseGrid;
      col = mix(col, toLinear(ubo.uGridColor.rgb), line);
      col = mix(col, toLinear(ubo.uAxisColorX.rgb), axisX);
      col = mix(col, toLinear(ubo.uAxisColorY.rgb), axisY);
      fragColor = vec4(toSrgb(col), 1.0);
      return;
    }

    vec3 groundLin = toLinear(ubo.uGroundColor.rgb);
    float gdiff = ndl * 0.5 + ndl2 * 0.2;
    vec3 ground = groundLin * (0.22 + gdiff);

    vec2 delta = vPos.xy - ubo.uShadowCenter.xy;
    float dist = length(delta) / max(0.001, ubo.uShadowParams.x);
    float shadow = exp(-dist * dist * ubo.uShadowParams.z) * ubo.uShadowParams.y;
    shadow = clamp(shadow, 0.0, 0.85);
    ground *= (1.0 - shadow);
    fragColor = vec4(toSrgb(ground), 1.0);
    return;
  }

  vec3 tex = (ubo.uMisc.y > 0.5) ? texture(uTex, vUV).rgb : vec3(1.0);
  vec3 lm = mix(vec3(1.0), vColor, ubo.uMisc.x);
  vec3 base = toLinear(lm) * toLinear(tex);
  vec3 lit = base * (ubo.uAmbient.xyz + ndl * 0.8 + ndl2 * 0.4);
  lit = min(lit, vec3(1.0));
  fragColor = vec4(toSrgb(lit), 1.0);
}
