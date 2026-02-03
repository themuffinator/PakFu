#pragma once

#include <QHash>
#include <QString>

struct Quake3SkinMapping {
  // Lowercased surface name -> shader path. Empty value means "*off".
  QHash<QString, QString> surface_to_shader;
};

[[nodiscard]] bool parse_quake3_skin_text(const QString& text, Quake3SkinMapping* out, QString* error = nullptr);
[[nodiscard]] bool parse_quake3_skin_file(const QString& file_path, Quake3SkinMapping* out, QString* error = nullptr);

