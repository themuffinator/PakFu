#include "ui/shader_viewer_widget.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

int wave_func_code(Quake3WaveFunc func) {
  switch (func) {
    case Quake3WaveFunc::Sin:
      return 1;
    case Quake3WaveFunc::Square:
      return 2;
    case Quake3WaveFunc::Triangle:
      return 3;
    case Quake3WaveFunc::Sawtooth:
      return 4;
    case Quake3WaveFunc::InverseSawtooth:
      return 5;
    case Quake3WaveFunc::Noise:
      return 6;
  }
  return 1;
}

QString sanitize_label_text(QString text) {
  for (int i = 0; i < text.size(); ++i) {
    const ushort u = text[i].unicode();
    if (u < 32 || u > 126) {
      text[i] = QChar('?');
    }
  }
  return text;
}

const char* kVertexShader = R"GLSL(
  #ifdef GL_ES
  precision highp float;
  #define ATTR attribute
  #define VARYING_OUT varying
  #else
  #if __VERSION__ >= 130
    #define ATTR in
    #define VARYING_OUT out
  #else
    #define ATTR attribute
    #define VARYING_OUT varying
  #endif
  #endif

  ATTR vec2 aPos;
  ATTR vec2 aUV;

  uniform mat4 uMvp;
  uniform float uTime;

  uniform int uTcModCount;
  uniform int uTcModType0;
  uniform int uTcModType1;
  uniform int uTcModType2;
  uniform int uTcModType3;
  uniform vec4 uTcModA0;
  uniform vec4 uTcModA1;
  uniform vec4 uTcModA2;
  uniform vec4 uTcModA3;
  uniform vec4 uTcModB0;
  uniform vec4 uTcModB1;
  uniform vec4 uTcModB2;
  uniform vec4 uTcModB3;

  uniform int uDeformType;
  uniform int uDeformWaveFunc;
  uniform vec4 uDeformA;
  uniform vec4 uDeformB;

  VARYING_OUT vec2 vUV;

  float waveValue(int func, float base, float amp, float phase, float freq, float t) {
    float x = phase + t * freq;
    if (func == 1) {
      return base + sin(x * 6.28318530718) * amp;
    }
    if (func == 2) {
      return base + ((sin(x * 6.28318530718) >= 0.0) ? 1.0 : -1.0) * amp;
    }
    if (func == 3) {
      float f = fract(x);
      float tri = (f < 0.5) ? (f * 4.0 - 1.0) : (3.0 - 4.0 * f);
      return base + tri * amp;
    }
    if (func == 4) {
      return base + fract(x) * amp;
    }
    if (func == 5) {
      return base + (1.0 - fract(x)) * amp;
    }
    if (func == 6) {
      float n = fract(sin((x + 17.13) * 43758.5453) * 43758.5453);
      return base + (n * 2.0 - 1.0) * amp;
    }
    return base;
  }

  vec2 applyTcMod(vec2 uv, int type, vec4 a, vec4 b) {
    if (type == 1) {
      float now = a.z + uTime * a.w;
      float s = sin((uv.x + now) * 6.28318530718) * a.y;
      float t = sin((uv.y + now) * 6.28318530718) * a.y;
      return uv + vec2(s + a.x, t + a.x);
    }
    if (type == 2) {
      return uv * a.xy;
    }
    if (type == 3) {
      return uv + fract(a.xy * uTime);
    }
    if (type == 4) {
      float p = 1.0 / max(0.0001, waveValue(int(a.x), a.y, a.z, a.w, b.x, uTime));
      return (uv - vec2(0.5)) * p + vec2(0.5);
    }
    if (type == 5) {
      return vec2(uv.x * a.x + uv.y * a.z + b.x, uv.x * a.y + uv.y * a.w + b.y);
    }
    if (type == 6) {
      float rad = -a.x * uTime * 0.01745329251;
      float c = cos(rad);
      float s = sin(rad);
      vec2 d = uv - vec2(0.5);
      return vec2(d.x * c - d.y * s, d.x * s + d.y * c) + vec2(0.5);
    }
    return uv;
  }

  vec2 applyDeform(vec2 pos, vec2 uv) {
    if (uDeformType == 1) {
      float off = (pos.x + pos.y) * uDeformA.x;
      float w = waveValue(uDeformWaveFunc, uDeformA.y, uDeformA.z, uDeformA.w + off, uDeformB.x, uTime);
      pos.y += w * 0.12;
    } else if (uDeformType == 2) {
      float amp = uDeformA.x;
      float freq = uDeformA.y;
      pos += vec2(sin((pos.x + uTime * freq) * 6.28318530718),
                  cos((pos.y + uTime * freq) * 6.28318530718)) * amp * 0.03;
    } else if (uDeformType == 3) {
      float w = waveValue(uDeformWaveFunc, uDeformB.x, uDeformB.y, uDeformB.z, uDeformB.w, uTime);
      pos += uDeformA.xy * w * 0.2;
    } else if (uDeformType == 4) {
      float w = sin((uv.x * uDeformA.x + uTime * uDeformA.z) * 6.28318530718) * uDeformA.y;
      pos.y += w * 0.18;
    }
    return pos;
  }

  void main() {
    vec2 pos = applyDeform(aPos, aUV);
    vec2 uv = aUV;
    if (uTcModCount > 0) uv = applyTcMod(uv, uTcModType0, uTcModA0, uTcModB0);
    if (uTcModCount > 1) uv = applyTcMod(uv, uTcModType1, uTcModA1, uTcModB1);
    if (uTcModCount > 2) uv = applyTcMod(uv, uTcModType2, uTcModA2, uTcModB2);
    if (uTcModCount > 3) uv = applyTcMod(uv, uTcModType3, uTcModA3, uTcModB3);
    vUV = uv;
    gl_Position = uMvp * vec4(pos, 0.0, 1.0);
  }
)GLSL";

const char* kFragmentShader = R"GLSL(
  #ifdef GL_ES
  precision mediump float;
  #define VARYING_IN varying
  #define FRAG_COLOR gl_FragColor
  #else
  #if __VERSION__ >= 130
    #define VARYING_IN in
    out vec4 _fragColor;
    #define FRAG_COLOR _fragColor
  #else
    #define VARYING_IN varying
    #define FRAG_COLOR gl_FragColor
  #endif
  #endif
  #if __VERSION__ >= 130
    #define texture2D texture
  #endif

  VARYING_IN vec2 vUV;
  uniform sampler2D uTex;
  uniform vec4 uColor;
  uniform int uClamp;
  uniform int uUseTex;
  uniform int uCheckerFallback;
  uniform int uAlphaFunc;

  void main() {
    vec2 uv = (uClamp == 1) ? clamp(vUV, vec2(0.0), vec2(1.0)) : vUV;
    vec4 sampled = vec4(1.0);
    if (uCheckerFallback == 1) {
      vec2 cell = floor(uv * 8.0);
      float c = mod(cell.x + cell.y, 2.0);
      sampled = mix(vec4(0.28, 0.28, 0.30, 1.0), vec4(0.42, 0.42, 0.46, 1.0), c);
    } else if (uUseTex == 1) {
      sampled = texture2D(uTex, uv);
    }
    vec4 color = sampled * uColor;
    if (uAlphaFunc == 1 && color.a <= 0.0) {
      discard;
    } else if (uAlphaFunc == 2 && color.a >= 0.5) {
      discard;
    } else if (uAlphaFunc == 3 && color.a < 0.5) {
      discard;
    }
    FRAG_COLOR = color;
  }
)GLSL";
}  // namespace

ShaderViewerWidget::ShaderViewerWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setAutoFillBackground(false);
  animation_origin_ms_ = QDateTime::currentMSecsSinceEpoch();
  animation_timer_.setInterval(33);
  connect(&animation_timer_, &QTimer::timeout, this, [this]() {
    if (isVisible()) {
      update();
    }
  });
}

ShaderViewerWidget::~ShaderViewerWidget() {
  makeCurrent();
  clear_gl_textures();
  if (checker_texture_ != 0) {
    glDeleteTextures(1, &checker_texture_);
    checker_texture_ = 0;
  }
  if (white_texture_ != 0) {
    glDeleteTextures(1, &white_texture_);
    white_texture_ = 0;
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
  doneCurrent();
}

QVector<int> ShaderViewerWidget::selected_indices() const {
  QVector<int> out = selection_.values().toVector();
  std::sort(out.begin(), out.end());
  return out;
}

QString ShaderViewerWidget::selected_shader_script_text() const {
  return join_quake3_shader_blocks_text(document_, selected_indices());
}

bool ShaderViewerWidget::build_text_with_appended_shaders(const QString& pasted_text, QString* out_text, QString* error) const {
  if (error) {
    error->clear();
  }
  if (!out_text) {
    if (error) {
      *error = "Invalid output text pointer.";
    }
    return false;
  }

  Quake3ShaderDocument parsed;
  QString parse_error;
  if (!parse_quake3_shader_text(pasted_text, &parsed, &parse_error) || parsed.shaders.isEmpty()) {
    if (error) {
      *error = parse_error.isEmpty() ? "Clipboard text does not contain shader blocks." : parse_error;
    }
    return false;
  }

  *out_text = append_quake3_shader_blocks_text(source_text_, parsed);
  return true;
}

void ShaderViewerWidget::set_document(const QString& source_text,
                                      const Quake3ShaderDocument& document,
                                      QHash<QString, QImage> textures) {
  source_text_ = source_text;
  document_ = document;
  selection_.clear();
  anchor_index_ = -1;

  source_textures_.clear();
  auto add_aliases = [this](const QString& raw_key, const QImage& image) {
    if (image.isNull()) {
      return;
    }
    const QString key = normalize_texture_key(raw_key);
    if (key.isEmpty()) {
      return;
    }
    source_textures_.insert(key, image);

    const QFileInfo fi(key);
    const QString leaf = fi.fileName().toLower();
    const QString base = fi.completeBaseName().toLower();
    if (!leaf.isEmpty()) {
      source_textures_.insert(leaf, image);
    }
    if (!base.isEmpty()) {
      source_textures_.insert(base, image);
    }
    const int dot = key.lastIndexOf('.');
    if (dot > 0) {
      source_textures_.insert(key.left(dot), image);
    }
  };

  for (auto it = textures.begin(); it != textures.end(); ++it) {
    add_aliases(it.key(), it.value());
  }

  makeCurrent();
  clear_gl_textures();
  doneCurrent();

  animation_origin_ms_ = QDateTime::currentMSecsSinceEpoch();
  if (has_animated_features()) {
    animation_timer_.start();
  } else {
    animation_timer_.stop();
  }

  rebuild_layout();
  update();
}

void ShaderViewerWidget::set_viewport_width(int width) {
  const int clamped = std::max(160, width);
  if (viewport_width_ == clamped) {
    return;
  }
  viewport_width_ = clamped;
  setFixedWidth(clamped);
  rebuild_layout();
}

void ShaderViewerWidget::initializeGL() {
  initializeOpenGLFunctions();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl_ready_ = true;
}

void ShaderViewerWidget::resizeGL(int, int) {
  rebuild_layout();
}

void ShaderViewerWidget::paintEvent(QPaintEvent* event) {
  QOpenGLWidget::paintEvent(event);
  draw_overlay();
}

void ShaderViewerWidget::ensure_program() {
  if (program_.isLinked()) {
    gl_program_error_.clear();
    return;
  }

  program_.removeAllShaders();
  if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
    gl_program_error_ = program_.log();
    qWarning() << "ShaderViewer vertex shader compile failed:" << gl_program_error_;
    return;
  }
  if (!program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
    gl_program_error_ = program_.log();
    qWarning() << "ShaderViewer fragment shader compile failed:" << gl_program_error_;
    return;
  }
  program_.bindAttributeLocation("aPos", 0);
  program_.bindAttributeLocation("aUV", 1);
  if (!program_.link()) {
    gl_program_error_ = program_.log();
    qWarning() << "ShaderViewer shader link failed:" << gl_program_error_;
    program_.removeAllShaders();
    return;
  }
  gl_program_error_.clear();
}

void ShaderViewerWidget::ensure_mesh() {
  if (!gl_ready_) {
    return;
  }
  if (vao_.isCreated() && vbo_.isCreated() && ibo_.isCreated()) {
    return;
  }

  struct Vertex {
    float x, y;
    float u, v;
  };

  const Vertex verts[4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f},
  };
  const quint16 idx[6] = {0, 1, 2, 0, 2, 3};

  if (!vao_.isCreated()) {
    vao_.create();
  }
  if (vao_.isCreated()) {
    vao_.bind();
  }

  vbo_.create();
  vbo_.bind();
  vbo_.allocate(verts, static_cast<int>(sizeof(verts)));

  ibo_.create();
  ibo_.bind();
  ibo_.allocate(idx, static_cast<int>(sizeof(idx)));

  const int stride = sizeof(Vertex);
  program_.enableAttributeArray(0);
  program_.enableAttributeArray(1);
  program_.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, x), 2, stride);
  program_.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, u), 2, stride);

  vbo_.release();
  ibo_.release();
  if (vao_.isCreated()) {
    vao_.release();
  }
}

GLuint ShaderViewerWidget::upload_texture(const QImage& image) {
  if (image.isNull()) {
    return 0;
  }
  QImage img = image.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
  if (img.isNull()) {
    return 0;
  }

  GLuint id = 0;
  glGenTextures(1, &id);
  if (id == 0) {
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, id);
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
  return id;
}

void ShaderViewerWidget::ensure_textures() {
  if (!gl_ready_) {
    return;
  }

  if (checker_texture_ == 0) {
    QImage checker(64, 64, QImage::Format_ARGB32);
    checker.fill(Qt::transparent);
    for (int y = 0; y < checker.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(checker.scanLine(y));
      for (int x = 0; x < checker.width(); ++x) {
        const bool dark = ((x / 8) + (y / 8)) % 2 == 0;
        row[x] = dark ? qRgba(72, 72, 76, 255) : qRgba(108, 108, 116, 255);
      }
    }
    checker_texture_ = upload_texture(checker);
  }
  if (white_texture_ == 0) {
    QImage white(1, 1, QImage::Format_ARGB32);
    white.fill(qRgba(255, 255, 255, 255));
    white_texture_ = upload_texture(white);
  }

  for (auto it = source_textures_.begin(); it != source_textures_.end(); ++it) {
    if (gl_textures_.contains(it.key())) {
      continue;
    }
    const GLuint id = upload_texture(it.value());
    if (id != 0) {
      gl_textures_.insert(it.key(), id);
    }
  }
}

void ShaderViewerWidget::clear_gl_textures() {
  if (!gl_textures_.isEmpty()) {
    QVector<GLuint> ids;
    ids.reserve(gl_textures_.size());
    for (auto it = gl_textures_.cbegin(); it != gl_textures_.cend(); ++it) {
      if (it.value() != 0) {
        ids.push_back(it.value());
      }
    }
    if (!ids.isEmpty()) {
      glDeleteTextures(ids.size(), ids.data());
    }
  }
  gl_textures_.clear();
}

QString ShaderViewerWidget::normalize_texture_key(const QString& ref) const {
  QString key = ref.trimmed().toLower();
  key.replace('\\', '/');
  while (key.startsWith('/')) {
    key.remove(0, 1);
  }
  return key;
}

GLuint ShaderViewerWidget::texture_for_stage(const Quake3ShaderStage& stage, const QString& fallback_ref, float time_seconds) {
  if (checker_texture_ == 0 && white_texture_ == 0) {
    return 0;
  }

  QString ref = stage.map;
  if (!stage.anim_maps.isEmpty() && stage.anim_frequency > 0.0f) {
    const int frame = static_cast<int>(std::floor(time_seconds * stage.anim_frequency));
    const int idx = ((frame % stage.anim_maps.size()) + stage.anim_maps.size()) % stage.anim_maps.size();
    ref = stage.anim_maps[idx];
  } else if (!stage.anim_maps.isEmpty()) {
    ref = stage.anim_maps.first();
  }

  QString key = normalize_texture_key(ref);
  if (stage.is_lightmap || stage.is_whiteimage) {
    return white_texture_ != 0 ? white_texture_ : checker_texture_;
  }
  if (key.isEmpty() || key.startsWith('$')) {
    return checker_texture_;
  }

  if (gl_textures_.contains(key)) {
    return gl_textures_.value(key);
  }

  const int dot = key.lastIndexOf('.');
  if (dot > 0) {
    const QString without_ext = key.left(dot);
    if (gl_textures_.contains(without_ext)) {
      return gl_textures_.value(without_ext);
    }
  }

  const QString leaf = QFileInfo(key).fileName().toLower();
  if (!leaf.isEmpty() && gl_textures_.contains(leaf)) {
    return gl_textures_.value(leaf);
  }

  const QString base = QFileInfo(key).completeBaseName().toLower();
  if (!base.isEmpty() && gl_textures_.contains(base)) {
    return gl_textures_.value(base);
  }

  const QString fallback_key = normalize_texture_key(fallback_ref);
  if (!fallback_key.isEmpty()) {
    if (gl_textures_.contains(fallback_key)) {
      return gl_textures_.value(fallback_key);
    }
    const int fallback_dot = fallback_key.lastIndexOf('.');
    if (fallback_dot > 0) {
      const QString without_ext = fallback_key.left(fallback_dot);
      if (gl_textures_.contains(without_ext)) {
        return gl_textures_.value(without_ext);
      }
    }
    const QString fallback_leaf = QFileInfo(fallback_key).fileName().toLower();
    if (!fallback_leaf.isEmpty() && gl_textures_.contains(fallback_leaf)) {
      return gl_textures_.value(fallback_leaf);
    }
    const QString fallback_base = QFileInfo(fallback_key).completeBaseName().toLower();
    if (!fallback_base.isEmpty() && gl_textures_.contains(fallback_base)) {
      return gl_textures_.value(fallback_base);
    }
  }

  return checker_texture_;
}

float ShaderViewerWidget::eval_wave(const Quake3WaveForm& wave, float time_seconds) const {
  if (!wave.valid) {
    return wave.base;
  }

  const float x = wave.phase + time_seconds * wave.frequency;
  float signal = 0.0f;
  switch (wave.func) {
    case Quake3WaveFunc::Sin:
      signal = std::sin(kTwoPi * x);
      break;
    case Quake3WaveFunc::Square:
      signal = (std::sin(kTwoPi * x) >= 0.0f) ? 1.0f : -1.0f;
      break;
    case Quake3WaveFunc::Triangle: {
      const float f = x - std::floor(x);
      signal = (f < 0.5f) ? (f * 4.0f - 1.0f) : (3.0f - 4.0f * f);
      break;
    }
    case Quake3WaveFunc::Sawtooth:
      signal = x - std::floor(x);
      break;
    case Quake3WaveFunc::InverseSawtooth:
      signal = 1.0f - (x - std::floor(x));
      break;
    case Quake3WaveFunc::Noise: {
      const float n = std::sin((x + 17.13f) * 43758.5453f);
      signal = (n - std::floor(n)) * 2.0f - 1.0f;
      break;
    }
  }
  return wave.base + signal * wave.amplitude;
}

QVector4D ShaderViewerWidget::evaluate_stage_color(const Quake3ShaderStage& stage, float time_seconds) const {
  QVector4D color(1.0f, 1.0f, 1.0f, 1.0f);

  switch (stage.rgb_gen) {
    case Quake3RgbGen::Constant:
      color.setX(stage.rgb_constant.redF());
      color.setY(stage.rgb_constant.greenF());
      color.setZ(stage.rgb_constant.blueF());
      break;
    case Quake3RgbGen::Wave: {
      const float v = std::clamp(eval_wave(stage.rgb_wave, time_seconds), 0.0f, 1.0f);
      color.setX(v);
      color.setY(v);
      color.setZ(v);
      break;
    }
    default:
      break;
  }

  switch (stage.alpha_gen) {
    case Quake3AlphaGen::Constant:
      color.setW(std::clamp(stage.alpha_constant, 0.0f, 1.0f));
      break;
    case Quake3AlphaGen::Wave:
      color.setW(std::clamp(eval_wave(stage.alpha_wave, time_seconds), 0.0f, 1.0f));
      break;
    case Quake3AlphaGen::Skip:
    case Quake3AlphaGen::Identity:
    default:
      color.setW(1.0f);
      break;
  }

  return color;
}

GLenum ShaderViewerWidget::gl_blend_factor(Quake3BlendFactor factor) const {
  switch (factor) {
    case Quake3BlendFactor::Zero:
      return GL_ZERO;
    case Quake3BlendFactor::One:
      return GL_ONE;
    case Quake3BlendFactor::SrcColor:
      return GL_SRC_COLOR;
    case Quake3BlendFactor::OneMinusSrcColor:
      return GL_ONE_MINUS_SRC_COLOR;
    case Quake3BlendFactor::DstColor:
      return GL_DST_COLOR;
    case Quake3BlendFactor::OneMinusDstColor:
      return GL_ONE_MINUS_DST_COLOR;
    case Quake3BlendFactor::SrcAlpha:
      return GL_SRC_ALPHA;
    case Quake3BlendFactor::OneMinusSrcAlpha:
      return GL_ONE_MINUS_SRC_ALPHA;
    case Quake3BlendFactor::DstAlpha:
      return GL_DST_ALPHA;
    case Quake3BlendFactor::OneMinusDstAlpha:
      return GL_ONE_MINUS_DST_ALPHA;
    case Quake3BlendFactor::SrcAlphaSaturate:
      return GL_SRC_ALPHA_SATURATE;
  }
  return GL_ONE;
}

int ShaderViewerWidget::alpha_func_code(Quake3AlphaFunc func) const {
  switch (func) {
    case Quake3AlphaFunc::GT0:
      return 1;
    case Quake3AlphaFunc::LT128:
      return 2;
    case Quake3AlphaFunc::GE128:
      return 3;
    case Quake3AlphaFunc::None:
    default:
      return 0;
  }
}

ShaderViewerWidget::StageUniforms ShaderViewerWidget::build_stage_uniforms(const Quake3ShaderBlock& shader,
                                                                           const Quake3ShaderStage& stage,
                                                                           float time_seconds) {
  StageUniforms out;
  out.texture_id = texture_for_stage(stage, shader.name, time_seconds);
  out.clamp = stage.clamp_map;
  out.blend_enabled = stage.blend_enabled;
  out.blend_src = gl_blend_factor(stage.blend_src);
  out.blend_dst = gl_blend_factor(stage.blend_dst);
  out.alpha_func = alpha_func_code(stage.alpha_func);
  out.color = evaluate_stage_color(stage, time_seconds);

  constexpr int kMaxMods = 4;
  out.tc_mods.reserve(std::min(static_cast<int>(stage.tc_mods.size()), kMaxMods));
  for (int i = 0; i < stage.tc_mods.size() && i < kMaxMods; ++i) {
    const Quake3TcMod& src = stage.tc_mods[i];
    TcModUniform tm;
    switch (src.type) {
      case Quake3TcModType::Turbulent:
        tm.type = 1;
        tm.a = QVector4D(src.wave.base, src.wave.amplitude, src.wave.phase, src.wave.frequency);
        break;
      case Quake3TcModType::Scale:
        tm.type = 2;
        tm.a = QVector4D(src.scaleS, src.scaleT, 0.0f, 0.0f);
        break;
      case Quake3TcModType::Scroll:
        tm.type = 3;
        tm.a = QVector4D(src.scrollS, src.scrollT, 0.0f, 0.0f);
        break;
      case Quake3TcModType::Stretch:
        tm.type = 4;
        tm.a = QVector4D(static_cast<float>(wave_func_code(src.wave.func)),
                         src.wave.base,
                         src.wave.amplitude,
                         src.wave.phase);
        tm.b = QVector4D(src.wave.frequency, 0.0f, 0.0f, 0.0f);
        break;
      case Quake3TcModType::Transform:
        tm.type = 5;
        tm.a = QVector4D(src.matrix00, src.matrix01, src.matrix10, src.matrix11);
        tm.b = QVector4D(src.translateS, src.translateT, 0.0f, 0.0f);
        break;
      case Quake3TcModType::Rotate:
        tm.type = 6;
        tm.a = QVector4D(src.rotateSpeed, 0.0f, 0.0f, 0.0f);
        break;
      case Quake3TcModType::EntityTranslate:
        tm.type = 7;
        break;
    }
    out.tc_mods.push_back(tm);
  }
  return out;
}

ShaderViewerWidget::DeformUniforms ShaderViewerWidget::build_deform_uniforms(const Quake3ShaderBlock& shader) {
  DeformUniforms out;
  for (const Quake3ShaderDeform& deform : shader.deforms) {
    switch (deform.type) {
      case Quake3DeformType::Wave:
        out.type = 1;
        out.wave_func = wave_func_code(deform.wave.func);
        out.a = QVector4D(deform.spread, deform.wave.base, deform.wave.amplitude, deform.wave.phase);
        out.b = QVector4D(deform.wave.frequency, 0.0f, 0.0f, 0.0f);
        return out;
      case Quake3DeformType::Normal:
        out.type = 2;
        out.a = QVector4D(deform.wave.amplitude, deform.wave.frequency, 0.0f, 0.0f);
        return out;
      case Quake3DeformType::Move:
        out.type = 3;
        out.wave_func = wave_func_code(deform.wave.func);
        out.a = QVector4D(deform.move_vector.x(), deform.move_vector.y(), deform.move_vector.z(), 0.0f);
        out.b = QVector4D(deform.wave.base, deform.wave.amplitude, deform.wave.phase, deform.wave.frequency);
        return out;
      case Quake3DeformType::Bulge:
        out.type = 4;
        out.a = QVector4D(deform.bulge_width, deform.bulge_height, deform.bulge_speed, 0.0f);
        return out;
      default:
        break;
    }
  }
  return out;
}

void ShaderViewerWidget::draw_tile_stage(const QRectF& rect,
                                         const StageUniforms& uniforms,
                                         const DeformUniforms& deform,
                                         const QMatrix4x4& ortho,
                                         float time_seconds) {
  QMatrix4x4 model;
  model.translate(rect.x(), rect.y());
  model.scale(rect.width(), rect.height());
  const QMatrix4x4 mvp = ortho * model;

  program_.setUniformValue("uMvp", mvp);
  program_.setUniformValue("uTime", time_seconds);
  program_.setUniformValue("uClamp", uniforms.clamp ? 1 : 0);
  const bool has_tex = (uniforms.texture_id != 0);
  program_.setUniformValue("uUseTex", has_tex ? 1 : 0);
  program_.setUniformValue("uCheckerFallback", uniforms.checker_fallback ? 1 : 0);
  program_.setUniformValue("uAlphaFunc", uniforms.alpha_func);
  program_.setUniformValue("uColor", uniforms.color);

  program_.setUniformValue("uDeformType", deform.type);
  program_.setUniformValue("uDeformWaveFunc", deform.wave_func);
  program_.setUniformValue("uDeformA", deform.a);
  program_.setUniformValue("uDeformB", deform.b);

  auto set_tc_uniform = [&](int index, const TcModUniform& tm) {
    program_.setUniformValue(QString("uTcModType%1").arg(index).toLatin1().constData(), tm.type);
    program_.setUniformValue(QString("uTcModA%1").arg(index).toLatin1().constData(), tm.a);
    program_.setUniformValue(QString("uTcModB%1").arg(index).toLatin1().constData(), tm.b);
  };

  program_.setUniformValue("uTcModCount", static_cast<int>(uniforms.tc_mods.size()));
  for (int i = 0; i < 4; ++i) {
    if (i < uniforms.tc_mods.size()) {
      set_tc_uniform(i, uniforms.tc_mods[i]);
    } else {
      set_tc_uniform(i, TcModUniform{});
    }
  }

  if (uniforms.blend_enabled) {
    glEnable(GL_BLEND);
    glBlendFunc(uniforms.blend_src, uniforms.blend_dst);
  } else {
    glDisable(GL_BLEND);
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, has_tex ? uniforms.texture_id : 0);
  program_.setUniformValue("uTex", 0);

  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
}

void ShaderViewerWidget::paintGL() {
  if (!gl_ready_) {
    return;
  }

  ensure_program();
  if (!program_.isLinked()) {
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return;
  }

  ensure_mesh();
  ensure_textures();

  const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
  const int fb_w = std::max(1, static_cast<int>(std::lround(width() * dpr)));
  const int fb_h = std::max(1, static_cast<int>(std::lround(height() * dpr)));
  glViewport(0, 0, fb_w, fb_h);

  const QColor base = palette().color(QPalette::Base);
  glClearColor(base.redF() * 0.28f, base.greenF() * 0.28f, base.blueF() * 0.28f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  program_.bind();
  const bool vao_bound = vao_.isCreated();
  if (vao_bound) {
    vao_.bind();
  }
  vbo_.bind();
  ibo_.bind();
  const int stride = sizeof(float) * 4;
  program_.enableAttributeArray(0);
  program_.enableAttributeArray(1);
  program_.setAttributeBuffer(0, GL_FLOAT, 0, 2, stride);
  program_.setAttributeBuffer(1, GL_FLOAT, static_cast<int>(sizeof(float) * 2), 2, stride);

  QMatrix4x4 ortho;
  ortho.ortho(0.0f, static_cast<float>(width()), static_cast<float>(height()), 0.0f, -1.0f, 1.0f);
  const float time_seconds = static_cast<float>(QDateTime::currentMSecsSinceEpoch() - animation_origin_ms_) * 0.001f;
  GLint viewport[4] = {0, 0, fb_w, fb_h};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const float sx_scale = (width() > 0) ? static_cast<float>(viewport[2]) / static_cast<float>(width()) : 1.0f;
  const float sy_scale = (height() > 0) ? static_cast<float>(viewport[3]) / static_cast<float>(height()) : 1.0f;

  glEnable(GL_SCISSOR_TEST);
  for (const Tile& tile : tiles_) {
    if (tile.shader_index < 0 || tile.shader_index >= document_.shaders.size()) {
      continue;
    }

    const Quake3ShaderBlock& shader = document_.shaders[tile.shader_index];
    const QRectF rect = tile.preview_bounds;

    const int sx = viewport[0] + std::max(0, static_cast<int>(std::floor(rect.x() * sx_scale)));
    const int sy = viewport[1] + std::max(0, static_cast<int>(std::floor(viewport[3] - rect.bottom() * sy_scale)));
    const int sw = std::max(1, static_cast<int>(std::ceil(rect.width() * sx_scale)));
    const int sh = std::max(1, static_cast<int>(std::ceil(rect.height() * sy_scale)));
    glScissor(sx, sy, sw, sh);

    StageUniforms bg;
    bg.texture_id = checker_texture_;
    bg.checker_fallback = (checker_texture_ == 0);
    bg.color = QVector4D(1.0f, 1.0f, 1.0f, 1.0f);
    bg.blend_enabled = false;
    TcModUniform checker_scale;
    checker_scale.type = 2;
    checker_scale.a = QVector4D(8.0f, 8.0f, 0.0f, 0.0f);
    bg.tc_mods.push_back(checker_scale);
    draw_tile_stage(rect, bg, DeformUniforms{}, ortho, time_seconds);

    if (shader.no_draw) {
      continue;
    }

    const DeformUniforms deform = build_deform_uniforms(shader);
    for (const Quake3ShaderStage& stage : shader.stages) {
      const StageUniforms uniforms = build_stage_uniforms(shader, stage, time_seconds);
      draw_tile_stage(rect, uniforms, deform, ortho, time_seconds);
    }

    if (shader.stages.isEmpty()) {
      Quake3ShaderStage implicit_stage;
      implicit_stage.map = shader.name;
      implicit_stage.blend_enabled = false;
      implicit_stage.rgb_gen = Quake3RgbGen::IdentityLighting;
      const StageUniforms uniforms = build_stage_uniforms(shader, implicit_stage, time_seconds);
      draw_tile_stage(rect, uniforms, deform, ortho, time_seconds);
    }
  }
  glDisable(GL_SCISSOR_TEST);
  glBindTexture(GL_TEXTURE_2D, 0);
  program_.disableAttributeArray(0);
  program_.disableAttributeArray(1);
  ibo_.release();
  vbo_.release();
  if (vao_bound) {
    vao_.release();
  }
  program_.release();

  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ShaderViewerWidget::draw_overlay() {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, false);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  if (document_.shaders.isEmpty()) {
    painter.setPen(QColor(220, 220, 220, 220));
    painter.drawText(rect(), Qt::AlignCenter, "No shader blocks to preview.");
    return;
  }

  if (!program_.isLinked() && !gl_program_error_.isEmpty()) {
    painter.setPen(QColor(255, 176, 176, 235));
    painter.drawText(QRect(8, 8, width() - 16, 44),
                     Qt::AlignLeft | Qt::TextWordWrap,
                     QString("Shader tile renderer error: %1").arg(gl_program_error_));
  }

  const QColor border_normal(145, 145, 150, 180);
  QColor border_selected = palette().color(QPalette::Highlight);
  if (!border_selected.isValid()) {
    border_selected = QColor(90, 150, 220);
  }

  for (const Tile& tile : tiles_) {
    if (tile.shader_index < 0 || tile.shader_index >= document_.shaders.size()) {
      continue;
    }

    const bool selected = selection_.contains(tile.shader_index);
    painter.setPen(QPen(selected ? border_selected : border_normal, selected ? 2.0 : 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(tile.preview_bounds.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);

    const QRectF label = tile.label_bounds;
    painter.fillRect(label, QColor(0, 0, 0, 72));
    painter.setPen(QColor(230, 230, 230, 230));

    const Quake3ShaderBlock& shader = document_.shaders[tile.shader_index];
    const QFontMetrics fm(painter.font());
    const QString name_source = sanitize_label_text(shader.name);
    const QString name = fm.elidedText(name_source, Qt::ElideMiddle, std::max(20, static_cast<int>(label.width()) - 8));
    painter.drawText(label.adjusted(4, 3, -4, -18), Qt::AlignLeft | Qt::AlignVCenter, name);

    QString meta = QString("%1 stage%2").arg(shader.stages.size()).arg(shader.stages.size() == 1 ? "" : "s");
    if (shader.no_draw) {
      meta += "  •  nodraw";
    } else if (!shader.deforms.isEmpty()) {
      meta += "  •  deform";
    }
    const QString meta_line = fm.elidedText(meta, Qt::ElideRight, std::max(20, static_cast<int>(label.width()) - 8));
    painter.setPen(QColor(196, 196, 200, 220));
    painter.drawText(label.adjusted(4, 18, -4, -2), Qt::AlignLeft | Qt::AlignVCenter, meta_line);
  }
}

void ShaderViewerWidget::rebuild_layout() {
  tiles_.clear();

  const int width_for_layout = std::max(160, viewport_width_ > 0 ? viewport_width_ : width());
  const int card_width = tile_size_;
  const int row_height = tile_size_ + label_height_;
  const int avail = std::max(1, width_for_layout - tile_margin_ * 2 + tile_gap_);
  const int cols = std::max(1, avail / std::max(1, (card_width + tile_gap_)));

  tiles_.reserve(document_.shaders.size());
  int rows = 0;
  for (int i = 0; i < document_.shaders.size(); ++i) {
    const int row = i / cols;
    const int col = i % cols;
    rows = std::max(rows, row + 1);

    const int x = tile_margin_ + col * (card_width + tile_gap_);
    const int y = tile_margin_ + row * (row_height + tile_gap_);

    Tile tile;
    tile.shader_index = i;
    tile.preview_bounds = QRectF(x, y, card_width, tile_size_);
    tile.label_bounds = QRectF(x, y + tile_size_ + 2, card_width, label_height_ - 2);
    tile.bounds = tile.preview_bounds.united(tile.label_bounds);
    tiles_.push_back(tile);
  }

  const int total_height = std::max(height(), tile_margin_ * 2 + rows * (row_height + tile_gap_) - tile_gap_);
  setMinimumHeight(total_height);
  resize(width_for_layout, total_height);
  update();
}

int ShaderViewerWidget::hit_test(const QPointF& pos) const {
  for (const Tile& tile : tiles_) {
    if (tile.bounds.contains(pos)) {
      return tile.shader_index;
    }
  }
  return -1;
}

void ShaderViewerWidget::set_single_selection(int index) {
  selection_.clear();
  if (index >= 0) {
    selection_.insert(index);
    anchor_index_ = index;
  } else {
    anchor_index_ = -1;
  }
}

void ShaderViewerWidget::toggle_selection(int index) {
  if (index < 0) {
    return;
  }
  if (selection_.contains(index)) {
    selection_.remove(index);
  } else {
    selection_.insert(index);
    anchor_index_ = index;
  }
  if (selection_.isEmpty()) {
    anchor_index_ = -1;
  }
}

void ShaderViewerWidget::select_range_to(int index) {
  if (index < 0) {
    return;
  }
  if (anchor_index_ < 0) {
    set_single_selection(index);
    return;
  }
  const int lo = std::min(anchor_index_, index);
  const int hi = std::max(anchor_index_, index);
  selection_.clear();
  for (int i = lo; i <= hi; ++i) {
    selection_.insert(i);
  }
}

bool ShaderViewerWidget::has_animated_features() const {
  for (const Quake3ShaderBlock& shader : document_.shaders) {
    if (!shader.deforms.isEmpty()) {
      return true;
    }
    for (const Quake3ShaderStage& stage : shader.stages) {
      if (stage.anim_maps.size() > 1 && stage.anim_frequency > 0.0f) {
        return true;
      }
      if (stage.rgb_gen == Quake3RgbGen::Wave || stage.alpha_gen == Quake3AlphaGen::Wave) {
        return true;
      }
      for (const Quake3TcMod& mod : stage.tc_mods) {
        if (mod.type == Quake3TcModType::Scroll || mod.type == Quake3TcModType::Rotate ||
            mod.type == Quake3TcModType::Stretch || mod.type == Quake3TcModType::Turbulent ||
            mod.type == Quake3TcModType::EntityTranslate) {
          return true;
        }
      }
    }
  }
  return false;
}

void ShaderViewerWidget::mousePressEvent(QMouseEvent* event) {
  if (!event) {
    return;
  }
  const int idx = hit_test(event->position());
  const Qt::KeyboardModifiers mods = event->modifiers();
  const bool toggle_modifier = (mods & Qt::ControlModifier) || (mods & Qt::MetaModifier);

  if (idx < 0) {
    if (!toggle_modifier && !(mods & Qt::ShiftModifier)) {
      set_single_selection(-1);
      update();
    }
    return;
  }

  if (mods & Qt::ShiftModifier) {
    select_range_to(idx);
  } else if (toggle_modifier) {
    toggle_selection(idx);
  } else {
    set_single_selection(idx);
  }

  update();
  event->accept();
}

void ShaderViewerWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  mousePressEvent(event);
}

void ShaderViewerWidget::keyPressEvent(QKeyEvent* event) {
  if (!event) {
    return;
  }

  if (event->matches(QKeySequence::SelectAll)) {
    selection_.clear();
    for (int i = 0; i < document_.shaders.size(); ++i) {
      selection_.insert(i);
    }
    if (!document_.shaders.isEmpty()) {
      anchor_index_ = 0;
    }
    update();
    event->accept();
    return;
  }

  if (event->key() == Qt::Key_Escape) {
    set_single_selection(-1);
    update();
    event->accept();
    return;
  }

  QOpenGLWidget::keyPressEvent(event);
}
