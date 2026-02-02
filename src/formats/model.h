#pragma once

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

struct LoadedModel {
  QString format;  // "mdl", "md2", "md3"
  int frame_count = 1;
  int surface_count = 1;
  ModelMesh mesh;
};

[[nodiscard]] std::optional<LoadedModel> load_model_file(const QString& file_path, QString* error = nullptr);
