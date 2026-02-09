#include "ui/bsp_preview_widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <QMatrix4x4>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QDebug>
#include <QtGui/QOpenGLContext>
#include <QtOpenGL/QOpenGLFunctions_1_1>

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
      attribute highp vec3 aColor;
      attribute highp vec2 aUV;
      attribute highp vec2 aUV2;
      uniform highp mat4 uMvp;
      uniform highp mat4 uModel;
      uniform highp vec2 uTexScale;
      uniform highp vec2 uTexOffset;
      varying highp vec3 vNormal;
      varying highp vec3 vColor;
      varying highp vec2 vUV;
      varying highp vec2 vUV2;
      varying highp vec3 vPos;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vColor = aColor;
        vUV = aUV * uTexScale + uTexOffset;
        vUV2 = aUV2;
        vPos = (uModel * vec4(aPos, 1.0)).xyz;
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
      layout(location = 2) in vec3 aColor;
      layout(location = 3) in vec2 aUV;
      layout(location = 4) in vec2 aUV2;
      uniform mat4 uMvp;
      uniform mat4 uModel;
      uniform vec2 uTexScale;
      uniform vec2 uTexOffset;
      out vec3 vNormal;
      out vec3 vColor;
      out vec2 vUV;
      out vec2 vUV2;
      out vec3 vPos;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vColor = aColor;
        vUV = aUV * uTexScale + uTexOffset;
        vUV2 = aUV2;
        vPos = (uModel * vec4(aPos, 1.0)).xyz;
      }
    )GLSL";
  }

  if (glsl_130) {
    return R"GLSL(
      #version 130
      in vec3 aPos;
      in vec3 aNormal;
      in vec3 aColor;
      in vec2 aUV;
      in vec2 aUV2;
      uniform mat4 uMvp;
      uniform mat4 uModel;
      uniform vec2 uTexScale;
      uniform vec2 uTexOffset;
      out vec3 vNormal;
      out vec3 vColor;
      out vec2 vUV;
      out vec2 vUV2;
      out vec3 vPos;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vColor = aColor;
        vUV = aUV * uTexScale + uTexOffset;
        vUV2 = aUV2;
        vPos = (uModel * vec4(aPos, 1.0)).xyz;
      }
    )GLSL";
  }

  return R"GLSL(
    #version 120
    attribute vec3 aPos;
    attribute vec3 aNormal;
    attribute vec3 aColor;
    attribute vec2 aUV;
    attribute vec2 aUV2;
    uniform mat4 uMvp;
    uniform mat4 uModel;
    uniform vec2 uTexScale;
    uniform vec2 uTexOffset;
    varying vec3 vNormal;
    varying vec3 vColor;
    varying vec2 vUV;
    varying vec2 vUV2;
    varying vec3 vPos;
    void main() {
      gl_Position = uMvp * vec4(aPos, 1.0);
      vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
      vColor = aColor;
      vUV = aUV * uTexScale + uTexOffset;
      vUV2 = aUV2;
      vPos = (uModel * vec4(aPos, 1.0)).xyz;
    }
  )GLSL";
}

QString fragment_shader_source(const QSurfaceFormat& fmt) {
  if (QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES()) {
    return R"GLSL(
      precision mediump float;
      varying mediump vec3 vNormal;
      varying mediump vec3 vColor;
      varying mediump vec2 vUV;
      varying mediump vec2 vUV2;
      varying mediump vec3 vPos;
      uniform mediump vec3 uLightDir;
      uniform mediump vec3 uFillDir;
      uniform mediump vec3 uAmbient;
      uniform mediump float uLightmapStrength;
      uniform mediump float uIsGround;
      uniform mediump float uGridMode;
      uniform mediump float uGridScale;
      uniform mediump vec3 uGroundColor;
      uniform mediump vec3 uGridColor;
      uniform mediump vec3 uAxisColorX;
      uniform mediump vec3 uAxisColorY;
      uniform mediump vec3 uShadowCenter;
      uniform mediump float uShadowRadius;
      uniform mediump float uShadowStrength;
      uniform mediump float uShadowSoftness;
      uniform mediump float uIsBackground;
      uniform mediump vec3 uBgTop;
      uniform mediump vec3 uBgBottom;
      uniform sampler2D uTex;
      uniform sampler2D uLightmapTex;
      uniform int uHasTexture;
      uniform int uHasLightmap;

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
        float ndl = abs(dot(n, normalize(uLightDir)));
        float ndl2 = abs(dot(n, normalize(uFillDir)));
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
          float gdiff = ndl * 0.5 + ndl2 * 0.2;
          vec3 ground = groundLin * (0.22 + gdiff);

          vec2 delta = vPos.xy - uShadowCenter.xy;
          float dist = length(delta) / max(0.001, uShadowRadius);
          float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
          shadow = clamp(shadow, 0.0, 0.85);
          ground *= (1.0 - shadow);
          gl_FragColor = vec4(toSrgb(ground), 1.0);
          return;
        }

        vec3 tex = (uHasTexture == 1) ? texture2D(uTex, vUV).rgb : vec3(1.0);
        vec3 lm_src = (uHasLightmap == 1) ? texture2D(uLightmapTex, vUV2).rgb : vColor;
        vec3 lm = mix(vec3(1.0), lm_src, uLightmapStrength);
        vec3 base = toLinear(lm) * toLinear(tex);
        vec3 lit = base * (uAmbient + ndl * 0.8 + ndl2 * 0.4);
        lit = min(lit, vec3(1.0));
        gl_FragColor = vec4(toSrgb(lit), 1.0);
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
      in vec3 vColor;
      in vec2 vUV;
      in vec2 vUV2;
      in vec3 vPos;
      uniform vec3 uLightDir;
      uniform vec3 uFillDir;
      uniform vec3 uAmbient;
      uniform float uLightmapStrength;
      uniform float uIsGround;
      uniform float uGridMode;
      uniform float uGridScale;
      uniform vec3 uGroundColor;
      uniform vec3 uGridColor;
      uniform vec3 uAxisColorX;
      uniform vec3 uAxisColorY;
      uniform vec3 uShadowCenter;
      uniform float uShadowRadius;
      uniform float uShadowStrength;
      uniform float uShadowSoftness;
      uniform float uIsBackground;
      uniform vec3 uBgTop;
      uniform vec3 uBgBottom;
      uniform sampler2D uTex;
      uniform sampler2D uLightmapTex;
      uniform int uHasTexture;
      uniform int uHasLightmap;
      out vec4 fragColor;

      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

      void main() {
        if (uIsBackground > 0.5) {
          float t = clamp(vUV.y, 0.0, 1.0);
          vec3 col = mix(uBgBottom, uBgTop, t);
          fragColor = vec4(col, 1.0);
          return;
        }
        vec3 n = normalize(vNormal);
        float ndl = abs(dot(n, normalize(uLightDir)));
        float ndl2 = abs(dot(n, normalize(uFillDir)));
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
            fragColor = vec4(toSrgb(col), 1.0);
            return;
          }
          vec3 groundLin = toLinear(uGroundColor);
          float gdiff = ndl * 0.5 + ndl2 * 0.2;
          vec3 ground = groundLin * (0.22 + gdiff);

          vec2 delta = vPos.xy - uShadowCenter.xy;
          float dist = length(delta) / max(0.001, uShadowRadius);
          float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
          shadow = clamp(shadow, 0.0, 0.85);
          ground *= (1.0 - shadow);
          fragColor = vec4(toSrgb(ground), 1.0);
          return;
        }

        vec3 tex = (uHasTexture == 1) ? texture(uTex, vUV).rgb : vec3(1.0);
        vec3 lm_src = (uHasLightmap == 1) ? texture(uLightmapTex, vUV2).rgb : vColor;
        vec3 lm = mix(vec3(1.0), lm_src, uLightmapStrength);
        vec3 base = toLinear(lm) * toLinear(tex);
        vec3 lit = base * (uAmbient + ndl * 0.8 + ndl2 * 0.4);
        lit = min(lit, vec3(1.0));
        fragColor = vec4(toSrgb(lit), 1.0);
      }
    )GLSL";
  }

  if (glsl_130) {
    return R"GLSL(
      #version 130
      in vec3 vNormal;
      in vec3 vColor;
      in vec2 vUV;
      in vec2 vUV2;
      in vec3 vPos;
      uniform vec3 uLightDir;
      uniform vec3 uFillDir;
      uniform vec3 uAmbient;
      uniform float uLightmapStrength;
      uniform float uIsGround;
      uniform float uGridMode;
      uniform float uGridScale;
      uniform vec3 uGroundColor;
      uniform vec3 uGridColor;
      uniform vec3 uAxisColorX;
      uniform vec3 uAxisColorY;
      uniform vec3 uShadowCenter;
      uniform float uShadowRadius;
      uniform float uShadowStrength;
      uniform float uShadowSoftness;
      uniform float uIsBackground;
      uniform vec3 uBgTop;
      uniform vec3 uBgBottom;
      uniform sampler2D uTex;
      uniform sampler2D uLightmapTex;
      uniform int uHasTexture;
      uniform int uHasLightmap;
      out vec4 fragColor;

      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

      void main() {
        if (uIsBackground > 0.5) {
          float t = clamp(vUV.y, 0.0, 1.0);
          vec3 col = mix(uBgBottom, uBgTop, t);
          fragColor = vec4(col, 1.0);
          return;
        }
        vec3 n = normalize(vNormal);
        float ndl = abs(dot(n, normalize(uLightDir)));
        float ndl2 = abs(dot(n, normalize(uFillDir)));
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
            fragColor = vec4(toSrgb(col), 1.0);
            return;
          }
          vec3 groundLin = toLinear(uGroundColor);
          float gdiff = ndl * 0.5 + ndl2 * 0.2;
          vec3 ground = groundLin * (0.22 + gdiff);

          vec2 delta = vPos.xy - uShadowCenter.xy;
          float dist = length(delta) / max(0.001, uShadowRadius);
          float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
          shadow = clamp(shadow, 0.0, 0.85);
          ground *= (1.0 - shadow);
          fragColor = vec4(toSrgb(ground), 1.0);
          return;
        }

        vec3 tex = (uHasTexture == 1) ? texture(uTex, vUV).rgb : vec3(1.0);
        vec3 lm_src = (uHasLightmap == 1) ? texture(uLightmapTex, vUV2).rgb : vColor;
        vec3 lm = mix(vec3(1.0), lm_src, uLightmapStrength);
        vec3 base = toLinear(lm) * toLinear(tex);
        vec3 lit = base * (uAmbient + ndl * 0.8 + ndl2 * 0.4);
        lit = min(lit, vec3(1.0));
        fragColor = vec4(toSrgb(lit), 1.0);
      }
    )GLSL";
  }

  return R"GLSL(
    #version 120
    varying vec3 vNormal;
    varying vec3 vColor;
    varying vec2 vUV;
    varying vec2 vUV2;
    varying vec3 vPos;
    uniform vec3 uLightDir;
    uniform vec3 uFillDir;
    uniform vec3 uAmbient;
    uniform float uLightmapStrength;
    uniform float uIsGround;
    uniform float uGridMode;
    uniform float uGridScale;
    uniform vec3 uGroundColor;
    uniform vec3 uGridColor;
    uniform vec3 uAxisColorX;
    uniform vec3 uAxisColorY;
    uniform vec3 uShadowCenter;
    uniform float uShadowRadius;
    uniform float uShadowStrength;
    uniform float uShadowSoftness;
    uniform float uIsBackground;
    uniform vec3 uBgTop;
    uniform vec3 uBgBottom;
    uniform sampler2D uTex;
    uniform sampler2D uLightmapTex;
    uniform int uHasTexture;
    uniform int uHasLightmap;

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
      float ndl = abs(dot(n, normalize(uLightDir)));
      float ndl2 = abs(dot(n, normalize(uFillDir)));
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
        float gdiff = ndl * 0.5 + ndl2 * 0.2;
        vec3 ground = groundLin * (0.22 + gdiff);

        vec2 delta = vPos.xy - uShadowCenter.xy;
        float dist = length(delta) / max(0.001, uShadowRadius);
        float shadow = exp(-dist * dist * uShadowSoftness) * uShadowStrength;
        shadow = clamp(shadow, 0.0, 0.85);
        ground *= (1.0 - shadow);
        gl_FragColor = vec4(toSrgb(ground), 1.0);
        return;
      }

      vec3 tex = (uHasTexture == 1) ? texture2D(uTex, vUV).rgb : vec3(1.0);
      vec3 lm_src = (uHasLightmap == 1) ? texture2D(uLightmapTex, vUV2).rgb : vColor;
      vec3 lm = mix(vec3(1.0), lm_src, uLightmapStrength);
      vec3 base = toLinear(lm) * toLinear(tex);
      vec3 lit = base * (uAmbient + ndl * 0.8 + ndl2 * 0.4);
      lit = min(lit, vec3(1.0));
      gl_FragColor = vec4(toSrgb(lit), 1.0);
    }
  )GLSL";
}
}  // namespace

BspPreviewWidget::BspPreviewWidget(QWidget* parent) : QOpenGLWidget(parent) {
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
}

BspPreviewWidget::~BspPreviewWidget() {
  clear();
}

void BspPreviewWidget::set_mesh(BspMesh mesh, QHash<QString, QImage> textures) {
  if (gl_ready_ && context()) {
    makeCurrent();
    destroy_gl_resources();
    doneCurrent();
  }

  mesh_ = std::move(mesh);
  has_mesh_ = !mesh_.vertices.isEmpty() && !mesh_.indices.isEmpty();

  textures_.clear();
  if (!textures.isEmpty()) {
    textures_.reserve(textures.size());
    for (auto it = textures.begin(); it != textures.end(); ++it) {
      const QString key = it.key().toLower();
      textures_.insert(key, it.value());
    }
  }

  surfaces_.clear();
  surfaces_.reserve(mesh_.surfaces.size());
  for (const BspMeshSurface& s : mesh_.surfaces) {
    DrawSurface ds;
    ds.first_index = s.first_index;
    ds.index_count = s.index_count;
    ds.texture = s.texture;
    ds.uv_normalized = s.uv_normalized;
    ds.lightmap_index = s.lightmap_index;
    surfaces_.push_back(ds);
  }

  pending_upload_ = has_mesh_;
  pending_texture_upload_ = has_mesh_;
  reset_camera_from_mesh();
  camera_fit_pending_ = has_mesh_;
  update();
}

void BspPreviewWidget::set_lightmap_enabled(bool enabled) {
  if (lightmap_enabled_ == enabled) {
    return;
  }
  lightmap_enabled_ = enabled;
  update();
}

void BspPreviewWidget::set_grid_mode(PreviewGridMode mode) {
  if (grid_mode_ == mode) {
    return;
  }
  grid_mode_ = mode;
  ground_extent_ = 0.0f;
  update();
}

void BspPreviewWidget::set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color) {
  if (bg_mode_ == mode && bg_custom_color_ == custom_color) {
    return;
  }
  bg_mode_ = mode;
  bg_custom_color_ = custom_color;
  update();
}

void BspPreviewWidget::set_wireframe_enabled(bool enabled) {
  if (wireframe_enabled_ == enabled) {
    return;
  }
  wireframe_enabled_ = enabled;
  update();
}

void BspPreviewWidget::set_textured_enabled(bool enabled) {
  if (textured_enabled_ == enabled) {
    return;
  }
  textured_enabled_ = enabled;
  update();
}

void BspPreviewWidget::set_fov_degrees(int degrees) {
  const float clamped = std::clamp(static_cast<float>(degrees), 40.0f, 120.0f);
  if (std::abs(clamped - fov_y_deg_) < 0.001f) {
    return;
  }
  fov_y_deg_ = clamped;
  ground_extent_ = 0.0f;
  update();
}

PreviewCameraState BspPreviewWidget::camera_state() const {
  PreviewCameraState state;
  state.center = center_;
  state.yaw_deg = yaw_deg_;
  state.pitch_deg = pitch_deg_;
  state.distance = distance_;
  state.valid = true;
  return state;
}

void BspPreviewWidget::set_camera_state(const PreviewCameraState& state) {
  if (!state.valid) {
    return;
  }
  center_ = state.center;
  yaw_deg_ = std::remainder(state.yaw_deg, 360.0f);
  pitch_deg_ = std::clamp(state.pitch_deg, -89.0f, 89.0f);
  distance_ = std::clamp(state.distance, orbit_min_distance(radius_), orbit_max_distance(radius_));
  camera_fit_pending_ = false;
  ground_extent_ = 0.0f;
  update();
}

void BspPreviewWidget::clear() {
  has_mesh_ = false;
  camera_fit_pending_ = false;
  pending_upload_ = false;
  pending_texture_upload_ = false;
  textures_.clear();
  surfaces_.clear();
  lightmap_textures_.clear();
  mesh_ = BspMesh{};
  if (gl_ready_ && context()) {
    makeCurrent();
    destroy_gl_resources();
    doneCurrent();
  }
  update();
}

void BspPreviewWidget::initializeGL() {
  initializeOpenGLFunctions();
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  gl_ready_ = true;
  // Reparenting (e.g. fullscreen toggle) can recreate the GL context.
  // Reset GPU handles and force a fresh upload for the new context.
  destroy_gl_resources();
  pending_upload_ = has_mesh_;
  pending_texture_upload_ = has_mesh_;
  ensure_program();
  upload_mesh_if_possible();
}

void BspPreviewWidget::paintGL() {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!gl_ready_) {
    return;
  }

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

  if (camera_fit_pending_ && has_mesh_ && width() > 0 && height() > 0) {
    frame_mesh();
    camera_fit_pending_ = false;
  }

  const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
  const float near_plane = std::max(radius_ * 0.01f, 0.01f);
  const float far_plane = std::max(radius_ * 200.0f, near_plane + 10.0f);

  QMatrix4x4 proj;
  proj.perspective(fov_y_deg_, aspect, near_plane, far_plane);

  const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
  const QVector3D cam_pos = center_ + dir * distance_;
  const QVector3D view_target = center_;

  QMatrix4x4 view;
  view.lookAt(cam_pos, view_target, QVector3D(0, 0, 1));

  QMatrix4x4 model;
  model.setToIdentity();

  const QMatrix4x4 mvp = proj * view * model;

  program_.bind();
  program_.setUniformValue("uMvp", QMatrix4x4());
  program_.setUniformValue("uModel", QMatrix4x4());
  program_.setUniformValue("uIsBackground", 1.0f);
  program_.setUniformValue("uIsGround", 0.0f);
  program_.setUniformValue("uBgTop", bg_top);
  program_.setUniformValue("uBgBottom", bg_bottom);
  program_.setUniformValue("uHasTexture", 0);
  program_.setUniformValue("uHasLightmap", 0);
  program_.setUniformValue("uTex", 0);
  program_.setUniformValue("uLightmapTex", 1);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  if (bg_vao_.isCreated()) {
    bg_vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    bg_vao_.release();
  }
  glEnable(GL_DEPTH_TEST);

  if (!has_mesh_) {
    program_.release();
    return;
  }

  if (pending_upload_) {
    upload_mesh_if_possible();
  }
  if (pending_texture_upload_) {
    upload_textures_if_possible();
  }

  if (index_count_ <= 0 || !vao_.isCreated() || !vbo_.isCreated() || !ibo_.isCreated()) {
    if (has_mesh_ && !pending_upload_) {
      pending_upload_ = true;
      upload_mesh_if_possible();
    }
  }

  if (index_count_ <= 0 || !vao_.isCreated() || !vbo_.isCreated() || !ibo_.isCreated()) {
    program_.release();
    return;
  }

  program_.setUniformValue("uMvp", mvp);
  program_.setUniformValue("uModel", model);
  program_.setUniformValue("uLightDir", QVector3D(-0.35f, -0.6f, 0.75f));
  program_.setUniformValue("uFillDir", QVector3D(0.75f, 0.2f, 0.45f));
  program_.setUniformValue("uAmbient", QVector3D(0.35f, 0.35f, 0.35f));
  program_.setUniformValue("uLightmapStrength", lightmap_enabled_ ? 1.0f : 0.0f);
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

  glActiveTexture(GL_TEXTURE0);
  program_.setUniformValue("uTex", 0);
  glActiveTexture(GL_TEXTURE1);
  program_.setUniformValue("uLightmapTex", 1);
  glActiveTexture(GL_TEXTURE0);
  update_ground_mesh_if_needed();

  apply_wireframe_state(wireframe_enabled_);

  if (grid_mode_ != PreviewGridMode::None && ground_index_count_ > 0 && ground_vbo_.isCreated() && ground_ibo_.isCreated()) {
    program_.setUniformValue("uIsGround", 1.0f);
    program_.setUniformValue("uHasTexture", 0);
    program_.setUniformValue("uHasLightmap", 0);
    program_.setUniformValue("uTexScale", QVector2D(1.0f, 1.0f));
    program_.setUniformValue("uTexOffset", QVector2D(0.0f, 0.0f));
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
    const int col_loc = program_.attributeLocation("aColor");
    const int uv_loc = program_.attributeLocation("aUV");
    const int uv2_loc = program_.attributeLocation("aUV2");
    program_.enableAttributeArray(pos_loc);
    program_.enableAttributeArray(nrm_loc);
    program_.enableAttributeArray(col_loc);
    program_.enableAttributeArray(uv_loc);
    program_.enableAttributeArray(uv2_loc);
    program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
    program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
    program_.setAttributeBuffer(col_loc, GL_FLOAT, offsetof(GpuVertex, r), 3, sizeof(GpuVertex));
    program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));
    program_.setAttributeBuffer(uv2_loc, GL_FLOAT, offsetof(GpuVertex, lu), 2, sizeof(GpuVertex));

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
  const int col_loc = program_.attributeLocation("aColor");
  const int uv_loc = program_.attributeLocation("aUV");
  const int uv2_loc = program_.attributeLocation("aUV2");
  program_.enableAttributeArray(pos_loc);
  program_.enableAttributeArray(nrm_loc);
  program_.enableAttributeArray(col_loc);
  program_.enableAttributeArray(uv_loc);
  program_.enableAttributeArray(uv2_loc);
  program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(col_loc, GL_FLOAT, offsetof(GpuVertex, r), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv2_loc, GL_FLOAT, offsetof(GpuVertex, lu), 2, sizeof(GpuVertex));

  if (surfaces_.isEmpty()) {
    program_.setUniformValue("uHasTexture", 0);
    program_.setUniformValue("uHasLightmap", 0);
    program_.setUniformValue("uTexScale", QVector2D(1.0f, 1.0f));
    program_.setUniformValue("uTexOffset", QVector2D(0.0f, 0.0f));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  } else {
    for (const DrawSurface& s : surfaces_) {
      if (s.first_index < 0 || s.index_count <= 0 || (s.first_index + s.index_count) > index_count_) {
        continue;
      }
      const bool use_tex = textured_enabled_ && s.has_texture;
      const bool use_lightmap = lightmap_enabled_ && s.has_lightmap;
      program_.setUniformValue("uHasTexture", use_tex ? 1 : 0);
      program_.setUniformValue("uHasLightmap", use_lightmap ? 1 : 0);
      program_.setUniformValue("uTexScale", s.tex_scale);
      program_.setUniformValue("uTexOffset", s.tex_offset);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, use_tex ? s.texture_id : 0);
      glActiveTexture(GL_TEXTURE1);
      GLuint lm_tex = 0;
      if (use_lightmap && s.lightmap_index >= 0 && s.lightmap_index < lightmap_textures_.size()) {
        lm_tex = lightmap_textures_[s.lightmap_index];
      }
      glBindTexture(GL_TEXTURE_2D, lm_tex);
      glActiveTexture(GL_TEXTURE0);
      const uintptr_t offs = static_cast<uintptr_t>(s.first_index) * sizeof(std::uint32_t);
      glDrawElements(GL_TRIANGLES, s.index_count, GL_UNSIGNED_INT, reinterpret_cast<const void*>(offs));
    }
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  vbo_.release();
  ibo_.release();
  if (vao_bound) {
    vao_.release();
  }
  apply_wireframe_state(false);
  program_.release();
}

void BspPreviewWidget::resizeGL(int, int) {
  if (camera_fit_pending_ && has_mesh_ && width() > 0 && height() > 0) {
    frame_mesh();
    camera_fit_pending_ = false;
  }
  update();
}

void BspPreviewWidget::mousePressEvent(QMouseEvent* event) {
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

void BspPreviewWidget::mouseMoveEvent(QMouseEvent* event) {
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

void BspPreviewWidget::mouseReleaseEvent(QMouseEvent* event) {
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

void BspPreviewWidget::wheelEvent(QWheelEvent* event) {
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

void BspPreviewWidget::reset_camera_from_mesh() {
  yaw_deg_ = 45.0f;
  pitch_deg_ = 55.0f;
  camera_fit_pending_ = false;
  frame_mesh();
}

void BspPreviewWidget::keyPressEvent(QKeyEvent* event) {
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

void BspPreviewWidget::keyReleaseEvent(QKeyEvent* event) {
  if (!event) {
    QOpenGLWidget::keyReleaseEvent(event);
    return;
  }
  QOpenGLWidget::keyReleaseEvent(event);
}

void BspPreviewWidget::frame_mesh() {
  if (!has_mesh_) {
    center_ = QVector3D(0, 0, 0);
    radius_ = 1.0f;
    distance_ = 3.0f;
    ground_z_ = 0.0f;
    ground_extent_ = 0.0f;
    return;
  }
  const QVector3D mins = mesh_.mins;
  const QVector3D maxs = mesh_.maxs;
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

void BspPreviewWidget::pan_by_pixels(const QPoint& delta) {
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
  ground_extent_ = 0.0f;
}

void BspPreviewWidget::dolly_by_pixels(const QPoint& delta) {
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

void BspPreviewWidget::upload_mesh_if_possible() {
  if (!gl_ready_ || !has_mesh_) {
    return;
  }

  ensure_program();
  if (!program_.isLinked()) {
    return;
  }

  if (!vao_.isCreated()) {
    vao_.create();
  }
  if (!vbo_.isCreated()) {
    vbo_.create();
  }
  if (!ibo_.isCreated()) {
    ibo_.create();
  }

  QVector<GpuVertex> verts;
  verts.reserve(mesh_.vertices.size());
  for (const BspMeshVertex& v : mesh_.vertices) {
    GpuVertex gv;
    gv.px = v.pos.x();
    gv.py = v.pos.y();
    gv.pz = v.pos.z();
    gv.nx = v.normal.x();
    gv.ny = v.normal.y();
    gv.nz = v.normal.z();
    gv.r = v.color.redF();
    gv.g = v.color.greenF();
    gv.b = v.color.blueF();
    gv.u = v.uv.x();
    gv.v = v.uv.y();
    gv.lu = v.lightmap_uv.x();
    gv.lv = v.lightmap_uv.y();
    verts.push_back(gv);
  }

  vao_.bind();
  vbo_.bind();
  vbo_.allocate(verts.constData(), verts.size() * static_cast<int>(sizeof(GpuVertex)));
  ibo_.bind();
  ibo_.allocate(mesh_.indices.constData(), mesh_.indices.size() * static_cast<int>(sizeof(std::uint32_t)));

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, px)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, nx)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, r)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, u)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<void*>(offsetof(GpuVertex, lu)));

  vao_.release();
  vbo_.release();
  ibo_.release();

  index_count_ = mesh_.indices.size();
  pending_upload_ = false;
  upload_textures_if_possible();
}

void BspPreviewWidget::upload_textures_if_possible() {
  if (!pending_texture_upload_ || !gl_ready_ || !context()) {
    return;
  }

  auto delete_tex = [&](GLuint* id) {
    if (id && *id != 0) {
      glDeleteTextures(1, id);
      *id = 0;
    }
  };

  for (DrawSurface& s : surfaces_) {
    delete_tex(&s.texture_id);
    s.has_texture = false;
    s.has_lightmap = false;
    s.tex_scale = QVector2D(1.0f, 1.0f);
    s.tex_offset = QVector2D(0.0f, 0.0f);
  }
  for (GLuint& id : lightmap_textures_) {
    delete_tex(&id);
  }
  lightmap_textures_.clear();

  auto upload = [&](const QImage& src, GLuint* out_id, bool flip_vertical, GLint wrap_mode) -> bool {
    if (!out_id) {
      return false;
    }
    *out_id = 0;
    if (src.isNull()) {
      return false;
    }
    QImage img = src.convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull()) {
      return false;
    }
    if (flip_vertical) {
      img = img.flipped(Qt::Vertical);
      if (img.isNull()) {
        return false;
      }
    }
    glGenTextures(1, out_id);
    if (*out_id == 0) {
      return false;
    }
    glBindTexture(GL_TEXTURE_2D, *out_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
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

  for (DrawSurface& s : surfaces_) {
    const QString key = s.texture.toLower();
    const QImage img = textures_.value(key);
    if (!img.isNull() && upload(img, &s.texture_id, true, GL_REPEAT)) {
      s.has_texture = true;
      if (s.uv_normalized) {
        s.tex_scale = QVector2D(1.0f, 1.0f);
        s.tex_offset = QVector2D(0.0f, 0.0f);
      } else {
        const float w = std::max(1, img.width());
        const float h = std::max(1, img.height());
        s.tex_scale = QVector2D(1.0f / w, 1.0f / h);
        s.tex_offset = QVector2D(0.0f, 0.0f);
      }
    }
  }

  lightmap_textures_.resize(mesh_.lightmaps.size());
  for (int i = 0; i < mesh_.lightmaps.size(); ++i) {
    GLuint lm_id = 0;
    if (upload(mesh_.lightmaps[i], &lm_id, false, GL_CLAMP_TO_EDGE)) {
      lightmap_textures_[i] = lm_id;
    } else {
      lightmap_textures_[i] = 0;
    }
  }

  for (DrawSurface& s : surfaces_) {
    if (s.lightmap_index < 0 || s.lightmap_index >= lightmap_textures_.size()) {
      s.has_lightmap = false;
      continue;
    }
    s.has_lightmap = (lightmap_textures_[s.lightmap_index] != 0);
  }

  pending_texture_upload_ = false;
}

void BspPreviewWidget::destroy_gl_resources() {
  index_count_ = 0;
  ground_index_count_ = 0;
  for (DrawSurface& s : surfaces_) {
    if (s.texture_id != 0) {
      glDeleteTextures(1, &s.texture_id);
      s.texture_id = 0;
      s.has_texture = false;
    }
    s.has_lightmap = false;
  }
  for (GLuint& id : lightmap_textures_) {
    if (id != 0) {
      glDeleteTextures(1, &id);
      id = 0;
    }
  }
  lightmap_textures_.clear();
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
  if (vao_.isCreated()) {
    vao_.destroy();
  }
  if (bg_vao_.isCreated()) {
    bg_vao_.destroy();
  }
  program_.removeAllShaders();
}

void BspPreviewWidget::ensure_program() {
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
  program_.bindAttributeLocation("aColor", 2);
  program_.bindAttributeLocation("aUV", 3);
  program_.bindAttributeLocation("aUV2", 4);

  if (!vs_ok || !fs_ok || !program_.link()) {
    qWarning() << "BspPreviewWidget shader compile/link failed:" << program_.log();
  }
}

void BspPreviewWidget::update_ground_mesh_if_needed() {
  if (!has_mesh_ || !gl_ready_ || !context()) {
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
  verts.push_back(GpuVertex{minx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
  verts.push_back(GpuVertex{maxx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f});
  verts.push_back(GpuVertex{maxx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
  verts.push_back(GpuVertex{minx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f});

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

void BspPreviewWidget::update_background_mesh_if_needed() {
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
    {-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    { 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    { 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    {-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    { 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    {-1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
  };

  bg_vao_.bind();
  bg_vbo_.bind();
  bg_vbo_.allocate(verts, static_cast<int>(sizeof(verts)));

  const int pos_loc = program_.attributeLocation("aPos");
  const int nrm_loc = program_.attributeLocation("aNormal");
  const int col_loc = program_.attributeLocation("aColor");
  const int uv_loc = program_.attributeLocation("aUV");
  const int uv2_loc = program_.attributeLocation("aUV2");
  program_.enableAttributeArray(pos_loc);
  program_.enableAttributeArray(nrm_loc);
  program_.enableAttributeArray(col_loc);
  program_.enableAttributeArray(uv_loc);
  program_.enableAttributeArray(uv2_loc);
  program_.setAttributeBuffer(pos_loc, GL_FLOAT, offsetof(GpuVertex, px), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(nrm_loc, GL_FLOAT, offsetof(GpuVertex, nx), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(col_loc, GL_FLOAT, offsetof(GpuVertex, r), 3, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv_loc, GL_FLOAT, offsetof(GpuVertex, u), 2, sizeof(GpuVertex));
  program_.setAttributeBuffer(uv2_loc, GL_FLOAT, offsetof(GpuVertex, lu), 2, sizeof(GpuVertex));

  bg_vao_.release();
  bg_vbo_.release();
  program_.release();
}

void BspPreviewWidget::update_grid_settings() {
  const float reference = std::max(distance_, radius_ * 0.25f);
  grid_scale_ = quantized_grid_scale(reference);
}

void BspPreviewWidget::apply_wireframe_state(bool enabled) {
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

void BspPreviewWidget::update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const {
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

void BspPreviewWidget::update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const {
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

