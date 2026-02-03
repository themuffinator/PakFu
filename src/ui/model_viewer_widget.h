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

class ModelViewerWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  explicit ModelViewerWidget(QWidget* parent = nullptr);
  ~ModelViewerWidget() override;

  [[nodiscard]] bool has_model() const { return model_.has_value(); }
  [[nodiscard]] QString model_format() const { return model_ ? model_->format : QString(); }
  [[nodiscard]] ModelMesh mesh() const { return model_ ? model_->mesh : ModelMesh{}; }

  void set_texture_smoothing(bool enabled);
  void set_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette);

  [[nodiscard]] bool load_file(const QString& file_path, QString* error = nullptr);
  [[nodiscard]] bool load_file(const QString& file_path, const QString& skin_path, QString* error);
  void unload();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

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
    GLuint texture_id = 0;
    bool has_texture = false;
  };

  void reset_camera_from_mesh();
  void upload_mesh_if_possible();
  void upload_textures_if_possible();
  void destroy_gl_resources();
  void ensure_program();

  std::optional<LoadedModel> model_;
  bool pending_upload_ = false;

  QOpenGLShaderProgram program_;
  QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject vao_;
  bool gl_ready_ = false;
  int index_count_ = 0;
  GLenum index_type_ = GL_UNSIGNED_INT;
  QVector<DrawSurface> surfaces_;
  GLuint texture_id_ = 0;
  bool has_texture_ = false;
  bool pending_texture_upload_ = false;
  QImage skin_image_;

  QVector3D center_ = QVector3D(0, 0, 0);
  float radius_ = 1.0f;
  float yaw_deg_ = 45.0f;
  float pitch_deg_ = 20.0f;
  float distance_ = 3.0f;

  QPoint last_mouse_pos_;

  bool texture_smoothing_ = false;
  QVector<QRgb> quake1_palette_;
  QVector<QRgb> quake2_palette_;
};
