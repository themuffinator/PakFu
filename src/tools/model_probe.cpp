#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "formats/model.h"

namespace {
struct TokenCursor {
  QByteArray bytes;
  int pos = 0;

  [[nodiscard]] bool at_end() const { return pos >= bytes.size(); }

  void skip_ws_and_comments() {
    while (pos < bytes.size()) {
      const char c = bytes[pos];
      if (c == '/' && pos + 1 < bytes.size() && bytes[pos + 1] == '/') {
        pos += 2;
        while (pos < bytes.size() && bytes[pos] != '\n') {
          ++pos;
        }
        continue;
      }
      if (c == '/' && pos + 1 < bytes.size() && bytes[pos + 1] == '*') {
        pos += 2;
        while (pos + 1 < bytes.size()) {
          if (bytes[pos] == '*' && bytes[pos + 1] == '/') {
            pos += 2;
            break;
          }
          ++pos;
        }
        continue;
      }
      if (static_cast<unsigned char>(c) <= 0x20) {
        ++pos;
        continue;
      }
      break;
    }
  }

  [[nodiscard]] QString next() {
    skip_ws_and_comments();
    if (at_end()) {
      return {};
    }

    const char c = bytes[pos];
    if (c == '{' || c == '}' || c == '(' || c == ')') {
      ++pos;
      return QString(QChar::fromLatin1(c));
    }

    if (c == '"') {
      ++pos;
      const int start = pos;
      while (pos < bytes.size() && bytes[pos] != '"') {
        ++pos;
      }
      const int end = pos;
      if (pos < bytes.size() && bytes[pos] == '"') {
        ++pos;
      }
      return QString::fromLatin1(bytes.constData() + start, end - start);
    }

    const int start = pos;
    while (pos < bytes.size()) {
      const char cc = bytes[pos];
      if (static_cast<unsigned char>(cc) <= 0x20) {
        break;
      }
      if (cc == '{' || cc == '}' || cc == '(' || cc == ')' || cc == '"') {
        break;
      }
      ++pos;
    }
    return QString::fromLatin1(bytes.constData() + start, pos - start);
  }
};
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  QTextStream err(stderr);

  const QStringList args = app.arguments();
  if (args.size() < 2) {
    err << "Usage: model_probe <file>\n";
    err << "       model_probe --tokens <file>\n";
    return 2;
  }

  bool dump_tokens = false;
  int file_arg = 1;
  if (args.size() >= 3 && args[1] == "--tokens") {
    dump_tokens = true;
    file_arg = 2;
  }

  const QString file_path = QFileInfo(args[file_arg]).absoluteFilePath();

  if (dump_tokens) {
    QFile f(file_path);
    if (!f.open(QIODevice::ReadOnly)) {
      err << "Unable to open file.\n";
      return 2;
    }

    TokenCursor cur;
    cur.bytes = f.readAll();

    int count = 0;
    while (!cur.at_end()) {
      const QString t = cur.next();
      if (t.isEmpty()) {
        break;
      }
      out << count << ": " << t << "\n";
      ++count;
      if (count >= 200) {
        out << "...\n";
        break;
      }
    }
    return 0;
  }

  QString load_err;
  const std::optional<LoadedModel> model = load_model_file(file_path, &load_err);
  if (!model) {
    err << (load_err.isEmpty() ? "Unable to load model.\n" : (load_err + "\n"));
    return 2;
  }

  out << "Format: " << model->format << "\n";
  out << "Frames: " << model->frame_count << "\n";
  out << "Surfaces: " << model->surface_count << " (declared=" << model->surfaces.size() << ")\n";
  out << "Vertices: " << model->mesh.vertices.size() << "\n";
  out << "Indices: " << model->mesh.indices.size() << "\n";
  out << "Bounds: mins=(" << model->mesh.mins.x() << "," << model->mesh.mins.y() << "," << model->mesh.mins.z()
      << ") maxs=(" << model->mesh.maxs.x() << "," << model->mesh.maxs.y() << "," << model->mesh.maxs.z() << ")\n";

  const int max_surfaces = 12;
  for (int i = 0; i < model->surfaces.size() && i < max_surfaces; ++i) {
    const ModelSurface& s = model->surfaces[i];
    out << "Surface " << i << ": name=" << s.name << " shader=" << s.shader << " first=" << s.first_index
        << " count=" << s.index_count << "\n";
  }

  return 0;
}
