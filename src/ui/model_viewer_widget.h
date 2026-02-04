#pragma once

#include <QtGui/QOpenGLFunctions>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLVertexArrayObject>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QImage>
#include <QPoint>
#include <QVector>
#include <QVector3D>

#include <optional>

#include "formats/model.h"
#include "ui/preview_3d_options.h"

class QKeyEvent;

class ModelViewerWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  explicit ModelViewerWidget(QWidget* parent = nullptr);
  ~ModelViewerWidget() override;

  [[nodiscard]] bool has_model() const { return model_.has_value(); }
  [[nodiscard]] QString model_format() const { return model_ ? model_->format : QString(); }
  [[nodiscard]] ModelMesh mesh() const { return model_ ? model_->mesh : ModelMesh{}; }

  void set_texture_smoothing(bool enabled);
  void set_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette);
  void set_grid_mode(PreviewGridMode mode);
  void set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color);
  void set_wireframe_enabled(bool enabled);
  void set_textured_enabled(bool enabled);
  void set_glow_enabled(bool enabled);

  [[nodiscard]] bool load_file(const QString& file_path, QString* error = nullptr);
  [[nodiscard]] bool load_file(const QString& file_path, const QString& skin_path, QString* error);
  void unload();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  struct GpuVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
  };

  struct DrawSurface {
    int first_index = 0;
    int index_count = 0;
    QString name;
    QString shader_hint;
    QString shader_leaf;
    QImage image;
    QImage glow_image;
    GLuint texture_id = 0;
    GLuint glow_texture_id = 0;
    bool has_texture = false;
    bool has_glow = false;
  };

  void reset_camera_from_mesh();
  void frame_mesh();
  void pan_by_pixels(const QPoint& delta);
  void upload_mesh_if_possible();
  void upload_textures_if_possible();
  void destroy_gl_resources();
  void ensure_program();
  void update_ground_mesh_if_needed();
  void update_background_mesh_if_needed();
  void update_grid_settings();
  void apply_wireframe_state(bool enabled);
  void update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const;
  void update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const;

  enum class DragMode {
    None,
    Orbit,
    Pan,
  };

  std::optional<LoadedModel> model_;
  QString last_model_path_;
  QString last_skin_path_;
  bool pending_upload_ = false;

  QOpenGLShaderProgram program_;
  QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLBuffer ground_vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ground_ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLBuffer bg_vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject vao_;
  QOpenGLVertexArrayObject bg_vao_;
  bool gl_ready_ = false;
  int index_count_ = 0;
  GLenum index_type_ = GL_UNSIGNED_INT;
  int ground_index_count_ = 0;
  float ground_extent_ = 0.0f;
  float ground_z_ = 0.0f;
  float grid_scale_ = 1.0f;
  QVector<DrawSurface> surfaces_;
  GLuint texture_id_ = 0;
  GLuint glow_texture_id_ = 0;
  bool has_texture_ = false;
  bool has_glow_ = false;
  bool pending_texture_upload_ = false;
  QImage skin_image_;
  QImage skin_glow_image_;

  QVector3D center_ = QVector3D(0, 0, 0);
  float radius_ = 1.0f;
  float yaw_deg_ = 45.0f;
  float pitch_deg_ = 20.0f;
  float distance_ = 3.0f;

  QPoint last_mouse_pos_;
  DragMode drag_mode_ = DragMode::None;
  Qt::MouseButton drag_button_ = Qt::NoButton;

  bool texture_smoothing_ = false;
  QVector<QRgb> quake1_palette_;
  QVector<QRgb> quake2_palette_;

  PreviewGridMode grid_mode_ = PreviewGridMode::Floor;
  PreviewBackgroundMode bg_mode_ = PreviewBackgroundMode::Themed;
  QColor bg_custom_color_;
  bool wireframe_enabled_ = false;
  bool textured_enabled_ = true;
  bool glow_enabled_ = false;
};
