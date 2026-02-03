#include "ui/bsp_preview_widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPalette>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QDebug>
#include <QtGui/QOpenGLContext>

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

QString vertex_shader_source(const QSurfaceFormat& fmt) {
  if (QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES()) {
    return R"GLSL(
      attribute highp vec3 aPos;
      attribute highp vec3 aNormal;
      attribute highp vec3 aColor;
      attribute highp vec2 aUV;
      uniform highp mat4 uMvp;
      uniform highp mat4 uModel;
      uniform highp vec2 uTexScale;
      uniform highp vec2 uTexOffset;
      varying highp vec3 vNormal;
      varying highp vec3 vColor;
      varying highp vec2 vUV;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vColor = aColor;
        vUV = aUV * uTexScale + uTexOffset;
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
      uniform mat4 uMvp;
      uniform mat4 uModel;
      uniform vec2 uTexScale;
      uniform vec2 uTexOffset;
      out vec3 vNormal;
      out vec3 vColor;
      out vec2 vUV;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vColor = aColor;
        vUV = aUV * uTexScale + uTexOffset;
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
      uniform mat4 uMvp;
      uniform mat4 uModel;
      uniform vec2 uTexScale;
      uniform vec2 uTexOffset;
      out vec3 vNormal;
      out vec3 vColor;
      out vec2 vUV;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
        vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
        vColor = aColor;
        vUV = aUV * uTexScale + uTexOffset;
      }
    )GLSL";
  }

  return R"GLSL(
    #version 120
    attribute vec3 aPos;
    attribute vec3 aNormal;
    attribute vec3 aColor;
    attribute vec2 aUV;
    uniform mat4 uMvp;
    uniform mat4 uModel;
    uniform vec2 uTexScale;
    uniform vec2 uTexOffset;
    varying vec3 vNormal;
    varying vec3 vColor;
    varying vec2 vUV;
    void main() {
      gl_Position = uMvp * vec4(aPos, 1.0);
      vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
      vColor = aColor;
      vUV = aUV * uTexScale + uTexOffset;
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
      uniform mediump vec3 uLightDir;
      uniform mediump vec3 uFillDir;
      uniform mediump vec3 uAmbient;
      uniform sampler2D uTex;
      uniform int uHasTexture;

      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

      void main() {
        vec3 n = normalize(vNormal);
        float ndl = abs(dot(n, normalize(uLightDir)));
        float ndl2 = abs(dot(n, normalize(uFillDir)));
        vec3 tex = (uHasTexture == 1) ? texture2D(uTex, vUV).rgb : vec3(1.0);
        vec3 base = toLinear(vColor) * toLinear(tex);
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
      uniform vec3 uLightDir;
      uniform vec3 uFillDir;
      uniform vec3 uAmbient;
      uniform sampler2D uTex;
      uniform int uHasTexture;
      out vec4 fragColor;

      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

      void main() {
        vec3 n = normalize(vNormal);
        float ndl = abs(dot(n, normalize(uLightDir)));
        float ndl2 = abs(dot(n, normalize(uFillDir)));
        vec3 tex = (uHasTexture == 1) ? texture(uTex, vUV).rgb : vec3(1.0);
        vec3 base = toLinear(vColor) * toLinear(tex);
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
      uniform vec3 uLightDir;
      uniform vec3 uFillDir;
      uniform vec3 uAmbient;
      uniform sampler2D uTex;
      uniform int uHasTexture;
      out vec4 fragColor;

      vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
      vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

      void main() {
        vec3 n = normalize(vNormal);
        float ndl = abs(dot(n, normalize(uLightDir)));
        float ndl2 = abs(dot(n, normalize(uFillDir)));
        vec3 tex = (uHasTexture == 1) ? texture(uTex, vUV).rgb : vec3(1.0);
        vec3 base = toLinear(vColor) * toLinear(tex);
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
    uniform vec3 uLightDir;
    uniform vec3 uFillDir;
    uniform vec3 uAmbient;
    uniform sampler2D uTex;
    uniform int uHasTexture;

    vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
    vec3 toSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

    void main() {
      vec3 n = normalize(vNormal);
      float ndl = abs(dot(n, normalize(uLightDir)));
      float ndl2 = abs(dot(n, normalize(uFillDir)));
      vec3 tex = (uHasTexture == 1) ? texture2D(uTex, vUV).rgb : vec3(1.0);
      vec3 base = toLinear(vColor) * toLinear(tex);
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
    surfaces_.push_back(ds);
  }

  pending_upload_ = has_mesh_;
  pending_texture_upload_ = has_mesh_;
  reset_camera_from_mesh();
  update();
}

void BspPreviewWidget::clear() {
  has_mesh_ = false;
  pending_upload_ = false;
  pending_texture_upload_ = false;
  textures_.clear();
  surfaces_.clear();
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
  ensure_program();
  upload_mesh_if_possible();
}

void BspPreviewWidget::paintGL() {
  const QColor base = palette().color(QPalette::Window);
  glClearColor(base.redF(), base.greenF(), base.blueF(), 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!gl_ready_ || !has_mesh_) {
    return;
  }

  if (pending_upload_) {
    upload_mesh_if_possible();
  }
  if (pending_texture_upload_) {
    upload_textures_if_possible();
  }

  if (index_count_ <= 0) {
    return;
  }

  ensure_program();
  if (!program_.isLinked()) {
    return;
  }

  const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
  const float near_plane = std::max(radius_ * 0.01f, 0.01f);
  const float far_plane = std::max(radius_ * 200.0f, near_plane + 10.0f);

  QMatrix4x4 proj;
  proj.perspective(45.0f, aspect, near_plane, far_plane);

  const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
  const QVector3D cam_pos = center_ + dir * distance_;

  QMatrix4x4 view;
  view.lookAt(cam_pos, center_, QVector3D(0, 0, 1));

  QMatrix4x4 model;
  model.setToIdentity();

  const QMatrix4x4 mvp = proj * view * model;

  program_.bind();
  program_.setUniformValue("uMvp", mvp);
  program_.setUniformValue("uModel", model);
  program_.setUniformValue("uLightDir", QVector3D(-0.35f, -0.6f, 0.75f));
  program_.setUniformValue("uFillDir", QVector3D(0.75f, 0.2f, 0.45f));
  program_.setUniformValue("uAmbient", QVector3D(0.35f, 0.35f, 0.35f));

  vao_.bind();
  glActiveTexture(GL_TEXTURE0);
  program_.setUniformValue("uTex", 0);
  if (surfaces_.isEmpty()) {
    program_.setUniformValue("uHasTexture", 0);
    program_.setUniformValue("uTexScale", QVector2D(1.0f, 1.0f));
    program_.setUniformValue("uTexOffset", QVector2D(0.0f, 0.0f));
    glBindTexture(GL_TEXTURE_2D, 0);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  } else {
    for (const DrawSurface& s : surfaces_) {
      program_.setUniformValue("uHasTexture", s.has_texture ? 1 : 0);
      program_.setUniformValue("uTexScale", s.tex_scale);
      program_.setUniformValue("uTexOffset", s.tex_offset);
      glBindTexture(GL_TEXTURE_2D, s.has_texture ? s.texture_id : 0);
      const uintptr_t offs = static_cast<uintptr_t>(s.first_index) * sizeof(std::uint32_t);
      glDrawElements(GL_TRIANGLES, s.index_count, GL_UNSIGNED_INT, reinterpret_cast<const void*>(offs));
    }
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  vao_.release();
  program_.release();
}

void BspPreviewWidget::resizeGL(int, int) {
  update();
}

void BspPreviewWidget::mousePressEvent(QMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) {
    last_mouse_pos_ = event->pos();
    event->accept();
    return;
  }
  QOpenGLWidget::mousePressEvent(event);
}

void BspPreviewWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!event || !(event->buttons() & Qt::LeftButton)) {
    QOpenGLWidget::mouseMoveEvent(event);
    return;
  }

  const QPoint delta = event->pos() - last_mouse_pos_;
  last_mouse_pos_ = event->pos();

  yaw_deg_ += static_cast<float>(delta.x()) * 0.6f;
  pitch_deg_ += static_cast<float>(-delta.y()) * 0.6f;
  pitch_deg_ = std::clamp(pitch_deg_, -89.0f, 89.0f);

  update();
  event->accept();
}

void BspPreviewWidget::wheelEvent(QWheelEvent* event) {
  if (!event) {
    return;
  }
  const QPoint num_deg = event->angleDelta() / 8;
  if (!num_deg.isNull()) {
    const float steps = static_cast<float>(num_deg.y()) / 15.0f;
    const float factor = std::pow(0.85f, steps);
    distance_ *= factor;
    distance_ = std::clamp(distance_, radius_ * 0.4f, radius_ * 50.0f);
    update();
    event->accept();
    return;
  }
  QOpenGLWidget::wheelEvent(event);
}

void BspPreviewWidget::reset_camera_from_mesh() {
  if (!has_mesh_) {
    center_ = QVector3D(0, 0, 0);
    radius_ = 1.0f;
    distance_ = 3.0f;
    yaw_deg_ = 45.0f;
    pitch_deg_ = 55.0f;
    return;
  }
  const QVector3D mins = mesh_.mins;
  const QVector3D maxs = mesh_.maxs;
  center_ = (mins + maxs) * 0.5f;
  radius_ = std::max(0.001f, (maxs - mins).length() * 0.5f);
  distance_ = std::max(radius_ * 2.8f, 1.0f);
  yaw_deg_ = 45.0f;
  pitch_deg_ = 55.0f;
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
    s.tex_scale = QVector2D(1.0f, 1.0f);
    s.tex_offset = QVector2D(0.0f, 0.0f);
  }

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

  for (DrawSurface& s : surfaces_) {
    const QString key = s.texture.toLower();
    const QImage img = textures_.value(key);
    if (!img.isNull() && upload(img, &s.texture_id)) {
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

  pending_texture_upload_ = false;
}

void BspPreviewWidget::destroy_gl_resources() {
  index_count_ = 0;
  for (DrawSurface& s : surfaces_) {
    if (s.texture_id != 0) {
      glDeleteTextures(1, &s.texture_id);
      s.texture_id = 0;
      s.has_texture = false;
    }
  }
  if (vbo_.isCreated()) {
    vbo_.destroy();
  }
  if (ibo_.isCreated()) {
    ibo_.destroy();
  }
  if (vao_.isCreated()) {
    vao_.destroy();
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

  if (!vs_ok || !fs_ok || !program_.link()) {
    qWarning() << "BspPreviewWidget shader compile/link failed:" << program_.log();
  }
}
