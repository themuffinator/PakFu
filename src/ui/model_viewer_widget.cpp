#include "ui/model_viewer_widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QDebug>
#include <QtGui/QOpenGLContext>

#include "formats/image_loader.h"
#include "formats/model.h"

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
      attribute highp vec2 aUV;
      uniform highp mat4 uMvp;
      uniform highp mat4 uModel;
      varying highp vec3 vNormal;
      varying highp vec2 vUV;
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
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
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
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
      void main() {
        gl_Position = uMvp * vec4(aPos, 1.0);
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
    void main() {
      gl_Position = uMvp * vec4(aPos, 1.0);
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
      uniform mediump vec3 uLightDir;
      uniform mediump vec3 uBaseColor;
      uniform sampler2D uTex;
      uniform int uHasTex;
      void main() {
        vec3 n = normalize(vNormal);
        float ndl = max(dot(n, normalize(uLightDir)), 0.0);
        vec4 tex = (uHasTex != 0) ? texture2D(uTex, vUV) : vec4(uBaseColor, 1.0);
        vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
        vec3 c = base * (0.22 + 0.78 * ndl);
        gl_FragColor = vec4(c, tex.a);
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
      uniform vec3 uLightDir;
      uniform vec3 uBaseColor;
      uniform sampler2D uTex;
      uniform int uHasTex;
      out vec4 FragColor;
      void main() {
        vec3 n = normalize(vNormal);
        float ndl = max(dot(n, normalize(uLightDir)), 0.0);
        vec4 tex = (uHasTex != 0) ? texture(uTex, vUV) : vec4(uBaseColor, 1.0);
        vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
        vec3 c = base * (0.22 + 0.78 * ndl);
        FragColor = vec4(c, tex.a);
      }
    )GLSL";
  }

  if (glsl_130) {
    return R"GLSL(
      #version 130
      in vec3 vNormal;
      in vec2 vUV;
      uniform vec3 uLightDir;
      uniform vec3 uBaseColor;
      uniform sampler2D uTex;
      uniform int uHasTex;
      out vec4 FragColor;
      void main() {
        vec3 n = normalize(vNormal);
        float ndl = max(dot(n, normalize(uLightDir)), 0.0);
        vec4 tex = (uHasTex != 0) ? texture2D(uTex, vUV) : vec4(uBaseColor, 1.0);
        vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
        vec3 c = base * (0.22 + 0.78 * ndl);
        FragColor = vec4(c, tex.a);
      }
    )GLSL";
  }

  return R"GLSL(
    #version 120
    varying vec3 vNormal;
    varying vec2 vUV;
    uniform vec3 uLightDir;
    uniform vec3 uBaseColor;
    uniform sampler2D uTex;
    uniform int uHasTex;
    void main() {
      vec3 n = normalize(vNormal);
      float ndl = max(dot(n, normalize(uLightDir)), 0.0);
      vec4 tex = (uHasTex != 0) ? texture2D(uTex, vUV) : vec4(uBaseColor, 1.0);
      vec3 base = (uHasTex != 0) ? tex.rgb : uBaseColor;
      vec3 c = base * (0.22 + 0.78 * ndl);
      gl_FragColor = vec4(c, tex.a);
    }
  )GLSL";
}
}  // namespace

ModelViewerWidget::ModelViewerWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setMinimumHeight(240);
  setFocusPolicy(Qt::StrongFocus);
}

ModelViewerWidget::~ModelViewerWidget() {
  unload();
}

bool ModelViewerWidget::load_file(const QString& file_path, QString* error) {
  return load_file(file_path, QString(), error);
}

bool ModelViewerWidget::load_file(const QString& file_path, const QString& skin_path, QString* error) {
  if (error) {
    error->clear();
  }

  QString err;
  model_ = load_model_file(file_path, &err);
  if (!model_) {
    if (error) {
      *error = err.isEmpty() ? "Unable to load model." : err;
    }
    unload();
    return false;
  }

  skin_image_ = {};
  has_texture_ = false;
  pending_texture_upload_ = false;
  if (!skin_path.isEmpty()) {
    const ImageDecodeResult decoded = decode_image_file(skin_path, ImageDecodeOptions{});
    if (decoded.ok()) {
      skin_image_ = decoded.image;
      pending_texture_upload_ = true;
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
  index_count_ = 0;
  index_type_ = GL_UNSIGNED_INT;
  pending_upload_ = false;
  pending_texture_upload_ = false;
  skin_image_ = {};
  has_texture_ = false;
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
  ensure_program();
  upload_mesh_if_possible();
}

void ModelViewerWidget::paintGL() {
  glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  ensure_program();
  if (!model_ || index_count_ <= 0 || !program_.isLinked()) {
    return;
  }

  program_.bind();

  QMatrix4x4 proj;
  const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
  const float near_plane = std::max(0.001f, radius_ * 0.02f);
  const float far_plane = std::max(10.0f, radius_ * 50.0f);
  proj.perspective(45.0f, aspect, near_plane, far_plane);

  const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
  const QVector3D cam_pos = center_ + dir * distance_;

  QMatrix4x4 view;
  view.lookAt(cam_pos, center_, QVector3D(0, 0, 1));

  QMatrix4x4 model_m;
  model_m.setToIdentity();

  const QMatrix4x4 mvp = proj * view * model_m;

  program_.setUniformValue("uMvp", mvp);
  program_.setUniformValue("uModel", model_m);
  program_.setUniformValue("uLightDir", QVector3D(0.4f, 0.25f, 1.0f));
  program_.setUniformValue("uBaseColor", QVector3D(0.75f, 0.78f, 0.82f));
  program_.setUniformValue("uHasTex", has_texture_ ? 1 : 0);
  program_.setUniformValue("uTex", 0);

  if (has_texture_ && texture_id_ != 0) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
  }

  if (vao_.isCreated()) {
    vao_.bind();
  } else {
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
  }

  glDrawElements(GL_TRIANGLES, index_count_, index_type_, nullptr);

  if (has_texture_ && texture_id_ != 0) {
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  if (vao_.isCreated()) {
    vao_.release();
  }
  program_.release();
}

void ModelViewerWidget::resizeGL(int, int) {
  update();
}

void ModelViewerWidget::mousePressEvent(QMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) {
    last_mouse_pos_ = event->pos();
    event->accept();
    return;
  }
  QOpenGLWidget::mousePressEvent(event);
}

void ModelViewerWidget::mouseMoveEvent(QMouseEvent* event) {
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

void ModelViewerWidget::wheelEvent(QWheelEvent* event) {
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

void ModelViewerWidget::reset_camera_from_mesh() {
  if (!model_) {
    center_ = QVector3D(0, 0, 0);
    radius_ = 1.0f;
    distance_ = 3.0f;
    return;
  }
  const QVector3D mins = model_->mesh.mins;
  const QVector3D maxs = model_->mesh.maxs;
  center_ = (mins + maxs) * 0.5f;
  radius_ = std::max(0.001f, (maxs - mins).length() * 0.5f);
  distance_ = std::max(radius_ * 2.6f, 1.0f);
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
  if (texture_id_ != 0) {
    glDeleteTextures(1, &texture_id_);
    texture_id_ = 0;
  }
  has_texture_ = false;
  program_.removeAllShaders();
  program_.release();
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
  upload_texture_if_possible();
  doneCurrent();
}

void ModelViewerWidget::upload_texture_if_possible() {
  if (!pending_texture_upload_ || !gl_ready_ || !context()) {
    return;
  }

  if (texture_id_ != 0) {
    glDeleteTextures(1, &texture_id_);
    texture_id_ = 0;
  }
  has_texture_ = false;

  if (skin_image_.isNull()) {
    pending_texture_upload_ = false;
    return;
  }

  QImage img = skin_image_.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
  if (img.isNull()) {
    pending_texture_upload_ = false;
    return;
  }

  glGenTextures(1, &texture_id_);
  glBindTexture(GL_TEXTURE_2D, texture_id_);
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

  has_texture_ = (texture_id_ != 0);
  pending_texture_upload_ = false;
}
