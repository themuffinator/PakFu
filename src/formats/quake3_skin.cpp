#include "formats/quake3_skin.h"

#include <QFile>

namespace {
QString strip_comments(QString line) {
  const int idx = line.indexOf("//");
  if (idx >= 0) {
    line = line.left(idx);
  }
  return line;
}
}  // namespace

bool parse_quake3_skin_text(const QString& text, Quake3SkinMapping* out, QString* error) {
  if (error) {
    error->clear();
  }
  if (!out) {
    if (error) {
      *error = "Invalid output mapping.";
    }
    return false;
  }

  out->surface_to_shader.clear();
  if (text.isEmpty()) {
    return true;
  }

  const QStringList lines = text.split('\n');
  for (QString line : lines) {
    line.remove('\r');
    line = strip_comments(std::move(line)).trimmed();
    if (line.isEmpty()) {
      continue;
    }
    if (line.startsWith('#')) {
      continue;
    }

    const int comma = line.indexOf(',');
    if (comma < 0) {
      continue;
    }

    const QString surface = line.left(comma).trimmed();
    QString shader = line.mid(comma + 1).trimmed();
    if (surface.isEmpty()) {
      continue;
    }

    const QString surface_key = surface.toLower();
    if (surface_key.startsWith("tag_")) {
      continue;
    }

    if (shader.compare("*off", Qt::CaseInsensitive) == 0) {
      shader.clear();
    }

    out->surface_to_shader.insert(surface_key, shader);
  }

  return true;
}

bool parse_quake3_skin_file(const QString& file_path, Quake3SkinMapping* out, QString* error) {
  if (error) {
    error->clear();
  }
  if (file_path.isEmpty()) {
    if (error) {
      *error = "Empty skin path.";
    }
    return false;
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open .skin file.";
    }
    return false;
  }

  const QByteArray bytes = f.readAll();
  return parse_quake3_skin_text(QString::fromUtf8(bytes), out, error);
}

