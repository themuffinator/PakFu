#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <cstdint>
#include <optional>

struct ModelVertex {
  float px = 0.0f;
  float py = 0.0f;
  float pz = 0.0f;
  float nx = 0.0f;
  float ny = 0.0f;
  float nz = 1.0f;
  float u = 0.0f;
  float v = 0.0f;
};

struct ModelMesh {
  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector3D mins;
  QVector3D maxs;
};

struct ModelSurface {
  QString name;    // Surface/mesh name (when available).
  QString shader;  // Skin/texture hint path (when available).
  int first_index = 0;
  int index_count = 0;
};

struct LoadedModel {
  QString format;  // "mdl", "md2", "md3", "mdm", "glm", "iqm", "md5mesh", "lwo", "obj"
  int frame_count = 1;
  int surface_count = 1;
  ModelMesh mesh;
  QVector<ModelSurface> surfaces;
  // Optional embedded indexed skin (used by formats like Quake MDL).
  QByteArray embedded_skin_indices;
  int embedded_skin_width = 0;
  int embedded_skin_height = 0;
};

[[nodiscard]] std::optional<LoadedModel> load_model_file(const QString& file_path, QString* error = nullptr);
