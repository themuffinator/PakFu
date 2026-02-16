#include "ui/model_viewer_widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <QMatrix4x4>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QPalette>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QDebug>
#include <QtGui/QOpenGLContext>
#include <QtOpenGL/QOpenGLFunctions_1_1>

#include "formats/image_loader.h"
#include "formats/model.h"
#include "formats/quake3_skin.h"

namespace {
QVector3D spherical_dir(float yaw_deg, float pitch_deg) {
  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = yaw_deg * kPi / 180.0f;
  const float pitch = pitch_deg * kPi / 180.0f;
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  return QVector3D(cp * cy, cp * sy, sp);
}

constexpr float kOrbitSensitivityDegPerPixel = 0.45f;

float orbit_min_distance(float radius) {
  return std::max(0.01f, radius * 0.001f);
}

float orbit_max_distance(float radius) {
  const float min_dist = orbit_min_distance(radius);
  return std::max(min_dist * 2.0f, std::max(radius, 1.0f) * 500.0f);
}

QVector3D safe_right_from_forward(const QVector3D& forward) {
  QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 0.0f, 1.0f));
  if (right.lengthSquared() < 1e-6f) {
    right = QVector3D(1.0f, 0.0f, 0.0f);
  } else {
    right.normalize();
  }
  return right;
}

float fit_distance_for_aabb(const QVector3D& half_extents,
                            const QVector3D& view_forward,
                            float aspect,
                            float fov_y_deg) {
  constexpr float kPi = 3.14159265358979323846f;
  const QVector3D safe_half(std::max(half_extents.x(), 0.001f),
                            std::max(half_extents.y(), 0.001f),
                            std::max(half_extents.z(), 0.001f));
  const float safe_aspect = std::max(aspect, 0.01f);
  const float fov_y = fov_y_deg * kPi / 180.0f;
  const float tan_half_y = std::tan(fov_y * 0.5f);
  const float tan_half_x = std::max(0.001f, tan_half_y * safe_aspect);
  const float safe_tan_half_y = std::max(0.001f, tan_half_y);

  const QVector3D fwd = view_forward.normalized();
  const QVector3D right = safe_right_from_forward(fwd);
  const QVector3D up = QVector3D::crossProduct(right, fwd).normalized();

  const auto projected_radius = [&](const QVector3D& axis) -> float {
    return std::abs(axis.x()) * safe_half.x() +
           std::abs(axis.y()) * safe_half.y() +
           std::abs(axis.z()) * safe_half.z();
  };

  const float radius_x = projected_radius(right);
  const float radius_y = projected_radius(up);
  const float radius_z = projected_radius(fwd);
  const float dist_x = radius_x / tan_half_x;
  const float dist_y = radius_y / safe_tan_half_y;
  return radius_z + std::max(dist_x, dist_y);
}

void apply_orbit_zoom(float factor,
                      float min_dist,
                      float max_dist,
                      float* distance,
                      QVector3D* center,
                      float yaw_deg,
                      float pitch_deg) {
  if (!distance || !center) {
    return;
  }
  const float safe_factor = std::clamp(factor, 0.01f, 100.0f);
  const float target_distance = (*distance) * safe_factor;
  if (target_distance < min_dist) {
    const float push = min_dist - target_distance;
    if (push > 0.0f) {
      const QVector3D forward = (-spherical_dir(yaw_deg, pitch_deg)).normalized();
      *center += forward * push;
    }
    *distance = min_dist;
    return;
  }
  *distance = std::clamp(target_distance, min_dist, max_dist);
}

float quantized_grid_scale(float reference_distance) {
  const float target = std::max(reference_distance / 16.0f, 1.0f);
  const float exponent = std::floor(std::log10(target));
  const float base = std::pow(10.0f, exponent);
  const float n = target / std::max(base, 1e-6f);
  float step = base;
  if (n >= 5.0f) {
    step = 5.0f * base;
  } else if (n >= 2.0f) {
    step = 2.0f * base;
  }
  return std::max(step, 1.0f);
}

QString vertex_shader_source(const QSurfaceFormat& fmt) {
  if (QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES()) {
    return R"GLSL(
      attribute highp vec3 aPos;
      attribute highp vec3 aNormal;
      attribute highp vec2 aUV;
      uniform highp mat4 uMvp;
      uniform highp mat4 uModel;
      varying highp vec3 vNormal;
      varying highp vec2 vUV;
      varying highp vec3 vPos;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vPos = (uModel * vec4(aPos, 1.0)).xyz;
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vUV = aUV;
      }
    )GLSL";
  }

  const int major = fmt.majorVersion();
  const int minor = fmt.minorVersion();
  const bool glsl_330 = (major > 3) || (major == 3 && minor >= 3);
  const bool glsl_130 = major >= 3;

  if (glsl_330) {
    return R"GLSL(
      #version 330 core
      layout(location = 0) in vec3 aPos;
      layout(location = 1) in vec3 aNormal;
      layout(location = 2) in vec2 aUV;
      uniform mat4 uMvp;
      uniform mat4 uModel;
      out vec3 vNormal;
      out vec2 vUV;
      out vec3 vPos;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vPos = (uModel * vec4(aPos, 1.0)).xyz;
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vUV = aUV;
      }
    )GLSL";
  }

  if (glsl_130) {
    return R"GLSL(
      #version 130
      in vec3 aPos;
      in vec3 aNormal;
      in vec2 aUV;
      uniform mat4 uMvp;
      uniform mat4 uModel;
      out vec3 vNormal;
      out vec2 vUV;
      out vec3 vPos;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vPos = (uModel * vec4(aPos, 1.0)).xyz;
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vUV = aUV;
      }
    )GLSL";
  }

  return R"GLSL(
    #version 120
    attribute vec3 aPos;
    attribute vec3 aNormal;
    attribute vec2 aUV;
    uniform mat4 uMvp;
    uniform mat4 uModel;
    varying vec3 vNormal;
    varying vec2 vUV;
    varying vec3 vPos;
    void main() {
      gl_Position = uMvp * vec4(aPos, 1.0);
      vPos = (uModel * vec4(aPos, 1.0)).xyz;
      vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
      vUV = aUV;
    }
  )GLSL";
}

QString fragment_shader_source(const QSurfaceFormat& fmt) {
  if (QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES()) {
    return R"GLSL(
      precision mediump float;
      varying mediump vec3 vNormal;
      varying mediump vec2 vUV;
      varying mediump vec3 vPos;
      uniform mediump vec3 uCamPos;
      uniform mediump vec3 uLightDir;
      uniform mediump vec3 uFillDir;
      uniform mediump vec3 uBaseColor;
      uniform mediump vec3 uGroundColor;
      uniform mediump vec3 uShadowCenter;
      uniform mediump float uShadowRadius;
      uniform mediump float uShadowStrength;
      uniform mediump float uShadowSoftness;
      uniform mediump float uIsGround;
      uniform mediump float uGridMode;
      uniform mediump float uGridScale;
      uniform mediump vec3 uGridColor;
      uniform mediump vec3 uAxisColorX;
      uniform mediump vec3 uAxisColorY;
      uniform mediump float uIsBackground;
      uniform mediump vec3 uBgTop;
      uniform mediump vec3 uBgBottom;
      uniform sampler2D uTex;
      uniform sampler2D uGlowTex;
      uniform int uHasTex;
      uniform int uHasGlow;
      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }
      void main() {
        if (uIsBackground > 0.5) {
          float t = clamp(vUV.y, 0.0, 1.0);
          vec3 col = mix(uBgBottom, uBgTop, t);
          gl_FragColor = vec4(col, 1.0);
          return;
        }
        vec3 n = normalize(vNormal);
        vec4 tex = (uHasTex != 0) ? texture2D(uTex, vUV) : vec4(uBaseColor, 1.0);
        vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
        vec3 baseLin = toLinear(base);

        float glowMask = 0.0;
        if (uHasGlow != 0) {
          vec4 g = texture2D(uGlowTex, vUV);
          vec3 gLin = toLinear(g.rgb);
          float gMax = max(max(gLin.r, gLin.g), gLin.b);
          glowMask = clamp(gMax * g.a, 0.0, 1.0);
        }

        vec3 viewDir = normalize(uCamPos - vPos);
        vec3 l1 = normalize(uLightDir);
        vec3 l2 = normalize(uFillDir);

        float ndl1 = max(dot(n, l1), 0.0);
        float ndl2 = max(dot(n, l2), 0.0);
        float diffuse = ndl1 * 0.95 + ndl2 * 0.35;

        vec3 h = normalize(l1 + viewDir);
        float spec = pow(max(dot(n, h), 0.0), 64.0) * 0.28;
        float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 2.0) * 0.18;

        vec3 lit = baseLin * (0.16 + diffuse) + vec3(1.0) * spec + baseLin * rim * 0.15;

        if (uIsGround > 0.5) {
          if (uGridMode > 0.5) {
            vec3 baseGrid = toLinear(uGroundColor);
            float minorScale = max(uGridScale, 0.001);
            float majorScale = minorScale * 10.0;
            vec2 minorCoord = vPos.xy / minorScale;
            vec2 majorCoord = vPos.xy / majorScale;
            vec2 minorCell = abs(fract(minorCoord + 0.5) - 0.5);
            vec2 majorCell = abs(fract(majorCoord + 0.5) - 0.5);
            float minorLine = clamp((0.035 - min(minorCell.x, minorCell.y)) / 0.035, 0.0, 1.0);
            float majorLine = clamp((0.06 - min(majorCell.x, majorCell.y)) / 0.06, 0.0, 1.0);
            float axisX = clamp((0.05 - abs(vPos.x / minorScale)) / 0.05, 0.0, 1.0);
            float axisY = clamp((0.05 - abs(vPos.y / minorScale)) / 0.05, 0.0, 1.0);
            float fade = clamp(1.0 - length(vPos.xy - uShadowCenter.xy) / max(uShadowRadius * 2.2, 1.0), 0.08, 1.0);
            vec3 col = baseGrid;
            col = mix(col, toLinear(uGridColor), minorLine * 0.22 * fade);
            col = mix(col, toLinear(uGridColor) * 1.35, majorLine * 0.75 * fade);
            col = mix(col, toLinear(uAxisColorX), axisX * 0.95);
            col = mix(col, toLinear(uAxisColorY), axisY * 0.95);
            gl_FragColor = vec4(toSrgb(col), 1.0);
            return;
          }

          vec3 groundLin = toLinear(uGroundColor);
          float gdiff = ndl1 * 0.5 + ndl2 * 0.2;
          vec3 ground = groundLin * (0.22 + gdiff);

          vec2 delta = vPos.xy - uShadowCenter.xy;
          float dist = length(delta) / max(0.001, uShadowRadius);
          float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
          shadow = clamp(shadow, 0.0, 0.85);
          ground *= (1.0 - shadow);
          gl_FragColor = vec4(toSrgb(ground), 1.0);
          return;
        }

        vec3 finalLin = mix(lit, baseLin, glowMask);
        gl_FragColor = vec4(toSrgb(finalLin), tex.a);
      }
    )GLSL";
  }

  const int major = fmt.majorVersion();
  const int minor = fmt.minorVersion();
  const bool glsl_330 = (major > 3) || (major == 3 && minor >= 3);
  const bool glsl_130 = major >= 3;

  if (glsl_330) {
    return R"GLSL(
      #version 330 core
      in vec3 vNormal;
      in vec2 vUV;
      in vec3 vPos;
      uniform vec3 uCamPos;
      uniform vec3 uLightDir;
      uniform vec3 uFillDir;
      uniform vec3 uBaseColor;
      uniform vec3 uGroundColor;
      uniform vec3 uShadowCenter;
      uniform float uShadowRadius;
      uniform float uShadowStrength;
      uniform float uShadowSoftness;
      uniform float uIsGround;
      uniform float uGridMode;
      uniform float uGridScale;
      uniform vec3 uGridColor;
      uniform vec3 uAxisColorX;
      uniform vec3 uAxisColorY;
      uniform float uIsBackground;
      uniform vec3 uBgTop;
      uniform vec3 uBgBottom;
      uniform sampler2D uTex;
      uniform sampler2D uGlowTex;
      uniform int uHasTex;
      uniform int uHasGlow;
      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }
      out vec4 FragColor;
      void main() {
        if (uIsBackground > 0.5) {
          float t = clamp(vUV.y, 0.0, 1.0);
          vec3 col = mix(uBgBottom, uBgTop, t);
          FragColor = vec4(col, 1.0);
          return;
        }
        vec3 n = normalize(vNormal);
        vec4 tex = (uHasTex != 0) ? texture(uTex, vUV) : vec4(uBaseColor, 1.0);
        vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
        vec3 baseLin = toLinear(base);

        float glowMask = 0.0;
        if (uHasGlow != 0) {
          vec4 g = texture(uGlowTex, vUV);
          vec3 gLin = toLinear(g.rgb);
          float gMax = max(max(gLin.r, gLin.g), gLin.b);
          glowMask = clamp(gMax * g.a, 0.0, 1.0);
        }

        vec3 viewDir = normalize(uCamPos - vPos);
        vec3 l1 = normalize(uLightDir);
        vec3 l2 = normalize(uFillDir);

        float ndl1 = max(dot(n, l1), 0.0);
        float ndl2 = max(dot(n, l2), 0.0);
        float diffuse = ndl1 * 0.95 + ndl2 * 0.35;

        vec3 h = normalize(l1 + viewDir);
        float spec = pow(max(dot(n, h), 0.0), 64.0) * 0.28;
        float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 2.0) * 0.18;

        vec3 lit = baseLin * (0.16 + diffuse) + vec3(1.0) * spec + baseLin * rim * 0.15;

        if (uIsGround > 0.5) {
          if (uGridMode > 0.5) {
            vec3 baseGrid = toLinear(uGroundColor);
            float minorScale = max(uGridScale, 0.001);
            float majorScale = minorScale * 10.0;
            vec2 minorCoord = vPos.xy / minorScale;
            vec2 majorCoord = vPos.xy / majorScale;
            vec2 minorCell = abs(fract(minorCoord + 0.5) - 0.5);
            vec2 majorCell = abs(fract(majorCoord + 0.5) - 0.5);
            float minorLine = clamp((0.035 - min(minorCell.x, minorCell.y)) / 0.035, 0.0, 1.0);
            float majorLine = clamp((0.06 - min(majorCell.x, majorCell.y)) / 0.06, 0.0, 1.0);
            float axisX = clamp((0.05 - abs(vPos.x / minorScale)) / 0.05, 0.0, 1.0);
            float axisY = clamp((0.05 - abs(vPos.y / minorScale)) / 0.05, 0.0, 1.0);
            float fade = clamp(1.0 - length(vPos.xy - uShadowCenter.xy) / max(uShadowRadius * 2.2, 1.0), 0.08, 1.0);
            vec3 col = baseGrid;
            col = mix(col, toLinear(uGridColor), minorLine * 0.22 * fade);
            col = mix(col, toLinear(uGridColor) * 1.35, majorLine * 0.75 * fade);
            col = mix(col, toLinear(uAxisColorX), axisX * 0.95);
            col = mix(col, toLinear(uAxisColorY), axisY * 0.95);
            FragColor = vec4(toSrgb(col), 1.0);
            return;
          }

          vec3 groundLin = toLinear(uGroundColor);
          float gdiff = ndl1 * 0.5 + ndl2 * 0.2;
          vec3 ground = groundLin * (0.22 + gdiff);

          vec2 delta = vPos.xy - uShadowCenter.xy;
          float dist = length(delta) / max(0.001, uShadowRadius);
          float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
          shadow = clamp(shadow, 0.0, 0.85);
          ground *= (1.0 - shadow);
          FragColor = vec4(toSrgb(ground), 1.0);
          return;
        }

        vec3 finalLin = mix(lit, baseLin, glowMask);
        FragColor = vec4(toSrgb(finalLin), tex.a);
      }
    )GLSL";
  }

  if (glsl_130) {
    return R"GLSL(
      #version 130
      in vec3 vNormal;
      in vec2 vUV;
      in vec3 vPos;
      uniform vec3 uCamPos;
      uniform vec3 uLightDir;
      uniform vec3 uFillDir;
      uniform vec3 uBaseColor;
      uniform vec3 uGroundColor;
      uniform vec3 uShadowCenter;
      uniform float uShadowRadius;
      uniform float uShadowStrength;
      uniform float uShadowSoftness;
      uniform float uIsGround;
      uniform float uGridMode;
      uniform float uGridScale;
      uniform vec3 uGridColor;
      uniform vec3 uAxisColorX;
      uniform vec3 uAxisColorY;
      uniform float uIsBackground;
      uniform vec3 uBgTop;
      uniform vec3 uBgBottom;
      uniform sampler2D uTex;
      uniform sampler2D uGlowTex;
      uniform int uHasTex;
      uniform int uHasGlow;
      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }
      out vec4 FragColor;
      void main() {
        if (uIsBackground > 0.5) {
          float t = clamp(vUV.y, 0.0, 1.0);
          vec3 col = mix(uBgBottom, uBgTop, t);
          FragColor = vec4(col, 1.0);
          return;
        }
        vec3 n = normalize(vNormal);
        vec4 tex = (uHasTex != 0) ? texture2D(uTex, vUV) : vec4(uBaseColor, 1.0);
        vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
        vec3 baseLin = toLinear(base);

        float glowMask = 0.0;
        if (uHasGlow != 0) {
          vec4 g = texture2D(uGlowTex, vUV);
          vec3 gLin = toLinear(g.rgb);
          float gMax = max(max(gLin.r, gLin.g), gLin.b);
          glowMask = clamp(gMax * g.a, 0.0, 1.0);
        }

        vec3 viewDir = normalize(uCamPos - vPos);
        vec3 l1 = normalize(uLightDir);
        vec3 l2 = normalize(uFillDir);

        float ndl1 = max(dot(n, l1), 0.0);
        float ndl2 = max(dot(n, l2), 0.0);
        float diffuse = ndl1 * 0.95 + ndl2 * 0.35;

        vec3 h = normalize(l1 + viewDir);
        float spec = pow(max(dot(n, h), 0.0), 64.0) * 0.28;
        float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 2.0) * 0.18;

        vec3 lit = baseLin * (0.16 + diffuse) + vec3(1.0) * spec + baseLin * rim * 0.15;

        if (uIsGround > 0.5) {
          if (uGridMode > 0.5) {
            vec3 baseGrid = toLinear(uGroundColor);
            float minorScale = max(uGridScale, 0.001);
            float majorScale = minorScale * 10.0;
            vec2 minorCoord = vPos.xy / minorScale;
            vec2 majorCoord = vPos.xy / majorScale;
            vec2 minorCell = abs(fract(minorCoord + 0.5) - 0.5);
            vec2 majorCell = abs(fract(majorCoord + 0.5) - 0.5);
            float minorLine = clamp((0.035 - min(minorCell.x, minorCell.y)) / 0.035, 0.0, 1.0);
            float majorLine = clamp((0.06 - min(majorCell.x, majorCell.y)) / 0.06, 0.0, 1.0);
            float axisX = clamp((0.05 - abs(vPos.x / minorScale)) / 0.05, 0.0, 1.0);
            float axisY = clamp((0.05 - abs(vPos.y / minorScale)) / 0.05, 0.0, 1.0);
            float fade = clamp(1.0 - length(vPos.xy - uShadowCenter.xy) / max(uShadowRadius * 2.2, 1.0), 0.08, 1.0);
            vec3 col = baseGrid;
            col = mix(col, toLinear(uGridColor), minorLine * 0.22 * fade);
            col = mix(col, toLinear(uGridColor) * 1.35, majorLine * 0.75 * fade);
            col = mix(col, toLinear(uAxisColorX), axisX * 0.95);
            col = mix(col, toLinear(uAxisColorY), axisY * 0.95);
            FragColor = vec4(toSrgb(col), 1.0);
            return;
          }

          vec3 groundLin = toLinear(uGroundColor);
          float gdiff = ndl1 * 0.5 + ndl2 * 0.2;
          vec3 ground = groundLin * (0.22 + gdiff);

          vec2 delta = vPos.xy - uShadowCenter.xy;
          float dist = length(delta) / max(0.001, uShadowRadius);
          float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
          shadow = clamp(shadow, 0.0, 0.85);
          ground *= (1.0 - shadow);
          FragColor = vec4(toSrgb(ground), 1.0);
          return;
        }

        vec3 finalLin = mix(lit, baseLin, glowMask);
        FragColor = vec4(toSrgb(finalLin), tex.a);
      }
    )GLSL";
  }

  return R"GLSL(
    #version 120
    varying vec3 vNormal;
    varying vec2 vUV;
    varying vec3 vPos;
    uniform vec3 uCamPos;
    uniform vec3 uLightDir;
    uniform vec3 uFillDir;
    uniform vec3 uBaseColor;
    uniform vec3 uGroundColor;
    uniform vec3 uShadowCenter;
    uniform float uShadowRadius;
    uniform float uShadowStrength;
    uniform float uShadowSoftness;
    uniform float uIsGround;
    uniform float uGridMode;
    uniform float uGridScale;
    uniform vec3 uGridColor;
    uniform vec3 uAxisColorX;
    uniform vec3 uAxisColorY;
    uniform float uIsBackground;
    uniform vec3 uBgTop;
    uniform vec3 uBgBottom;
    uniform sampler2D uTex;
    uniform sampler2D uGlowTex;
    uniform int uHasTex;
    uniform int uHasGlow;
    vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
    vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }
    void main() {
      if (uIsBackground > 0.5) {
        float t = clamp(vUV.y, 0.0, 1.0);
        vec3 col = mix(uBgBottom, uBgTop, t);
        gl_FragColor = vec4(col, 1.0);
        return;
      }
      vec3 n = normalize(vNormal);
      vec4 tex = (uHasTex != 0) ? texture2D(uTex, vUV) : vec4(uBaseColor, 1.0);
      vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
      vec3 baseLin = toLinear(base);

      float glowMask = 0.0;
      if (uHasGlow != 0) {
        vec4 g = texture2D(uGlowTex, vUV);
        vec3 gLin = toLinear(g.rgb);
        float gMax = max(max(gLin.r, gLin.g), gLin.b);
        glowMask = clamp(gMax * g.a, 0.0, 1.0);
      }

      vec3 viewDir = normalize(uCamPos - vPos);
      vec3 l1 = normalize(uLightDir);
      vec3 l2 = normalize(uFillDir);

      float ndl1 = max(dot(n, l1), 0.0);
      float ndl2 = max(dot(n, l2), 0.0);
      float diffuse = ndl1 * 0.95 + ndl2 * 0.35;

      vec3 h = normalize(l1 + viewDir);
      float spec = pow(max(dot(n, h), 0.0), 64.0) * 0.28;
      float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 2.0) * 0.18;

      vec3 lit = baseLin * (0.16 + diffuse) + vec3(1.0) * spec + baseLin * rim * 0.15;

      if (uIsGround > 0.5) {
        if (uGridMode > 0.5) {
          vec3 baseGrid = toLinear(uGroundColor);
          float minorScale = max(uGridScale, 0.001);
          float majorScale = minorScale * 10.0;
          vec2 minorCoord = vPos.xy / minorScale;
          vec2 majorCoord = vPos.xy / majorScale;
          vec2 minorCell = abs(fract(minorCoord + 0.5) - 0.5);
          vec2 majorCell = abs(fract(majorCoord + 0.5) - 0.5);
          float minorLine = clamp((0.035 - min(minorCell.x, minorCell.y)) / 0.035, 0.0, 1.0);
          float majorLine = clamp((0.06 - min(majorCell.x, majorCell.y)) / 0.06, 0.0, 1.0);
          float axisX = clamp((0.05 - abs(vPos.x / minorScale)) / 0.05, 0.0, 1.0);
          float axisY = clamp((0.05 - abs(vPos.y / minorScale)) / 0.05, 0.0, 1.0);
          float fade = clamp(1.0 - length(vPos.xy - uShadowCenter.xy) / max(uShadowRadius * 2.2, 1.0), 0.08, 1.0);
          vec3 col = baseGrid;
          col = mix(col, toLinear(uGridColor), minorLine * 0.22 * fade);
          col = mix(col, toLinear(uGridColor) * 1.35, majorLine * 0.75 * fade);
          col = mix(col, toLinear(uAxisColorX), axisX * 0.95);
          col = mix(col, toLinear(uAxisColorY), axisY * 0.95);
          gl_FragColor = vec4(toSrgb(col), 1.0);
          return;
        }

        vec3 groundLin = toLinear(uGroundColor);
        float gdiff = ndl1 * 0.5 + ndl2 * 0.2;
        vec3 ground = groundLin * (0.22 + gdiff);

        vec2 delta = vPos.xy - uShadowCenter.xy;
        float dist = length(delta) / max(0.001, uShadowRadius);
        float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
        shadow = clamp(shadow, 0.0, 0.85);
        ground *= (1.0 - shadow);
        gl_FragColor = vec4(toSrgb(ground), 1.0);
        return;
      }

      vec3 finalLin = mix(lit, baseLin, glowMask);
      gl_FragColor = vec4(toSrgb(finalLin), tex.a);
    }
  )GLSL";
}
}  // namespace

ModelViewerWidget::ModelViewerWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setMinimumHeight(240);
  setFocusPolicy(Qt::StrongFocus);
  setToolTip(
    "3D Controls:\n"
    "- Orbit: Middle-drag (Alt+Left-drag)\n"
    "- Pan: Shift+Middle-drag (Alt+Shift+Left-drag)\n"
    "- Dolly: Ctrl+Middle-drag (Alt+Ctrl+Left-drag)\n"
    "- Zoom: Mouse wheel\n"
    "- Frame: F\n"
    "- Reset: R / Home");

  QSettings settings;
  texture_smoothing_ = settings.value("preview/model/textureSmoothing", false).toBool();
}

ModelViewerWidget::~ModelViewerWidget() {
  unload();
}

void ModelViewerWidget::set_texture_smoothing(bool enabled) {
  if (texture_smoothing_ == enabled) {
    return;
  }
  texture_smoothing_ = enabled;

  if (!gl_ready_ || !context()) {
    return;
  }

  const GLint filter = texture_smoothing_ ? GL_LINEAR : GL_NEAREST;

  makeCurrent();

  auto apply = [&](GLuint id) {
    if (id == 0) {
      return;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  };

  apply(texture_id_);
  apply(glow_texture_id_);
  for (const DrawSurface& s : surfaces_) {
    apply(s.texture_id);
    apply(s.glow_texture_id);
  }
  glBindTexture(GL_TEXTURE_2D, 0);

  doneCurrent();
  update();
}

void ModelViewerWidget::set_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette) {
  quake1_palette_ = quake1_palette;
  quake2_palette_ = quake2_palette;
}

void ModelViewerWidget::set_grid_mode(PreviewGridMode mode) {
  if (grid_mode_ == mode) {
    return;
  }
  grid_mode_ = mode;
  ground_extent_ = 0.0f;
  update();
}

void ModelViewerWidget::set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color) {
  if (bg_mode_ == mode && bg_custom_color_ == custom_color) {
    return;
  }
  bg_mode_ = mode;
  bg_custom_color_ = custom_color;
  update();
}

void ModelViewerWidget::set_wireframe_enabled(bool enabled) {
  if (wireframe_enabled_ == enabled) {
    return;
  }
  wireframe_enabled_ = enabled;
  update();
}

void ModelViewerWidget::set_textured_enabled(bool enabled) {
  if (textured_enabled_ == enabled) {
    return;
  }
  textured_enabled_ = enabled;
  update();
}

void ModelViewerWidget::set_glow_enabled(bool enabled) {
  if (glow_enabled_ == enabled) {
    return;
  }
  glow_enabled_ = enabled;
  if (model_ && !last_model_path_.isEmpty()) {
    QString err;
    (void)load_file(last_model_path_, last_skin_path_, &err);
    return;
  }
  update();
}

void ModelViewerWidget::set_fov_degrees(int degrees) {
  const float clamped = std::clamp(static_cast<float>(degrees), 40.0f, 120.0f);
  if (std::abs(clamped - fov_y_deg_) < 0.001f) {
    return;
  }
  fov_y_deg_ = clamped;
  ground_extent_ = 0.0f;
  update();
}

PreviewCameraState ModelViewerWidget::camera_state() const {
  PreviewCameraState state;
  state.center = center_;
  state.yaw_deg = yaw_deg_;
  state.pitch_deg = pitch_deg_;
  state.distance = distance_;
  state.valid = true;
  return state;
}

void ModelViewerWidget::set_camera_state(const PreviewCameraState& state) {
  if (!state.valid) {
    return;
  }
  center_ = state.center;
  yaw_deg_ = std::remainder(state.yaw_deg, 360.0f);
  pitch_deg_ = std::clamp(state.pitch_deg, -89.0f, 89.0f);
  distance_ = std::clamp(state.distance, orbit_min_distance(radius_), orbit_max_distance(radius_));
  ground_extent_ = 0.0f;
  update();
}

bool ModelViewerWidget::load_file(const QString& file_path, QString* error) {
  return load_file(file_path, QString(), error);
}

bool ModelViewerWidget::load_file(const QString& file_path, const QString& skin_path, QString* error) {
  if (error) {
    error->clear();
  }

  const QFileInfo skin_info(skin_path);
  const bool skin_is_q3_skin = (!skin_path.isEmpty() && skin_info.suffix().compare("skin", Qt::CaseInsensitive) == 0);
  Quake3SkinMapping skin_mapping;
  if (skin_is_q3_skin) {
    QString skin_err;
    if (!parse_quake3_skin_file(skin_path, &skin_mapping, &skin_err)) {
      if (error) {
        *error = skin_err.isEmpty() ? "Unable to load .skin file." : skin_err;
      }
      unload();
      return false;
    }
  }

  const auto decode_options_for = [&](const QString& path) -> ImageDecodeOptions {
    ImageDecodeOptions opt;
    const QString leaf = QFileInfo(path).fileName();
    const QString ext = QFileInfo(leaf).suffix().toLower();
    if ((ext == "lmp" || ext == "mip") && quake1_palette_.size() == 256) {
      opt.palette = &quake1_palette_;
    } else if (ext == "wal" && quake2_palette_.size() == 256) {
      opt.palette = &quake2_palette_;
    }
    return opt;
  };

  const auto glow_path_for = [&](const QString& base_path) -> QString {
    if (base_path.isEmpty() || !glow_enabled_) {
      return {};
    }
    const QFileInfo fi(base_path);
    const QString base = fi.completeBaseName();
    if (base.isEmpty()) {
      return {};
    }
    return QDir(fi.absolutePath()).filePath(QString("%1_glow.png").arg(base));
  };

  const auto load_glow_for = [&](const QString& base_path) -> QImage {
    const QString glow_path = glow_path_for(base_path);
    if (glow_path.isEmpty() || !QFileInfo::exists(glow_path)) {
      return {};
    }
    const ImageDecodeResult decoded = decode_image_file(glow_path, ImageDecodeOptions{});
    return decoded.ok() ? decoded.image : QImage();
  };

  const auto decode_embedded_skin = [&](const LoadedModel& model) -> QImage {
    if (model.embedded_skin_width <= 0 || model.embedded_skin_height <= 0 || model.embedded_skin_indices.isEmpty()) {
      return {};
    }
    const qint64 pixel_count =
        static_cast<qint64>(model.embedded_skin_width) * static_cast<qint64>(model.embedded_skin_height);
    if (pixel_count <= 0 || pixel_count > model.embedded_skin_indices.size()) {
      return {};
    }
    QImage img(model.embedded_skin_width, model.embedded_skin_height, QImage::Format_ARGB32);
    if (img.isNull()) {
      return {};
    }
    const bool has_palette = (quake1_palette_.size() == 256);
    const auto* src = reinterpret_cast<const unsigned char*>(model.embedded_skin_indices.constData());
    for (int y = 0; y < model.embedded_skin_height; ++y) {
      QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
      const qint64 row_off = static_cast<qint64>(y) * static_cast<qint64>(model.embedded_skin_width);
      for (int x = 0; x < model.embedded_skin_width; ++x) {
        const unsigned char idx = src[row_off + x];
        if (has_palette) {
          row[x] = quake1_palette_[idx];
        } else {
          row[x] = qRgba(idx, idx, idx, 255);
        }
      }
    }
    return img;
  };

  QString err;
  model_ = load_model_file(file_path, &err);
  if (!model_) {
    if (error) {
      *error = err.isEmpty() ? "Unable to load model." : err;
    }
    unload();
    return false;
  }
  last_model_path_ = file_path;
  last_skin_path_ = skin_path;
  const QFileInfo model_info(file_path);
  const QString model_dir = model_info.absolutePath();
  const QString model_base = model_info.completeBaseName();
  const QString model_format = model_->format.toLower();

  const auto score_auto_skin = [&](const QFileInfo& fi) -> int {
    const QString ext = fi.suffix().toLower();
    if (ext.isEmpty()) {
      return std::numeric_limits<int>::min();
    }
    const QString base = fi.completeBaseName();
    const QString base_lower = base.toLower();
    const QString model_base_lower = model_base.toLower();

    int score = 0;
    if (!model_base_lower.isEmpty()) {
      if (base_lower == model_base_lower) {
        score += 140;
      } else if (base_lower.startsWith(model_base_lower + "_")) {
        score += 95;
      }
    }
    if (base_lower == "skin") {
      score += 80;
    }
    if (base_lower.contains("default")) {
      score += 30;
    }
    if (base_lower.endsWith("_glow")) {
      score -= 200;
    }

    if (model_format == "mdl" && !model_base_lower.isEmpty()) {
      const QString mdl_prefix = model_base_lower + "_";
      if (base_lower == model_base_lower + "_00_00") {
        score += 220;
      } else if (base_lower.startsWith(mdl_prefix)) {
        const QString suffix = base_lower.mid(mdl_prefix.size());
        const bool two_by_two_numeric = (suffix.size() == 5 && suffix[2] == '_' && suffix[0].isDigit() &&
                                         suffix[1].isDigit() && suffix[3].isDigit() && suffix[4].isDigit());
        score += two_by_two_numeric ? 180 : 120;
      }
    }

    if (ext == "png") {
      score += 20;
    } else if (ext == "tga") {
      score += 18;
    } else if (ext == "jpg" || ext == "jpeg") {
      score += 16;
    } else if (ext == "pcx") {
      score += 14;
    } else if (ext == "wal") {
      score += 12;
    } else if (ext == "dds") {
      score += 10;
    } else if (ext == "lmp") {
      score += (model_format == "mdl") ? 26 : 12;
    } else if (ext == "mip") {
      score += (model_format == "mdl") ? 24 : 11;
    } else {
      score -= 1000;
    }
    return score;
  };

  const auto find_auto_skin_on_disk = [&]() -> QString {
    if (model_dir.isEmpty()) {
      return {};
    }
    QDir d(model_dir);
    if (!d.exists()) {
      return {};
    }
    const QStringList files = d.entryList(QStringList() << "*.png"
                                                        << "*.tga"
                                                        << "*.jpg"
                                                        << "*.jpeg"
                                                        << "*.pcx"
                                                        << "*.wal"
                                                        << "*.dds"
                                                        << "*.lmp"
                                                        << "*.mip",
                                          QDir::Files,
                                          QDir::Name);
    QString best_name;
    int best_score = std::numeric_limits<int>::min();
    for (const QString& name : files) {
      const int score = score_auto_skin(QFileInfo(name));
      if (score > best_score || (score == best_score && name.compare(best_name, Qt::CaseInsensitive) < 0)) {
        best_score = score;
        best_name = name;
      }
    }
    if (best_score < 40) {
      return {};
    }
    return best_name.isEmpty() ? QString() : d.filePath(best_name);
  };

  const auto try_apply_skin = [&](const QString& candidate_path) -> bool {
    if (candidate_path.isEmpty()) {
      return false;
    }
    const ImageDecodeResult decoded = decode_image_file(candidate_path, decode_options_for(candidate_path));
    if (!decoded.ok()) {
      return false;
    }
    skin_image_ = decoded.image;
    if (glow_enabled_) {
      skin_glow_image_ = load_glow_for(candidate_path);
    }
    last_skin_path_ = candidate_path;
    return !skin_image_.isNull();
  };

  surfaces_.clear();
  const int total_indices = model_->mesh.indices.size();
  if (model_->surfaces.isEmpty()) {
    DrawSurface s;
    s.first_index = 0;
    s.index_count = total_indices;
    s.name = "model";
    surfaces_.push_back(std::move(s));
  } else {
    surfaces_.reserve(model_->surfaces.size());
    for (const ModelSurface& ms : model_->surfaces) {
      const qint64 first = ms.first_index;
      const qint64 count = ms.index_count;
      if (first < 0 || count <= 0 || first >= total_indices || (first + count) > total_indices) {
        continue;
      }
      DrawSurface s;
      s.first_index = static_cast<int>(first);
      s.index_count = static_cast<int>(count);
      s.name = ms.name;
      s.shader_hint = ms.shader;
      s.shader_leaf = QFileInfo(ms.shader).fileName();
      surfaces_.push_back(std::move(s));
    }
    if (surfaces_.isEmpty()) {
      DrawSurface s;
      s.first_index = 0;
      s.index_count = total_indices;
      s.name = "model";
      surfaces_.push_back(std::move(s));
    }
  }

  skin_image_ = {};
  skin_glow_image_ = {};
  has_texture_ = false;
  has_glow_ = false;
  pending_texture_upload_ = false;
  if (!skin_is_q3_skin && !skin_path.isEmpty()) {
    (void)try_apply_skin(skin_path);
  }
  if (skin_image_.isNull() && !skin_is_q3_skin) {
    const QString auto_skin = find_auto_skin_on_disk();
    (void)try_apply_skin(auto_skin);
  }
  if (skin_image_.isNull() && model_) {
    skin_image_ = decode_embedded_skin(*model_);
  }

  if (skin_is_q3_skin && !skin_mapping.surface_to_shader.isEmpty()) {
    for (DrawSurface& s : surfaces_) {
      const QString key = s.name.trimmed().toLower();
      if (!skin_mapping.surface_to_shader.contains(key)) {
        continue;
      }
      const QString shader = skin_mapping.surface_to_shader.value(key).trimmed();
      s.shader_hint = shader;
      s.shader_leaf = shader.isEmpty() ? QString() : QFileInfo(shader).fileName();
      s.image = {};
      s.glow_image = {};
    }
  }

  if (!model_dir.isEmpty()) {
    const QStringList exts = {"png", "tga", "jpg", "jpeg", "pcx", "wal", "dds", "lmp", "mip"};

    const auto try_find_in_dir = [&](const QString& base_or_file) -> QString {
      if (base_or_file.isEmpty()) {
        return {};
      }
      const QFileInfo fi(base_or_file);
      const QString base = fi.completeBaseName();
      const QString file = fi.fileName();
      if (!file.isEmpty() && QFileInfo::exists(QDir(model_dir).filePath(file))) {
        return QDir(model_dir).filePath(file);
      }
      if (!base.isEmpty()) {
        for (const QString& ext : exts) {
          const QString cand = QDir(model_dir).filePath(QString("%1.%2").arg(base, ext));
          if (QFileInfo::exists(cand)) {
            return cand;
          }
        }
      }
      // Case-insensitive basename match (helps when extracted filenames differ in case).
      QDir d(model_dir);
      const QStringList files = d.entryList(QStringList() << "*.png"
                                                          << "*.tga"
                                                          << "*.jpg"
                                                          << "*.jpeg"
                                                          << "*.pcx"
                                                          << "*.wal"
                                                          << "*.dds"
                                                          << "*.lmp"
                                                          << "*.mip",
                                            QDir::Files,
                                            QDir::Name);
      for (const QString& f : files) {
        const QFileInfo cfi(f);
        if (cfi.completeBaseName().compare(base, Qt::CaseInsensitive) == 0 || f.compare(file, Qt::CaseInsensitive) == 0) {
          return d.filePath(f);
        }
      }
      return {};
    };

    for (DrawSurface& s : surfaces_) {
      if (s.shader_leaf.isEmpty()) {
        continue;
      }
      const QString found = try_find_in_dir(s.shader_leaf);
      if (found.isEmpty()) {
        continue;
      }
      const ImageDecodeResult decoded = decode_image_file(found, decode_options_for(found));
      if (decoded.ok()) {
        s.image = decoded.image;
        if (glow_enabled_) {
          s.glow_image = load_glow_for(found);
        }
      }
    }
  }

  pending_texture_upload_ = (!skin_image_.isNull() || !skin_glow_image_.isNull());
  for (const DrawSurface& s : surfaces_) {
    if (!s.image.isNull() || !s.glow_image.isNull()) {
      pending_texture_upload_ = true;
      break;
    }
  }

  reset_camera_from_mesh();
  pending_upload_ = true;
  upload_mesh_if_possible();
  update();
  return true;
}

void ModelViewerWidget::unload() {
  model_.reset();
  last_model_path_.clear();
  last_skin_path_.clear();
  index_count_ = 0;
  index_type_ = GL_UNSIGNED_INT;
  surfaces_.clear();
  pending_upload_ = false;
  pending_texture_upload_ = false;
  skin_image_ = {};
  skin_glow_image_ = {};
  has_texture_ = false;
  has_glow_ = false;
  if (gl_ready_ && context()) {
    makeCurrent();
    destroy_gl_resources();
    doneCurrent();
  }
  update();
}

void ModelViewerWidget::initializeGL() {
  initializeOpenGLFunctions();
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  gl_ready_ = true;
  // Reparenting (e.g. fullscreen toggle) can recreate the GL context.
  // Reset GPU handles and force a fresh upload for the new context.
  destroy_gl_resources();
  pending_upload_ = model_.has_value();
  pending_texture_upload_ = (!skin_image_.isNull() || !skin_glow_image_.isNull());
  if (!pending_texture_upload_) {
    for (const DrawSurface& s : surfaces_) {
      if (!s.image.isNull() || !s.glow_image.isNull()) {
        pending_texture_upload_ = true;
        break;
      }
    }
  }
  ensure_program();
  upload_mesh_if_possible();
}

void ModelViewerWidget::paintGL() {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  ensure_program();
  if (!program_.isLinked()) {
    return;
  }

  update_background_mesh_if_needed();

  QVector3D bg_top;
  QVector3D bg_bottom;
  QVector3D bg_base;
  update_background_colors(&bg_top, &bg_bottom, &bg_base);

  QVector3D grid_color;
  QVector3D axis_x;
  QVector3D axis_y;
  update_grid_colors(&grid_color, &axis_x, &axis_y);

  program_.bind();

  QMatrix4x4 identity;
  identity.setToIdentity();
  program_.setUniformValue("uMvp", identity);
  program_.setUniformValue("uModel", identity);
  program_.setUniformValue("uIsBackground", 1.0f);
  program_.setUniformValue("uIsGround", 0.0f);
  program_.setUniformValue("uBgTop", bg_top);
  program_.setUniformValue("uBgBottom", bg_bottom);
  program_.setUniformValue("uHasTex", 0);
  program_.setUniformValue("uHasGlow", 0);
  program_.setUniformValue("uTex", 0);
  program_.setUniformValue("uGlowTex", 1);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  if (bg_vao_.isCreated()) {
    bg_vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    bg_vao_.release();
  }
  glEnable(GL_DEPTH_TEST);

  if ((!vbo_.isCreated() || !ibo_.isCreated()) && model_ && !pending_upload_) {
    pending_upload_ = true;
    upload_mesh_if_possible();
  }

  if (!model_ || index_count_ <= 0 || !vbo_.isCreated() || !ibo_.isCreated()) {
    program_.release();
    return;
  }

  QMatrix4x4 proj;
  const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
  const float near_plane = std::max(0.001f, radius_ * 0.02f);
  const float far_plane = std::max(10.0f, radius_ * 50.0f);
  proj.perspective(fov_y_deg_, aspect, near_plane, far_plane);

  const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
  const QVector3D cam_pos = center_ + dir * distance_;
  const QVector3D view_target = center_;

  QMatrix4x4 view;
  view.lookAt(cam_pos, view_target, QVector3D(0, 0, 1));

  QMatrix4x4 model_m;
  model_m.setToIdentity();

  const QMatrix4x4 mvp = proj * view * model_m;

  program_.setUniformValue("uMvp", mvp);
  program_.setUniformValue("uModel", model_m);
  program_.setUniformValue("uCamPos", cam_pos);
  program_.setUniformValue("uLightDir", QVector3D(0.4f, 0.25f, 1.0f));
  program_.setUniformValue("uFillDir", QVector3D(-0.65f, -0.15f, 0.8f));
  program_.setUniformValue("uBaseColor", QVector3D(0.75f, 0.78f, 0.82f));
  program_.setUniformValue("uGroundColor", bg_base);
  program_.setUniformValue("uShadowCenter", QVector3D(center_.x(), center_.y(), ground_z_));
  program_.setUniformValue("uShadowRadius", std::max(0.05f, radius_ * 1.45f));
  program_.setUniformValue("uShadowStrength", 0.55f);
  program_.setUniformValue("uShadowSoftness", 2.4f);
  program_.setUniformValue("uGridMode", grid_mode_ == PreviewGridMode::Grid ? 1.0f : 0.0f);
  program_.setUniformValue("uGridScale", grid_scale_);
  program_.setUniformValue("uGridColor", grid_color);
  program_.setUniformValue("uAxisColorX", axis_x);
  program_.setUniformValue("uAxisColorY", axis_y);
  program_.setUniformValue("uIsBackground", 0.0f);
  program_.setUniformValue("uBgTop", bg_top);
  program_.setUniformValue("uBgBottom", bg_bottom);
  program_.setUniformValue("uTex", 0);
  program_.setUniformValue("uGlowTex", 1);

  update_ground_mesh_if_needed();

  apply_wireframe_state(wireframe_enabled_);

  if (grid_mode_ != PreviewGridMode::None && ground_index_count_ > 0 && ground_vbo_.isCreated() && ground_ibo_.isCreated()) {
    program_.setUniformValue("uIsGround", 1.0f);
    program_.setUniformValue("uHasTex", 0);
    program_.setUniformValue("uHasGlow", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);

    ground_vbo_.bind();
    ground_ibo_.bind();
    const int pos_loc = program_.attributeLocation("aPos");
    const int nrm_loc = program_.attributeLocation("aNormal");
    const int uv_loc = program_.attributeLocation("aUV");
    program_.enableAttributeArray(pos_loc);
    program_.enableAttributeArray(nrm_loc);
    program_.enableAttributeArray(uv_loc);
    program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
    program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
    program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));

    glDrawElements(GL_TRIANGLES, ground_index_count_, GL_UNSIGNED_SHORT, nullptr);
  }

  program_.setUniformValue("uIsGround", 0.0f);

  const bool vao_bound = vao_.isCreated();
  if (vao_bound) {
    vao_.bind();
  }
  vbo_.bind();
  ibo_.bind();
  const int pos_loc = program_.attributeLocation("aPos");
  const int nrm_loc = program_.attributeLocation("aNormal");
  const int uv_loc = program_.attributeLocation("aUV");
  program_.enableAttributeArray(pos_loc);
  program_.enableAttributeArray(nrm_loc);
  program_.enableAttributeArray(uv_loc);
  program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));

  const int index_size = (index_type_ == GL_UNSIGNED_SHORT) ? 2 : 4;
  const QVector<DrawSurface>& draw_surfaces = surfaces_;
  for (const DrawSurface& s : draw_surfaces) {
    if (s.first_index < 0 || s.index_count <= 0 || (s.first_index + s.index_count) > index_count_) {
      continue;
    }

    GLuint tid = 0;
    bool has_tex = false;
    if (s.has_texture && s.texture_id != 0) {
      tid = s.texture_id;
      has_tex = true;
    } else if (has_texture_ && texture_id_ != 0) {
      tid = texture_id_;
      has_tex = true;
    }

    GLuint gtid = 0;
    bool has_glow = false;
    if (s.has_glow && s.glow_texture_id != 0) {
      gtid = s.glow_texture_id;
      has_glow = true;
    } else if (has_glow_ && glow_texture_id_ != 0) {
      gtid = glow_texture_id_;
      has_glow = true;
    }

    const bool use_tex = textured_enabled_ && has_tex;
    const bool use_glow = textured_enabled_ && has_glow;

    program_.setUniformValue("uHasTex", use_tex ? 1 : 0);
    program_.setUniformValue("uHasGlow", use_glow ? 1 : 0);

    if (use_tex) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tid);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glDisable(GL_BLEND);
    }

    if (use_glow) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gtid);
      glActiveTexture(GL_TEXTURE0);
    } else {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
    }

    const uintptr_t offs = static_cast<uintptr_t>(s.first_index) * static_cast<uintptr_t>(index_size);
    glDrawElements(GL_TRIANGLES, s.index_count, index_type_, reinterpret_cast<const void*>(offs));
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);

  apply_wireframe_state(false);

  vbo_.release();
  ibo_.release();
  if (vao_bound) {
    vao_.release();
  }
  program_.release();
}

void ModelViewerWidget::resizeGL(int, int) {
  update();
}

void ModelViewerWidget::mousePressEvent(QMouseEvent* event) {
  if (!event) {
    QOpenGLWidget::mousePressEvent(event);
    return;
  }

  const Qt::MouseButton button = event->button();
  const Qt::KeyboardModifiers mods = event->modifiers();
  const bool mmb = (button == Qt::MiddleButton);
  const bool alt_lmb = (button == Qt::LeftButton && (mods & Qt::AltModifier));
  if (mmb || alt_lmb) {
    setFocus(Qt::MouseFocusReason);
    last_mouse_pos_ = event->pos();
    drag_mode_ = DragMode::Orbit;
    if (mods & Qt::ControlModifier) {
      drag_mode_ = DragMode::Dolly;
    } else if (mods & Qt::ShiftModifier) {
      drag_mode_ = DragMode::Pan;
    }
    drag_buttons_ = button;
    event->accept();
    return;
  }

  QOpenGLWidget::mousePressEvent(event);
}

void ModelViewerWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!event || drag_mode_ == DragMode::None || drag_buttons_ == Qt::NoButton ||
      (event->buttons() & drag_buttons_) != drag_buttons_) {
    drag_mode_ = DragMode::None;
    drag_buttons_ = Qt::NoButton;
    QOpenGLWidget::mouseMoveEvent(event);
    return;
  }

  const QPoint delta = event->pos() - last_mouse_pos_;
  last_mouse_pos_ = event->pos();

  if (drag_mode_ == DragMode::Orbit) {
    yaw_deg_ += static_cast<float>(delta.x()) * kOrbitSensitivityDegPerPixel;
    pitch_deg_ += static_cast<float>(-delta.y()) * kOrbitSensitivityDegPerPixel;
    pitch_deg_ = std::clamp(pitch_deg_, -89.0f, 89.0f);
  } else if (drag_mode_ == DragMode::Pan) {
    pan_by_pixels(delta);
  } else if (drag_mode_ == DragMode::Dolly) {
    dolly_by_pixels(delta);
  }

  update();
  event->accept();
}

void ModelViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event && drag_mode_ != DragMode::None && drag_buttons_ != Qt::NoButton &&
      (Qt::MouseButtons(event->button()) & drag_buttons_) &&
      (event->buttons() & drag_buttons_) != drag_buttons_) {
    drag_mode_ = DragMode::None;
    drag_buttons_ = Qt::NoButton;
    event->accept();
    return;
  }
  QOpenGLWidget::mouseReleaseEvent(event);
}

void ModelViewerWidget::wheelEvent(QWheelEvent* event) {
  if (!event) {
    return;
  }
  const QPoint num_deg = event->angleDelta() / 8;
  if (!num_deg.isNull()) {
    const float steps = static_cast<float>(num_deg.y()) / 15.0f;
    const float factor = std::pow(0.85f, steps);
    apply_orbit_zoom(factor,
                     orbit_min_distance(radius_),
                     orbit_max_distance(radius_),
                     &distance_,
                     &center_,
                     yaw_deg_,
                     pitch_deg_);
    ground_extent_ = 0.0f;
    update();
    event->accept();
    return;
  }
  QOpenGLWidget::wheelEvent(event);
}

void ModelViewerWidget::keyPressEvent(QKeyEvent* event) {
  if (!event) {
    QOpenGLWidget::keyPressEvent(event);
    return;
  }

  if (event->key() == Qt::Key_R || event->key() == Qt::Key_Home) {
    reset_camera_from_mesh();
    update();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_F) {
    frame_mesh();
    update();
    event->accept();
    return;
  }

  QOpenGLWidget::keyPressEvent(event);
}

void ModelViewerWidget::keyReleaseEvent(QKeyEvent* event) {
  if (!event) {
    QOpenGLWidget::keyReleaseEvent(event);
    return;
  }

  QOpenGLWidget::keyReleaseEvent(event);
}

void ModelViewerWidget::reset_camera_from_mesh() {
  yaw_deg_ = 45.0f;
  pitch_deg_ = 20.0f;
  frame_mesh();
}

void ModelViewerWidget::frame_mesh() {
  if (!model_) {
    center_ = QVector3D(0, 0, 0);
    radius_ = 1.0f;
    distance_ = 3.0f;
    ground_z_ = 0.0f;
    ground_extent_ = 0.0f;
    return;
  }
  const QVector3D mins = model_->mesh.mins;
  const QVector3D maxs = model_->mesh.maxs;
  center_ = (mins + maxs) * 0.5f;
  const QVector3D half_extents = (maxs - mins) * 0.5f;
  radius_ = std::max(0.001f, half_extents.length());
  const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
  const QVector3D view_forward = (-spherical_dir(yaw_deg_, pitch_deg_)).normalized();
  const float fit_dist = fit_distance_for_aabb(half_extents, view_forward, aspect, fov_y_deg_);
  distance_ = std::clamp(fit_dist * 1.05f, orbit_min_distance(radius_), orbit_max_distance(radius_));
  ground_z_ = mins.z() - radius_ * 0.02f;
  ground_extent_ = 0.0f;
}

void ModelViewerWidget::pan_by_pixels(const QPoint& delta) {
  if (height() <= 0) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846f;
  const float fov_rad = fov_y_deg_ * kPi / 180.0f;
  const float units_per_px =
      (2.0f * distance_ * std::tan(fov_rad * 0.5f)) / std::max(1.0f, static_cast<float>(height()));

  const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
  const QVector3D forward = (-dir).normalized();
  const QVector3D world_up(0.0f, 0.0f, 1.0f);

  QVector3D right = QVector3D::crossProduct(forward, world_up);
  if (right.lengthSquared() < 1e-6f) {
    right = QVector3D(1.0f, 0.0f, 0.0f);
  } else {
    right.normalize();
  }

  const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
  center_ += (-right * static_cast<float>(delta.x()) + up * static_cast<float>(delta.y())) * units_per_px;
  ground_extent_ = 0.0f;  // ensure the ground mesh recenters during pan
}

void ModelViewerWidget::dolly_by_pixels(const QPoint& delta) {
  const float factor = std::pow(1.01f, static_cast<float>(delta.y()));
  apply_orbit_zoom(factor,
                   orbit_min_distance(radius_),
                   orbit_max_distance(radius_),
                   &distance_,
                   &center_,
                   yaw_deg_,
                   pitch_deg_);
  ground_extent_ = 0.0f;
}

void ModelViewerWidget::ensure_program() {
  if (program_.isLinked()) {
    return;
  }

  program_.removeAllShaders();

  QSurfaceFormat fmt = format();
  if (QOpenGLContext::currentContext()) {
    fmt = QOpenGLContext::currentContext()->format();
  }

  const bool vs_ok = program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader_source(fmt));
  const bool fs_ok = program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_shader_source(fmt));

  program_.bindAttributeLocation("aPos", 0);
  program_.bindAttributeLocation("aNormal", 1);
  program_.bindAttributeLocation("aUV", 2);

  if (!vs_ok || !fs_ok || !program_.link()) {
    qWarning() << "ModelViewerWidget shader compile/link failed:" << program_.log();
  }
}

void ModelViewerWidget::destroy_gl_resources() {
  if (vao_.isCreated()) {
    vao_.destroy();
  }
  if (vbo_.isCreated()) {
    vbo_.destroy();
  }
  if (ibo_.isCreated()) {
    ibo_.destroy();
  }
  if (ground_vbo_.isCreated()) {
    ground_vbo_.destroy();
  }
  if (ground_ibo_.isCreated()) {
    ground_ibo_.destroy();
  }
  if (bg_vbo_.isCreated()) {
    bg_vbo_.destroy();
  }
  if (bg_vao_.isCreated()) {
    bg_vao_.destroy();
  }
  ground_index_count_ = 0;
  for (DrawSurface& s : surfaces_) {
    if (s.texture_id != 0) {
      glDeleteTextures(1, &s.texture_id);
      s.texture_id = 0;
      s.has_texture = false;
    }
    if (s.glow_texture_id != 0) {
      glDeleteTextures(1, &s.glow_texture_id);
      s.glow_texture_id = 0;
      s.has_glow = false;
    }
  }
  if (texture_id_ != 0) {
    glDeleteTextures(1, &texture_id_);
    texture_id_ = 0;
  }
  if (glow_texture_id_ != 0) {
    glDeleteTextures(1, &glow_texture_id_);
    glow_texture_id_ = 0;
  }
  has_texture_ = false;
  has_glow_ = false;
  // Avoid forcing program release during context transitions: in Qt debug builds this can
  // assert if the underlying function wrapper is not initialized for the current context.
  program_.removeAllShaders();
}

void ModelViewerWidget::update_ground_mesh_if_needed() {
  if (!model_ || !gl_ready_ || !context()) {
    return;
  }

  update_grid_settings();
  const float extent = std::max(radius_ * 2.6f, 1.0f);
  if (ground_index_count_ == 6 && std::abs(extent - ground_extent_) < 0.001f && ground_vbo_.isCreated() &&
      ground_ibo_.isCreated()) {
    return;
  }

  ground_extent_ = extent;
  const float z = ground_z_;
  const float minx = center_.x() - extent;
  const float maxx = center_.x() + extent;
  const float miny = center_.y() - extent;
  const float maxy = center_.y() + extent;

  QVector<GpuVertex> verts;
  verts.reserve(4);
  verts.push_back(GpuVertex{minx, miny, z, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
  verts.push_back(GpuVertex{maxx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f});
  verts.push_back(GpuVertex{maxx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
  verts.push_back(GpuVertex{minx, maxy, z, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f});

  const std::uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

  if (!ground_vbo_.isCreated()) {
    ground_vbo_.create();
  }
  if (!ground_ibo_.isCreated()) {
    ground_ibo_.create();
  }

  ground_vbo_.bind();
  ground_vbo_.allocate(verts.constData(), static_cast<int>(verts.size() * sizeof(GpuVertex)));
  ground_ibo_.bind();
  ground_ibo_.allocate(idx, static_cast<int>(sizeof(idx)));

  ground_index_count_ = 6;
}

void ModelViewerWidget::update_background_mesh_if_needed() {
  if (!gl_ready_ || !context()) {
    return;
  }
  if (bg_vao_.isCreated() && bg_vbo_.isCreated()) {
    return;
  }

  ensure_program();
  program_.bind();

  if (!bg_vbo_.isCreated()) {
    bg_vbo_.create();
  }
  if (!bg_vao_.isCreated()) {
    bg_vao_.create();
  }

  const GpuVertex verts[6] = {
    {-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    { 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
    { 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    {-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    { 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    {-1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
  };

  bg_vao_.bind();
  bg_vbo_.bind();
  bg_vbo_.allocate(verts, static_cast<int>(sizeof(verts)));

  const int pos_loc = program_.attributeLocation("aPos");
  const int nrm_loc = program_.attributeLocation("aNormal");
  const int uv_loc = program_.attributeLocation("aUV");
  program_.enableAttributeArray(pos_loc);
  program_.enableAttributeArray(nrm_loc);
  program_.enableAttributeArray(uv_loc);
  program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));

  bg_vao_.release();
  bg_vbo_.release();
  program_.release();
}

void ModelViewerWidget::update_grid_settings() {
  const float reference = std::max(distance_, radius_ * 0.25f);
  grid_scale_ = quantized_grid_scale(reference);
}

void ModelViewerWidget::apply_wireframe_state(bool enabled) {
  if (!gl_ready_ || !context()) {
    return;
  }
  QOpenGLContext* ctx = QOpenGLContext::currentContext();
  if (!ctx || ctx->isOpenGLES()) {
    return;
  }
  QOpenGLFunctions_1_1 f;
  f.initializeOpenGLFunctions();
  f.glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
}

void ModelViewerWidget::update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const {
  QColor base_color;
  if (bg_mode_ == PreviewBackgroundMode::Custom && bg_custom_color_.isValid()) {
    base_color = bg_custom_color_;
  } else if (bg_mode_ == PreviewBackgroundMode::Grey) {
    base_color = QColor(88, 88, 92);
  } else {
    base_color = palette().color(QPalette::Window);
  }
  if (!base_color.isValid()) {
    base_color = QColor(64, 64, 68);
  }

  QColor top_color = base_color.lighter(112);
  QColor bottom_color = base_color.darker(118);

  if (top) {
    *top = QVector3D(top_color.redF(), top_color.greenF(), top_color.blueF());
  }
  if (bottom) {
    *bottom = QVector3D(bottom_color.redF(), bottom_color.greenF(), bottom_color.blueF());
  }
  if (base) {
    *base = QVector3D(base_color.redF(), base_color.greenF(), base_color.blueF());
  }
}

void ModelViewerWidget::update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const {
  QVector3D base_vec;
  update_background_colors(nullptr, nullptr, &base_vec);
  QColor base_color = QColor::fromRgbF(base_vec.x(), base_vec.y(), base_vec.z());
  QColor grid_color = (base_color.lightness() < 128) ? base_color.lighter(140) : base_color.darker(140);

  QColor axis_x_color = palette().color(QPalette::Highlight);
  if (!axis_x_color.isValid()) {
    axis_x_color = QColor(220, 80, 80);
  }
  QColor axis_y_color = palette().color(QPalette::Link);
  if (!axis_y_color.isValid()) {
    axis_y_color = QColor(80, 180, 120);
  }

  if (grid) {
    *grid = QVector3D(grid_color.redF(), grid_color.greenF(), grid_color.blueF());
  }
  if (axis_x) {
    *axis_x = QVector3D(axis_x_color.redF(), axis_x_color.greenF(), axis_x_color.blueF());
  }
  if (axis_y) {
    *axis_y = QVector3D(axis_y_color.redF(), axis_y_color.greenF(), axis_y_color.blueF());
  }
}

void ModelViewerWidget::upload_mesh_if_possible() {
  if (!pending_upload_ || !model_ || !gl_ready_ || !context()) {
    return;
  }

  makeCurrent();
  ensure_program();

  // GLES2 does not support GL_UNSIGNED_INT indices.
  index_type_ = GL_UNSIGNED_INT;
  const bool is_gles = QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES();
  const QSurfaceFormat fmt = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->format() : format();
  const bool gles2 = is_gles && (fmt.majorVersion() < 3);
  if (gles2) {
    std::uint32_t max_index = 0;
    for (std::uint32_t i : model_->mesh.indices) {
      max_index = std::max(max_index, i);
    }
    if (max_index <= static_cast<std::uint32_t>(std::numeric_limits<quint16>::max())) {
      index_type_ = GL_UNSIGNED_SHORT;
    } else {
      qWarning() << "ModelViewerWidget: model has" << max_index << "index which exceeds GLES2 limits.";
      index_count_ = 0;
      pending_upload_ = false;
      doneCurrent();
      return;
    }
  }

  QVector<GpuVertex> gpu;
  gpu.resize(model_->mesh.vertices.size());
  for (int i = 0; i < model_->mesh.vertices.size(); ++i) {
    const ModelVertex& v = model_->mesh.vertices[i];
    gpu[i] = GpuVertex{v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v};
  }

  if (!vbo_.isCreated()) {
    vbo_.create();
  }
  if (!ibo_.isCreated()) {
    ibo_.create();
  }

  vbo_.bind();
  vbo_.allocate(gpu.constData(), gpu.size() * static_cast<int>(sizeof(GpuVertex)));

  ibo_.bind();
  if (index_type_ == GL_UNSIGNED_SHORT) {
    QVector<quint16> indices16;
    indices16.resize(model_->mesh.indices.size());
    for (int i = 0; i < model_->mesh.indices.size(); ++i) {
      indices16[i] = static_cast<quint16>(model_->mesh.indices[i]);
    }
    ibo_.allocate(indices16.constData(), indices16.size() * static_cast<int>(sizeof(quint16)));
  } else {
    ibo_.allocate(model_->mesh.indices.constData(),
                  model_->mesh.indices.size() * static_cast<int>(sizeof(std::uint32_t)));
  }

  if (!vao_.isCreated()) {
    vao_.create();
  }
  vao_.bind();
  // Element-array buffer binding is part of VAO state, so bind it while the VAO is bound.
  vbo_.bind();
  ibo_.bind();

  program_.bind();
  const int pos_loc = program_.attributeLocation("aPos");
  const int nrm_loc = program_.attributeLocation("aNormal");
  const int uv_loc = program_.attributeLocation("aUV");
  program_.enableAttributeArray(pos_loc);
  program_.enableAttributeArray(nrm_loc);
  program_.enableAttributeArray(uv_loc);
  program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));
  program_.release();

  vao_.release();
  vbo_.release();
  ibo_.release();

  index_count_ = model_->mesh.indices.size();
  pending_upload_ = false;
  upload_textures_if_possible();
  doneCurrent();
}

void ModelViewerWidget::upload_textures_if_possible() {
  if (!pending_texture_upload_ || !gl_ready_ || !context()) {
    return;
  }

  const GLint filter = texture_smoothing_ ? GL_LINEAR : GL_NEAREST;

  auto delete_tex = [&](GLuint* id) {
    if (id && *id != 0) {
      glDeleteTextures(1, id);
      *id = 0;
    }
  };

  for (DrawSurface& s : surfaces_) {
    delete_tex(&s.texture_id);
    s.has_texture = false;
    delete_tex(&s.glow_texture_id);
    s.has_glow = false;
  }

  delete_tex(&texture_id_);
  has_texture_ = false;
  delete_tex(&glow_texture_id_);
  has_glow_ = false;

  auto upload = [&](const QImage& src, GLuint* out_id) -> bool {
    if (!out_id) {
      return false;
    }
    *out_id = 0;
    if (src.isNull()) {
      return false;
    }
    QImage img = src.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
    if (img.isNull()) {
      return false;
    }
    glGenTextures(1, out_id);
    if (*out_id == 0) {
      return false;
    }
    glBindTexture(GL_TEXTURE_2D, *out_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 img.width(),
                 img.height(),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 img.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
    return (*out_id != 0);
  };

  if (upload(skin_image_, &texture_id_)) {
    has_texture_ = true;
  }
  if (upload(skin_glow_image_, &glow_texture_id_)) {
    has_glow_ = true;
  }

  for (DrawSurface& s : surfaces_) {
    if (upload(s.image, &s.texture_id)) {
      s.has_texture = true;
    }
    if (upload(s.glow_image, &s.glow_texture_id)) {
      s.has_glow = true;
    }
  }

  pending_texture_upload_ = false;
}

