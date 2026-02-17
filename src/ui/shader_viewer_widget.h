#pragma once

#include <QHash>
#include <QImage>
#include <QtGui/QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QVector4D>

#include "formats/quake3_shader.h"

class QMouseEvent;
class QKeyEvent;

class ShaderViewerWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  explicit ShaderViewerWidget(QWidget* parent = nullptr);
  ~ShaderViewerWidget() override;

  void set_document(const QString& source_text, const Quake3ShaderDocument& document, QHash<QString, QImage> textures);
  void set_viewport_width(int width);

  [[nodiscard]] bool has_document() const { return !document_.shaders.isEmpty(); }
  [[nodiscard]] bool has_selection() const { return !selection_.isEmpty(); }
  [[nodiscard]] QVector<int> selected_indices() const;
  [[nodiscard]] QString selected_shader_script_text() const;
  [[nodiscard]] bool build_text_with_appended_shaders(const QString& pasted_text, QString* out_text, QString* error) const;

 protected:
  void initializeGL() override;
  void paintGL() override;
  void paintEvent(QPaintEvent* event) override;
  void resizeGL(int w, int h) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  struct Tile {
    int shader_index = -1;
    QRectF bounds;
    QRectF preview_bounds;
    QRectF label_bounds;
  };

  struct TcModUniform {
    int type = 0;
    QVector4D a;
    QVector4D b;
  };

  struct StageUniforms {
    GLuint texture_id = 0;
    bool clamp = false;
    bool checker_fallback = false;
    bool blend_enabled = false;
    GLenum blend_src = GL_ONE;
    GLenum blend_dst = GL_ZERO;
    int alpha_func = 0;
    QVector4D color = QVector4D(1.0f, 1.0f, 1.0f, 1.0f);
    QVector<TcModUniform> tc_mods;
  };

  struct DeformUniforms {
    int type = 0;
    int wave_func = 0;
    QVector4D a;
    QVector4D b;
  };

  void rebuild_layout();
  [[nodiscard]] int hit_test(const QPointF& pos) const;
  void set_single_selection(int index);
  void toggle_selection(int index);
  void select_range_to(int index);

  [[nodiscard]] bool has_animated_features() const;
  [[nodiscard]] QString normalize_texture_key(const QString& ref) const;
  [[nodiscard]] GLuint texture_for_stage(const Quake3ShaderStage& stage, const QString& fallback_ref, float time_seconds);
  [[nodiscard]] StageUniforms build_stage_uniforms(const Quake3ShaderBlock& shader,
                                                   const Quake3ShaderStage& stage,
                                                   float time_seconds);
  [[nodiscard]] DeformUniforms build_deform_uniforms(const Quake3ShaderBlock& shader);
  [[nodiscard]] QVector4D evaluate_stage_color(const Quake3ShaderStage& stage, float time_seconds) const;
  [[nodiscard]] float eval_wave(const Quake3WaveForm& wave, float time_seconds) const;
  [[nodiscard]] GLenum gl_blend_factor(Quake3BlendFactor factor) const;
  [[nodiscard]] int alpha_func_code(Quake3AlphaFunc func) const;

  void ensure_program();
  void ensure_mesh();
  void ensure_textures();
  void clear_gl_textures();
  GLuint upload_texture(const QImage& image);
  void draw_tile_stage(const QRectF& rect,
                       const StageUniforms& uniforms,
                       const DeformUniforms& deform,
                       const QMatrix4x4& ortho,
                       float time_seconds);
  void draw_overlay();

  Quake3ShaderDocument document_;
  QString source_text_;
  QString gl_program_error_;
  QHash<QString, QImage> source_textures_;
  QHash<QString, GLuint> gl_textures_;
  GLuint checker_texture_ = 0;
  GLuint white_texture_ = 0;

  QOpenGLShaderProgram program_;
  QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject vao_;
  bool gl_ready_ = false;

  QVector<Tile> tiles_;
  QSet<int> selection_;
  int anchor_index_ = -1;
  int viewport_width_ = 0;
  int tile_size_ = 170;
  int tile_gap_ = 14;
  int tile_margin_ = 14;
  int label_height_ = 40;

  QTimer animation_timer_;
  qint64 animation_origin_ms_ = 0;
};
