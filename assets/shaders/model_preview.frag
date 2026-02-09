#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vPos;

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

layout(binding = 1) uniform sampler2D uTex;
layout(binding = 2) uniform sampler2D uGlowTex;

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
  vec4 tex = (ubo.uMisc.x > 0.5) ? texture(uTex, vUV) : vec4(ubo.uBaseColor.rgb, 1.0);
  vec3 base = (ubo.uMisc.x > 0.5) ? tex.rgb : ubo.uBaseColor.rgb;
  vec3 baseLin = toLinear(base);

  float glowMask = 0.0;
  if (ubo.uMisc.y > 0.5) {
    vec4 g = texture(uGlowTex, vUV);
    vec3 gLin = toLinear(g.rgb);
    float gMax = max(max(gLin.r, gLin.g), gLin.b);
    glowMask = clamp(gMax * g.a, 0.0, 1.0);
  }

  vec3 viewDir = normalize(ubo.uCamPos.xyz - vPos);
  vec3 l1 = normalize(ubo.uLightDir.xyz);
  vec3 l2 = normalize(ubo.uFillDir.xyz);

  float ndl1 = max(dot(n, l1), 0.0);
  float ndl2 = max(dot(n, l2), 0.0);
  float diffuse = ndl1 * 0.95 + ndl2 * 0.35;

  vec3 h = normalize(l1 + viewDir);
  float spec = pow(max(dot(n, h), 0.0), 64.0) * 0.28;
  float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 2.0) * 0.18;

  vec3 lit = baseLin * (0.16 + diffuse) + vec3(1.0) * spec + baseLin * rim * 0.15;

  if (ubo.uShadowParams.w > 0.5) {
    if (ubo.uGridParams.x > 0.5) {
      vec3 baseGrid = toLinear(ubo.uGroundColor.rgb);
      float minorScale = max(ubo.uGridParams.y, 0.001);
      float majorScale = minorScale * 10.0;
      vec2 minorCoord = vPos.xy / minorScale;
      vec2 majorCoord = vPos.xy / majorScale;
      vec2 minorCell = abs(fract(minorCoord + 0.5) - 0.5);
      vec2 majorCell = abs(fract(majorCoord + 0.5) - 0.5);
      float minorLine = clamp((0.035 - min(minorCell.x, minorCell.y)) / 0.035, 0.0, 1.0);
      float majorLine = clamp((0.06 - min(majorCell.x, majorCell.y)) / 0.06, 0.0, 1.0);
      float axisX = clamp((0.05 - abs(vPos.x / minorScale)) / 0.05, 0.0, 1.0);
      float axisY = clamp((0.05 - abs(vPos.y / minorScale)) / 0.05, 0.0, 1.0);
      float fade = clamp(1.0 - length(vPos.xy - ubo.uShadowCenter.xy) / max(ubo.uShadowParams.x * 2.2, 1.0), 0.08, 1.0);
      vec3 col = baseGrid;
      col = mix(col, toLinear(ubo.uGridColor.rgb), minorLine * 0.22 * fade);
      col = mix(col, toLinear(ubo.uGridColor.rgb) * 1.35, majorLine * 0.75 * fade);
      col = mix(col, toLinear(ubo.uAxisColorX.rgb), axisX * 0.95);
      col = mix(col, toLinear(ubo.uAxisColorY.rgb), axisY * 0.95);
      fragColor = vec4(toSrgb(col), 1.0);
      return;
    }

    vec3 groundLin = toLinear(ubo.uGroundColor.rgb);
    float gdiff = ndl1 * 0.5 + ndl2 * 0.2;
    vec3 ground = groundLin * (0.22 + gdiff);

    vec2 delta = vPos.xy - ubo.uShadowCenter.xy;
    float dist = length(delta) / max(0.001, ubo.uShadowParams.x);
    float shadow = exp(-dist * dist * ubo.uShadowParams.z) * ubo.uShadowParams.y;
    shadow = clamp(shadow, 0.0, 0.85);
    ground *= (1.0 - shadow);
    fragColor = vec4(toSrgb(ground), 1.0);
    return;
  }

  vec3 finalLin = mix(lit, baseLin, glowMask);
  fragColor = vec4(toSrgb(finalLin), tex.a);
}
