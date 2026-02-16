#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include <cstdint>

enum class BspPreviewStyle {
  Lightmapped,
  WireframeFlat,
  Silhouette,
};

enum class BspFamily {
  Unknown = 0,
  Quake1,
  Quake2,
  Quake3,
};

struct BspPreviewResult {
  QImage image;
  QString error;

  [[nodiscard]] bool ok() const { return !image.isNull(); }
};

struct BspMeshVertex {
  QVector3D pos;
  QVector3D normal;
  QColor color;
  QVector2D uv;
  QVector2D lightmap_uv = QVector2D(0.0f, 0.0f);
};

struct BspMeshSurface {
  int first_index = 0;
  int index_count = 0;
  QString texture;
  bool uv_normalized = false;
  int lightmap_index = -1;
};

struct BspMesh {
  QVector<BspMeshVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<BspMeshSurface> surfaces;
  QVector<QImage> lightmaps;
  QVector3D mins;
  QVector3D maxs;
};

[[nodiscard]] BspPreviewResult render_bsp_preview_bytes(const QByteArray& bytes,
                                                        const QString& file_name,
                                                        BspPreviewStyle style,
                                                        int image_size = 1024);
[[nodiscard]] BspPreviewResult render_bsp_preview_file(const QString& file_path,
                                                       BspPreviewStyle style,
                                                       int image_size = 1024);

[[nodiscard]] bool load_bsp_mesh_bytes(const QByteArray& bytes,
                                       const QString& file_name,
                                       BspMesh* out,
                                       QString* error = nullptr,
                                       bool use_lightmap = true);
[[nodiscard]] bool load_bsp_mesh_file(const QString& file_path,
                                      BspMesh* out,
                                      QString* error = nullptr,
                                      bool use_lightmap = true);

[[nodiscard]] QHash<QString, QImage> extract_bsp_embedded_textures_bytes(
    const QByteArray& bytes,
    const QVector<QRgb>* quake_palette,
    QString* error = nullptr);

[[nodiscard]] int bsp_version_bytes(const QByteArray& bytes, QString* error = nullptr);
[[nodiscard]] BspFamily bsp_family_bytes(const QByteArray& bytes, QString* error = nullptr);
