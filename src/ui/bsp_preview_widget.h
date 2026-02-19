#pragma once

#include <QtGui/QOpenGLFunctions>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLVertexArrayObject>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QTimer>
#include <QVector2D>
#include <QVector3D>

#include "formats/bsp_preview.h"
#include "ui/preview_3d_options.h"

class QFocusEvent;
class QKeyEvent;

class BspPreviewWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  explicit BspPreviewWidget(QWidget* parent = nullptr);
  ~BspPreviewWidget() override;

  void set_mesh(BspMesh mesh, QHash<QString, QImage> textures = {});
  void set_lightmap_enabled(bool enabled);
  void set_grid_mode(PreviewGridMode mode);
  void set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color);
  void set_wireframe_enabled(bool enabled);
  void set_textured_enabled(bool enabled);
  void set_fov_degrees(int degrees);
  [[nodiscard]] PreviewCameraState camera_state() const;
  void set_camera_state(const PreviewCameraState& state);
  void clear();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

 private:
  struct GpuVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
    float u, v;
    float lu, lv;
  };

  struct GridLineVertex {
    float px, py, pz;
    float r, g, b, a;
  };

  struct DrawSurface {
    int first_index = 0;
    int index_count = 0;
    QString texture;
    bool uv_normalized = false;
    int lightmap_index = -1;
    QVector2D tex_scale = QVector2D(1.0f, 1.0f);
    QVector2D tex_offset = QVector2D(0.0f, 0.0f);
    GLuint texture_id = 0;
    bool has_texture = false;
    bool has_lightmap = false;
  };

  void reset_camera_from_mesh();
  void frame_mesh();
  void pan_by_pixels(const QPoint& delta);
  void dolly_by_pixels(const QPoint& delta);
  void on_fly_tick();
  void set_fly_key(int key, bool down);
  void upload_mesh_if_possible();
  void upload_textures_if_possible();
  void destroy_gl_resources();
  void ensure_program();
  void ensure_grid_program();
  void update_ground_mesh_if_needed();
  void update_grid_lines_if_needed(const QVector3D& cam_pos, float aspect);
  void update_background_mesh_if_needed();
  void update_grid_settings();
  void apply_wireframe_state(bool enabled);
  void update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const;
  void update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const;

  enum class DragMode {
    None,
    Orbit,
    Pan,
    Dolly,
    Look,
  };

  BspMesh mesh_;
  bool has_mesh_ = false;
  bool pending_upload_ = false;
  bool pending_texture_upload_ = false;

  QOpenGLShaderProgram program_;
  QOpenGLShaderProgram grid_program_;
  QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLBuffer ground_vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ground_ibo_{QOpenGLBuffer::IndexBuffer};
  QOpenGLBuffer bg_vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject vao_;
  QOpenGLVertexArrayObject bg_vao_;
  bool gl_ready_ = false;
  int index_count_ = 0;
  int ground_index_count_ = 0;
  int grid_vertex_count_ = 0;
  float ground_extent_ = 0.0f;
  float ground_z_ = 0.0f;
  float grid_scale_ = 1.0f;
  float grid_step_ = 0.0f;
  int grid_center_i_ = 0;
  int grid_center_j_ = 0;
  int grid_half_lines_ = 0;
  QVector3D grid_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
  QVector3D axis_x_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
  QVector3D axis_y_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
  QVector<DrawSurface> surfaces_;
  QVector<GLuint> lightmap_textures_;
  QHash<QString, QImage> textures_;

  bool lightmap_enabled_ = true;
  PreviewGridMode grid_mode_ = PreviewGridMode::Floor;
  PreviewBackgroundMode bg_mode_ = PreviewBackgroundMode::Themed;
  QColor bg_custom_color_;
  bool wireframe_enabled_ = false;
  bool textured_enabled_ = true;
  QVector3D center_ = QVector3D(0, 0, 0);
  float radius_ = 1.0f;
  float yaw_deg_ = 45.0f;
  float pitch_deg_ = 55.0f;
  float distance_ = 3.0f;
  float fov_y_deg_ = 100.0f;
  bool camera_fit_pending_ = false;

  QTimer fly_timer_;
  QElapsedTimer fly_elapsed_;
  qint64 fly_last_nsecs_ = 0;
  float fly_speed_ = 640.0f;
  int fly_move_mask_ = 0;

  QPoint last_mouse_pos_;
  DragMode drag_mode_ = DragMode::None;
  Qt::MouseButtons drag_buttons_ = Qt::NoButton;
};
