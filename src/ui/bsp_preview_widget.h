#pragma once

#include <QtGui/QOpenGLFunctions>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLVertexArrayObject>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QVector2D>
#include <QVector3D>

#include "formats/bsp_preview.h"

class BspPreviewWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
  explicit BspPreviewWidget(QWidget* parent = nullptr);
  ~BspPreviewWidget() override;

  void set_mesh(BspMesh mesh, QHash<QString, QImage> textures = {});
  void clear();

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

private:
  struct GpuVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
    float u, v;
  };

  struct DrawSurface {
    int first_index = 0;
    int index_count = 0;
    QString texture;
    bool uv_normalized = false;
    QVector2D tex_scale = QVector2D(1.0f, 1.0f);
    QVector2D tex_offset = QVector2D(0.0f, 0.0f);
    GLuint texture_id = 0;
    bool has_texture = false;
  };

  void reset_camera_from_mesh();
  void upload_mesh_if_possible();
  void upload_textures_if_possible();
  void destroy_gl_resources();
  void ensure_program();

  BspMesh mesh_;
  bool has_mesh_ = false;
  bool pending_upload_ = false;
  bool pending_texture_upload_ = false;

  QOpenGLShaderProgram program_;
  QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLVertexArrayObject vao_;
  bool gl_ready_ = false;
  int index_count_ = 0;
  QVector<DrawSurface> surfaces_;
  QHash<QString, QImage> textures_;

  QVector3D center_ = QVector3D(0, 0, 0);
  float radius_ = 1.0f;
  float yaw_deg_ = 45.0f;
  float pitch_deg_ = 55.0f;
  float distance_ = 3.0f;

  QPoint last_mouse_pos_;
};
