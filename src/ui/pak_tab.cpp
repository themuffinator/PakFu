#include "pak_tab.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QAbstractScrollArea>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QKeySequence>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMatrix4x4>
#include <QPainter>
#include <QPolygonF>
#include <QProgressDialog>
#include <QPushButton>
#include <QRunnable>
#include <QShortcut>
#include <QSet>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QTimeZone>
#include <QBrush>
#include <QSaveFile>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "archive/path_safety.h"
#include "formats/cinematic.h"
#include "formats/lmp_image.h"
#include "formats/model.h"
#include "formats/pcx_image.h"
#include "third_party/miniz/miniz.h"
#include "pak/pak_archive.h"
#include "ui/breadcrumb_bar.h"
#include "ui/preview_pane.h"
#include "zip/quakelive_pk3_crypto.h"

namespace {
struct ChildListing {
  QString name;
  QString source_path;
  bool is_dir = false;
  quint32 size = 0;
  qint64 mtime_utc_secs = -1;
  bool is_added = false;
  bool is_overridden = false;
};

[[nodiscard]] size_t mz_read_qfile(void* opaque, mz_uint64 file_ofs, void* buf, size_t n) {
  auto* f = static_cast<QFile*>(opaque);
  if (!f || !f->isOpen()) {
    return 0;
  }
  if (!f->seek(static_cast<qint64>(file_ofs))) {
    return 0;
  }
  const qint64 got = f->read(static_cast<char*>(buf), static_cast<qint64>(n));
  return got > 0 ? static_cast<size_t>(got) : 0;
}

[[nodiscard]] size_t mz_write_qiodevice(void* opaque, mz_uint64 file_ofs, const void* buf, size_t n) {
  auto* dev = static_cast<QIODevice*>(opaque);
  if (!dev || !dev->isOpen()) {
    return 0;
  }
  if (!dev->seek(static_cast<qint64>(file_ofs))) {
    return 0;
  }
  const qint64 wrote = dev->write(static_cast<const char*>(buf), static_cast<qint64>(n));
  return wrote > 0 ? static_cast<size_t>(wrote) : 0;
}

[[nodiscard]] mz_bool mz_keepalive_qiodevice(void* opaque) {
  (void)opaque;
  return MZ_TRUE;
}

QString format_size(quint32 size) {
  constexpr quint32 kKiB = 1024;
  constexpr quint32 kMiB = 1024 * 1024;
  if (size >= kMiB) {
    return QString("%1 MiB").arg(QString::number(static_cast<double>(size) / kMiB, 'f', 1));
  }
  if (size >= kKiB) {
    return QString("%1 KiB").arg(QString::number(static_cast<double>(size) / kKiB, 'f', 1));
  }
  return QString("%1 B").arg(size);
}

QString join_prefix(const QStringList& parts) {
  if (parts.isEmpty()) {
    return {};
  }
  return parts.join('/') + '/';
}

QVector<ChildListing> list_children(const QVector<ArchiveEntry>& entries,
                                    const QHash<QString, quint32>& added_sizes,
                                    const QHash<QString, QString>& added_sources,
                                    const QHash<QString, qint64>& added_mtimes,
                                    const QSet<QString>& virtual_dirs,
                                    const QSet<QString>& deleted_files,
                                    const QSet<QString>& deleted_dirs,
                                    const QStringList& dir) {
  const QString prefix = join_prefix(dir);
  QSet<QString> dirs;
  QHash<QString, ChildListing> files;

  for (const ArchiveEntry& e : entries) {
    if (deleted_files.contains(e.name)) {
      continue;
    }
    bool deleted_by_dir = false;
    for (const QString& d : deleted_dirs) {
      if (!d.isEmpty() && e.name.startsWith(d)) {
        deleted_by_dir = true;
        break;
      }
    }
    if (deleted_by_dir) {
      continue;
    }
    if (!prefix.isEmpty() && !e.name.startsWith(prefix)) {
      continue;
    }
    const QString rest = prefix.isEmpty() ? e.name : e.name.mid(prefix.size());
    if (rest.isEmpty()) {
      continue;
    }
    const int slash = rest.indexOf('/');
    if (slash >= 0) {
      const QString dir_name = rest.left(slash);
      if (!dir_name.isEmpty()) {
        dirs.insert(dir_name);
      }
      continue;
    }
    ChildListing item;
    item.name = rest;
    item.is_dir = false;
    item.size = e.size;
    item.mtime_utc_secs = e.mtime_utc_secs;
    files.insert(rest, item);
  }

  for (auto it = added_sizes.cbegin(); it != added_sizes.cend(); ++it) {
    const QString full_name = it.key();
    if (deleted_files.contains(full_name)) {
      continue;
    }
    bool deleted_by_dir = false;
    for (const QString& d : deleted_dirs) {
      if (!d.isEmpty() && full_name.startsWith(d)) {
        deleted_by_dir = true;
        break;
      }
    }
    if (deleted_by_dir) {
      continue;
    }
    if (!prefix.isEmpty() && !full_name.startsWith(prefix)) {
      continue;
    }
    const QString rest = prefix.isEmpty() ? full_name : full_name.mid(prefix.size());
    if (rest.isEmpty()) {
      continue;
    }
    const int slash = rest.indexOf('/');
    if (slash >= 0) {
      const QString dir_name = rest.left(slash);
      if (!dir_name.isEmpty()) {
        dirs.insert(dir_name);
      }
      continue;
    }

    auto existing = files.find(rest);
    if (existing != files.end()) {
      existing->is_overridden = true;
      existing->is_added = true;
      existing->size = it.value();
      existing->source_path = added_sources.value(full_name);
      existing->mtime_utc_secs = added_mtimes.value(full_name, -1);
    } else {
      ChildListing item;
      item.name = rest;
      item.is_dir = false;
      item.size = it.value();
      item.is_added = true;
      item.source_path = added_sources.value(full_name);
      item.mtime_utc_secs = added_mtimes.value(full_name, -1);
      files.insert(rest, item);
    }
  }

  for (const QString& vdir : virtual_dirs) {
    if (deleted_files.contains(vdir)) {
      continue;
    }
    bool deleted_by_dir = false;
    for (const QString& d : deleted_dirs) {
      if (!d.isEmpty() && vdir.startsWith(d)) {
        deleted_by_dir = true;
        break;
      }
    }
    if (deleted_by_dir) {
      continue;
    }
    if (!prefix.isEmpty() && !vdir.startsWith(prefix)) {
      continue;
    }
    const QString rest = prefix.isEmpty() ? vdir : vdir.mid(prefix.size());
    if (rest.isEmpty()) {
      continue;
    }
    const int slash = rest.indexOf('/');
    const QString dir_name = slash >= 0 ? rest.left(slash) : rest;
    if (!dir_name.isEmpty()) {
      dirs.insert(dir_name);
    }
  }

  QVector<ChildListing> out;
  out.reserve(dirs.size() + files.size());

  for (const QString& d : dirs) {
    ChildListing item;
    item.name = d;
    item.is_dir = true;
    out.push_back(item);
  }
  for (auto it = files.cbegin(); it != files.cend(); ++it) {
    out.push_back(it.value());
  }

  std::sort(out.begin(), out.end(), [](const ChildListing& a, const ChildListing& b) {
    if (a.is_dir != b.is_dir) {
      return a.is_dir > b.is_dir;
    }
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });

  return out;
}

constexpr int kPakHeaderSize = 12;
constexpr int kPakDirEntrySize = 64;
constexpr int kPakNameBytes = 56;

void write_u32_le(QByteArray* bytes, int offset, quint32 value) {
  if (!bytes || offset < 0 || offset + 4 > bytes->size()) {
    return;
  }
  (*bytes)[offset + 0] = static_cast<char>(value & 0xFF);
  (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
  (*bytes)[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
  (*bytes)[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

QString normalize_pak_path(QString path) {
  return normalize_archive_entry_name(std::move(path));
}

bool is_safe_entry_name(const QString& name) {
  return is_safe_archive_entry_name(name);
}

constexpr int kRoleIsDir = Qt::UserRole;
constexpr int kRolePakPath = Qt::UserRole + 1;
constexpr int kRoleSize = Qt::UserRole + 2;
constexpr int kRoleMtime = Qt::UserRole + 3;
constexpr int kRoleIsAdded = Qt::UserRole + 4;
constexpr int kRoleIsOverridden = Qt::UserRole + 5;

constexpr char kPakFuMimeType[] = "application/x-pakfu-items";

class PakTreeItem : public QTreeWidgetItem {
public:
  using QTreeWidgetItem::QTreeWidgetItem;

  bool operator<(const QTreeWidgetItem& other) const override {
    const int col = treeWidget() ? treeWidget()->sortColumn() : 0;

    auto clean_name = [](QString s) -> QString {
      if (s.endsWith('/')) {
        s.chop(1);
      }
      return s;
    };

    auto ext_lower = [&](const QTreeWidgetItem& it) -> QString {
      QString s = clean_name(it.text(0)).toLower();
      const int dot = s.lastIndexOf('.');
      return dot >= 0 ? s.mid(dot + 1) : QString();
    };

    auto sort_group = [&](const QTreeWidgetItem& it) -> int {
      const bool is_dir = it.data(0, kRoleIsDir).toBool();
      if (is_dir) {
        return 0;  // folders
      }
      const QString ext = ext_lower(it);
      if (ext == "wad") {
        return 1;  // wad files
      }
      return 2;  // all other files
    };

    const int ga = sort_group(*this);
    const int gb = sort_group(other);
    if (ga != gb) {
      return ga < gb;
    }

    if (col == 1) {
      const qint64 a = data(1, kRoleSize).toLongLong();
      const qint64 b = other.data(1, kRoleSize).toLongLong();
      if (a != b) {
        return a < b;
      }
    } else if (col == 2) {
      const qint64 a = data(2, kRoleMtime).toLongLong();
      const qint64 b = other.data(2, kRoleMtime).toLongLong();
      const bool a_unknown = a < 0;
      const bool b_unknown = b < 0;
      if (a_unknown != b_unknown) {
        return (!a_unknown && b_unknown);
      }
      if (a != b) {
        return a < b;
      }
    }

    const QString a_name = clean_name(text(0));
    const QString b_name = clean_name(other.text(0));
    return a_name.compare(b_name, Qt::CaseInsensitive) < 0;
  }
};

class PakIconItem final : public QListWidgetItem {
public:
  using QListWidgetItem::QListWidgetItem;

  bool operator<(const QListWidgetItem& other) const override {
    auto clean_name = [](QString s) -> QString {
      if (s.endsWith('/')) {
        s.chop(1);
      }
      return s;
    };

    auto ext_lower = [&](const QListWidgetItem& it) -> QString {
      QString s = clean_name(it.text()).toLower();
      const int dot = s.lastIndexOf('.');
      return dot >= 0 ? s.mid(dot + 1) : QString();
    };

    auto sort_group = [&](const QListWidgetItem& it) -> int {
      const bool is_dir = it.data(kRoleIsDir).toBool();
      if (is_dir) {
        return 0;
      }
      const QString ext = ext_lower(it);
      if (ext == "wad") {
        return 1;
      }
      return 2;
    };

    const int ga = sort_group(*this);
    const int gb = sort_group(other);
    if (ga != gb) {
      return ga < gb;
    }

    const QString a_name = clean_name(text());
    const QString b_name = clean_name(other.text());
    return a_name.compare(b_name, Qt::CaseInsensitive) < 0;
  }
};

QString format_mtime(qint64 utc_secs) {
  if (utc_secs < 0) {
    return "-";
  }
  const QDateTime utc = QDateTime::fromSecsSinceEpoch(utc_secs, QTimeZone::UTC);
  return utc.toLocalTime().toString("yyyy-MM-dd HH:mm");
}

bool is_image_file_name(const QString& name) {
  const QString lower = name.toLower();
  const int dot = lower.lastIndexOf('.');
  if (dot < 0) {
    return false;
  }
  const QString ext = lower.mid(dot + 1);
  static const QSet<QString> kImageExts = {
    "png", "jpg", "jpeg", "bmp", "gif", "tga", "pcx", "wal", "dds", "lmp", "mip", "tif", "tiff"
  };
  return kImageExts.contains(ext);
}

QIcon make_badged_icon(const QIcon& base, const QSize& icon_size, const QString& badge, const QPalette& pal) {
  if (!icon_size.isValid()) {
    return base;
  }
  if (badge.isEmpty() || icon_size.width() < 24 || icon_size.height() < 24) {
    return base;
  }

  QPixmap pm = base.pixmap(icon_size);
  if (pm.isNull()) {
    pm = QPixmap(icon_size);
    pm.fill(Qt::transparent);
  }

  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  QFont f = p.font();
  f.setBold(true);
  f.setPixelSize(qMax(9, icon_size.height() / 3));
  p.setFont(f);

  QColor c = pal.color(QPalette::Highlight);
  if (!c.isValid()) {
    c = pal.color(QPalette::Text);
  }
  c.setAlpha(230);
  p.setPen(c);

  QRect r = pm.rect();
  r.adjust(0, icon_size.height() / 3, 0, 0);
  p.drawText(r, Qt::AlignHCenter | Qt::AlignBottom, badge);

  return QIcon(pm);
}

QIcon make_archive_icon(const QIcon& base, const QSize& icon_size, const QPalette& pal) {
  if (!icon_size.isValid()) {
    return base;
  }

  QPixmap pm = base.pixmap(icon_size);
  if (pm.isNull()) {
    pm = QPixmap(icon_size);
    pm.fill(Qt::transparent);
  }

  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);

  QColor fill = pal.color(QPalette::Highlight);
  if (!fill.isValid()) {
    fill = pal.color(QPalette::Text);
  }
  fill.setAlpha(110);

  QColor stroke = fill;
  stroke.setAlpha(200);

  const int w = icon_size.width();
  const int h = icon_size.height();
  const int box_h = qMax(6, h / 5);
  QRect box(static_cast<int>(w * 0.18f), h - box_h - 2, static_cast<int>(w * 0.64f), box_h);

  QPen pen(stroke);
  pen.setWidth(qMax(1, w / 32));
  p.setPen(pen);
  p.setBrush(fill);
  p.drawRoundedRect(box, 2.0, 2.0);

  // Simple "zipper" to hint that this is an archive.
  QColor zip = pal.color(QPalette::Base);
  if (!zip.isValid()) {
    zip = Qt::white;
  }
  zip.setAlpha(180);
  p.setPen(QPen(zip, qMax(1, w / 64)));
  const int zx = box.center().x();
  for (int yy = box.top() + 2; yy < box.bottom() - 1; yy += qMax(3, box_h / 4)) {
    p.drawPoint(zx, yy);
  }

  return QIcon(pm);
}

[[nodiscard]] QImage render_model_thumbnail(const LoadedModel& model, const QSize& size) {
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    return {};
  }
  if (model.mesh.vertices.isEmpty() || model.mesh.indices.size() < 3) {
    return {};
  }

  QImage img(size, QImage::Format_ARGB32_Premultiplied);
  if (img.isNull()) {
    return {};
  }
  img.fill(Qt::transparent);

  const QVector3D mins = model.mesh.mins;
  const QVector3D maxs = model.mesh.maxs;
  const QVector3D center = (mins + maxs) * 0.5f;
  const QVector3D ext = (maxs - mins);
  const float radius = qMax(0.001f, 0.5f * qMax(ext.x(), qMax(ext.y(), ext.z())));

  const float aspect = static_cast<float>(size.width()) / static_cast<float>(size.height());
  const float dist = radius * 3.2f;

  QMatrix4x4 m;
  m.translate(-center);
  m.rotate(35.0f, 1.0f, 0.0f, 0.0f);
  m.rotate(45.0f, 0.0f, 1.0f, 0.0f);

  QMatrix4x4 v;
  v.translate(0.0f, 0.0f, -dist);

  QMatrix4x4 pmat;
  pmat.perspective(45.0f, aspect, qMax(0.001f, radius * 0.02f), qMax(10.0f, radius * 50.0f));

  const QMatrix4x4 mvp = pmat * v * m;
  const QVector3D light_dir = QVector3D(-0.3f, 0.5f, 1.0f).normalized();

  struct Tri {
    QPointF p0;
    QPointF p1;
    QPointF p2;
    float depth = 0.0f;
    QColor color;
  };

  const int tri_count = model.mesh.indices.size() / 3;
  const int max_tris = 9000;
  const int stride = (tri_count > max_tris) ? qMax(1, tri_count / max_tris) : 1;

  QVector<Tri> tris;
  tris.reserve(qMin(tri_count, max_tris));

  const int vw = model.mesh.vertices.size();
  for (int t = 0; t < tri_count; t += stride) {
    const int base = t * 3;
    const std::uint32_t i0u = model.mesh.indices[base + 0];
    const std::uint32_t i1u = model.mesh.indices[base + 1];
    const std::uint32_t i2u = model.mesh.indices[base + 2];
    if (i0u >= static_cast<std::uint32_t>(vw) || i1u >= static_cast<std::uint32_t>(vw) ||
        i2u >= static_cast<std::uint32_t>(vw)) {
      continue;
    }

    const ModelVertex& v0 = model.mesh.vertices[static_cast<int>(i0u)];
    const ModelVertex& v1 = model.mesh.vertices[static_cast<int>(i1u)];
    const ModelVertex& v2 = model.mesh.vertices[static_cast<int>(i2u)];

    const QVector3D p0(v0.px, v0.py, v0.pz);
    const QVector3D p1(v1.px, v1.py, v1.pz);
    const QVector3D p2(v2.px, v2.py, v2.pz);

    const QVector4D c0 = mvp * QVector4D(p0, 1.0f);
    const QVector4D c1 = mvp * QVector4D(p1, 1.0f);
    const QVector4D c2 = mvp * QVector4D(p2, 1.0f);
    if (c0.w() <= 0.0f || c1.w() <= 0.0f || c2.w() <= 0.0f) {
      continue;
    }

    const QVector3D n0 = m.mapVector(QVector3D(v0.nx, v0.ny, v0.nz)).normalized();
    const QVector3D n1 = m.mapVector(QVector3D(v1.nx, v1.ny, v1.nz)).normalized();
    const QVector3D n2 = m.mapVector(QVector3D(v2.nx, v2.ny, v2.nz)).normalized();
    const QVector3D nn = (n0 + n1 + n2).normalized();

    const float ndotl = qMax(0.0f, QVector3D::dotProduct(nn, light_dir));
    const float ambient = 0.25f;
    const float lit = qBound(0.0f, ambient + ndotl * 0.75f, 1.0f);

    auto to_screen = [&](const QVector4D& clip) -> QPointF {
      const float invw = 1.0f / clip.w();
      const float x = clip.x() * invw;
      const float y = clip.y() * invw;
      const float sx = (x * 0.5f + 0.5f) * static_cast<float>(size.width());
      const float sy = (1.0f - (y * 0.5f + 0.5f)) * static_cast<float>(size.height());
      return QPointF(sx, sy);
    };

    auto ndc_z = [&](const QVector4D& clip) -> float { return clip.z() / clip.w(); };
    const float z = (ndc_z(c0) + ndc_z(c1) + ndc_z(c2)) / 3.0f;

    // Basic backface cull in screen space.
    const QPointF s0 = to_screen(c0);
    const QPointF s1 = to_screen(c1);
    const QPointF s2 = to_screen(c2);
    const QPointF e1 = s1 - s0;
    const QPointF e2 = s2 - s0;
    const float area2 = static_cast<float>(e1.x() * e2.y() - e1.y() * e2.x());
    if (area2 >= 0.0f) {
      continue;
    }

    const int shade = static_cast<int>(40 + lit * 190.0f);
    Tri tri;
    tri.p0 = s0;
    tri.p1 = s1;
    tri.p2 = s2;
    tri.depth = z;
    tri.color = QColor(shade, shade, shade, 235);
    tris.push_back(std::move(tri));
  }

  if (tris.isEmpty()) {
    return img;
  }

  std::sort(tris.begin(), tris.end(), [](const Tri& a, const Tri& b) {
    // NDC z: -1 near, +1 far -> draw far-to-near.
    return a.depth > b.depth;
  });

  QPainter painter(&img);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  QColor outline = Qt::black;
  outline.setAlpha(55);
  painter.setPen(QPen(outline, 1));

  for (const Tri& tri : tris) {
    painter.setBrush(tri.color);
    QPolygonF poly;
    poly.reserve(3);
    poly.push_back(tri.p0);
    poly.push_back(tri.p1);
    poly.push_back(tri.p2);
    painter.drawPolygon(poly);
  }

  return img;
}

QString pak_leaf_name(QString pak_path) {
  pak_path = normalize_pak_path(pak_path);
  if (pak_path.endsWith('/')) {
    pak_path.chop(1);
  }
  const int slash = pak_path.lastIndexOf('/');
  return (slash >= 0) ? pak_path.mid(slash + 1) : pak_path;
}

QString file_ext_lower(const QString& name) {
  const QString lower = name.toLower();
  const int dot = lower.lastIndexOf('.');
  return dot >= 0 ? lower.mid(dot + 1) : QString();
}

/*
=============
is_supported_audio_file

Return true when a file name uses a supported audio extension.
=============
*/
bool is_supported_audio_file(const QString& name) {
	const QString ext = file_ext_lower(name);
	return (ext == "wav" || ext == "ogg" || ext == "mp3");
}

bool is_video_file_name(const QString& name) {
	const QString ext = file_ext_lower(name);
	return (ext == "cin" || ext == "roq" || ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "ogv" || ext == "webm");
}

bool is_model_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  return (ext == "mdl" || ext == "md2" || ext == "md3" || ext == "iqm" || ext == "md5mesh");
}

bool is_text_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  static const QSet<QString> kTextExts = {
    "cfg", "txt", "log", "md", "ini", "json", "xml", "shader", "menu", "script"
  };
  return kTextExts.contains(ext);
}

bool looks_like_text(const QByteArray& bytes) {
  if (bytes.isEmpty()) {
    return true;
  }
  int printable = 0;
  int control = 0;
  for (const char c : bytes) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == 0) {
      return false;
    }
    if (u == '\n' || u == '\r' || u == '\t') {
      ++printable;
      continue;
    }
    if (u >= 32 && u < 127) {
      ++printable;
      continue;
    }
    if (u < 32) {
      ++control;
    }
  }
  const int total = bytes.size();
  if (total <= 0) {
    return true;
  }
  return (printable * 100) / total >= 85 && control * 100 / total < 5;
}
}  // namespace

class PakTabStateCommand : public QUndoCommand {
public:
  PakTabStateCommand(PakTab* tab,
                     const QString& text,
                     const QVector<PakTab::AddedFile>& before_added,
                     const QSet<QString>& before_virtual_dirs,
                     const QSet<QString>& before_deleted_files,
                     const QSet<QString>& before_deleted_dirs,
                     const QVector<PakTab::AddedFile>& after_added,
                     const QSet<QString>& after_virtual_dirs,
                     const QSet<QString>& after_deleted_files,
                     const QSet<QString>& after_deleted_dirs)
      : QUndoCommand(text),
        tab_(tab),
        before_added_(before_added),
        before_virtual_dirs_(before_virtual_dirs),
        before_deleted_files_(before_deleted_files),
        before_deleted_dirs_(before_deleted_dirs),
        after_added_(after_added),
        after_virtual_dirs_(after_virtual_dirs),
        after_deleted_files_(after_deleted_files),
        after_deleted_dirs_(after_deleted_dirs) {}

  void undo() override {
    apply(before_added_, before_virtual_dirs_, before_deleted_files_, before_deleted_dirs_);
  }

  void redo() override {
    if (first_redo_) {
      first_redo_ = false;
      return;  // state already applied before push()
    }
    apply(after_added_, after_virtual_dirs_, after_deleted_files_, after_deleted_dirs_);
  }

private:
  void apply(const QVector<PakTab::AddedFile>& added,
             const QSet<QString>& virtual_dirs,
             const QSet<QString>& deleted_files,
             const QSet<QString>& deleted_dirs) {
    if (!tab_) {
      return;
    }
    tab_->added_files_ = added;
    tab_->virtual_dirs_ = virtual_dirs;
    tab_->deleted_files_ = deleted_files;
    tab_->deleted_dir_prefixes_ = deleted_dirs;
    tab_->rebuild_added_index();
    tab_->refresh_listing();
  }

  PakTab* tab_ = nullptr;
  QVector<PakTab::AddedFile> before_added_;
  QSet<QString> before_virtual_dirs_;
  QSet<QString> before_deleted_files_;
  QSet<QString> before_deleted_dirs_;
  QVector<PakTab::AddedFile> after_added_;
  QSet<QString> after_virtual_dirs_;
  QSet<QString> after_deleted_files_;
  QSet<QString> after_deleted_dirs_;
  bool first_redo_ = true;
};

class PakTabDetailsView : public QTreeWidget {
public:
  explicit PakTabDetailsView(PakTab* tab, QWidget* parent = nullptr) : QTreeWidget(parent), tab_(tab) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
  }

protected:
  QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
    if (!tab_) {
      return nullptr;
    }
    QVector<QPair<QString, bool>> selected;
    selected.reserve(items.size());
    for (const QTreeWidgetItem* item : items) {
      if (!item) {
        continue;
      }
      const QString pak_path = item->data(0, kRolePakPath).toString();
      const bool is_dir = item->data(0, kRoleIsDir).toBool();
      if (!pak_path.isEmpty()) {
        selected.push_back(qMakePair(pak_path, is_dir));
      }
    }

    QStringList failures;
    return tab_->make_mime_data_for_items(selected, false, &failures);
  }

  void dragEnterEvent(QDragEnterEvent* event) override {
    if (event && event->mimeData() && event->mimeData()->hasUrls()) {
      event->acceptProposedAction();
      return;
    }
    QTreeWidget::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    if (event && event->mimeData() && event->mimeData()->hasUrls()) {
      event->acceptProposedAction();
      return;
    }
    QTreeWidget::dragMoveEvent(event);
  }

  void dropEvent(QDropEvent* event) override {
    if (!event || !tab_ || !event->mimeData() || !event->mimeData()->hasUrls()) {
      QTreeWidget::dropEvent(event);
      return;
    }

    QString dest_prefix = tab_->current_prefix();
    if (QTreeWidgetItem* target = itemAt(event->position().toPoint())) {
      if (target->data(0, kRoleIsDir).toBool()) {
        const QString pak_path = target->data(0, kRolePakPath).toString();
        if (!pak_path.isEmpty()) {
          dest_prefix = pak_path;
        }
      }
    }

    tab_->import_urls_with_undo(event->mimeData()->urls(), dest_prefix, "Drop");
    event->acceptProposedAction();
  }

private:
  PakTab* tab_ = nullptr;
};

class PakTabIconView : public QListWidget {
public:
  explicit PakTabIconView(PakTab* tab, QWidget* parent = nullptr) : QListWidget(parent), tab_(tab) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
  }

protected:
  QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
    if (!tab_) {
      return nullptr;
    }
    QVector<QPair<QString, bool>> selected;
    selected.reserve(items.size());
    for (const QListWidgetItem* item : items) {
      if (!item) {
        continue;
      }
      const QString pak_path = item->data(kRolePakPath).toString();
      const bool is_dir = item->data(kRoleIsDir).toBool();
      if (!pak_path.isEmpty()) {
        selected.push_back(qMakePair(pak_path, is_dir));
      }
    }

    QStringList failures;
    return tab_->make_mime_data_for_items(selected, false, &failures);
  }

  void dragEnterEvent(QDragEnterEvent* event) override {
    if (event && event->mimeData() && event->mimeData()->hasUrls()) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    if (event && event->mimeData() && event->mimeData()->hasUrls()) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragMoveEvent(event);
  }

  void dropEvent(QDropEvent* event) override {
    if (!event || !tab_ || !event->mimeData() || !event->mimeData()->hasUrls()) {
      QListWidget::dropEvent(event);
      return;
    }

    QString dest_prefix = tab_->current_prefix();
    if (QListWidgetItem* target = itemAt(event->position().toPoint())) {
      if (target->data(kRoleIsDir).toBool()) {
        const QString pak_path = target->data(kRolePakPath).toString();
        if (!pak_path.isEmpty()) {
          dest_prefix = pak_path;
        }
      }
    }

    tab_->import_urls_with_undo(event->mimeData()->urls(), dest_prefix, "Drop");
    event->acceptProposedAction();
  }

private:
  PakTab* tab_ = nullptr;
};

PakTab::PakTab(Mode mode, const QString& pak_path, QWidget* parent)
    : QWidget(parent), mode_(mode), pak_path_(pak_path) {
  thumbnail_pool_.setMaxThreadCount(1);
  build_ui();
  if (mode_ == Mode::ExistingPak) {
    load_archive();
  } else {
    loaded_ = true;
    set_dirty(false);
    refresh_listing();
  }
}

PakTab::~PakTab() {
  stop_thumbnail_generation();
  thumbnail_pool_.waitForDone();
}

QUndoStack* PakTab::undo_stack() const {
  return undo_stack_;
}

void PakTab::cut() {
  copy_selected(true);
}

void PakTab::copy() {
  copy_selected(false);
}

void PakTab::paste() {
  paste_from_clipboard();
}

void PakTab::rename() {
  rename_selected();
}

void PakTab::undo() {
  if (undo_stack_) {
    undo_stack_->undo();
  }
}

void PakTab::redo() {
  if (undo_stack_) {
    undo_stack_->redo();
  }
}

void PakTab::set_dirty(bool dirty) {
  if (dirty_ == dirty) {
    return;
  }
  dirty_ = dirty;
  emit dirty_changed(dirty_);
}

bool PakTab::save(QString* error) {
  if (!loaded_) {
    if (error) {
      *error = "Archive is not loaded.";
    }
    return false;
  }
  if (!dirty_) {
    return true;
  }
  if (pak_path_.isEmpty()) {
    if (error) {
      *error = "This archive has not been saved yet. Use Save As...";
    }
    return false;
  }
  const SaveOptions options = default_save_options_for_current_path();
  return save_as(pak_path_, options, error);
}

PakTab::SaveOptions PakTab::default_save_options_for_current_path() const {
  SaveOptions opts;
  if (archive_.is_loaded() && archive_.format() == Archive::Format::Zip) {
    opts.format = Archive::Format::Zip;
    if (archive_.is_quakelive_encrypted_pk3()) {
      opts.quakelive_encrypt_pk3 = true;
    }
  } else if (archive_.is_loaded() && archive_.format() == Archive::Format::Pak) {
    opts.format = Archive::Format::Pak;
  }
  return opts;
}

bool PakTab::write_archive_file(const QString& dest_path, const SaveOptions& options, QString* error) {
  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }

  const QString lower = abs.toLower();
  const int dot = lower.lastIndexOf('.');
  const QString ext = dot >= 0 ? lower.mid(dot + 1) : QString();

  Archive::Format fmt = options.format;
  if (fmt == Archive::Format::Unknown) {
    if (ext == "pak") {
      fmt = Archive::Format::Pak;
    } else if (ext == "zip" || ext == "pk3" || ext == "pk4" || ext == "pkz") {
      fmt = Archive::Format::Zip;
    } else if (archive_.is_loaded()) {
      fmt = archive_.format();
    } else {
      fmt = Archive::Format::Pak;
    }
  }

  if (fmt == Archive::Format::Pak) {
    if (options.quakelive_encrypt_pk3) {
      if (error) {
        *error = "Quake Live PK3 encryption is only supported for ZIP-based archives.";
      }
      return false;
    }
    return write_pak_file(abs, error);
  }
  if (fmt == Archive::Format::Zip) {
    return write_zip_file(abs, options.quakelive_encrypt_pk3, error);
  }

  if (error) {
    *error = "Unknown archive format.";
  }
  return false;
}

bool PakTab::save_as(const QString& dest_path, const SaveOptions& options, QString* error) {
  if (!loaded_) {
    if (error) {
      *error = "Archive is not loaded.";
    }
    return false;
  }

  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }

  if (!write_archive_file(abs, options, error)) {
    return false;
  }

  QString reload_err;
  if (!archive_.load(abs, &reload_err)) {
    if (error) {
      *error = reload_err.isEmpty() ? "Saved, but failed to reload the new archive." : reload_err;
    }
    return false;
  }

  mode_ = Mode::ExistingPak;
  pak_path_ = abs;
  added_files_.clear();
  added_index_by_name_.clear();
  virtual_dirs_.clear();
  deleted_files_.clear();
  deleted_dir_prefixes_.clear();
  set_dirty(false);
  if (undo_stack_) {
    undo_stack_->clear();
    undo_stack_->setClean();
  }
  load_error_.clear();
  loaded_ = true;
  set_current_dir(current_dir_);
  return true;
}

/*
=============
PakTab::build_ui

Construct the Pak tab user interface and wire up signals.
=============
*/
void PakTab::build_ui() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 18, 22, 18);
  layout->setSpacing(12);

  breadcrumbs_ = new BreadcrumbBar(this);
  breadcrumbs_->set_crumbs({"Root"});
  connect(breadcrumbs_, &BreadcrumbBar::crumb_activated, this, &PakTab::activate_crumb);
  layout->addWidget(breadcrumbs_);

  toolbar_ = new QToolBar(this);
  toolbar_->setIconSize(QSize(18, 18));
  toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar_->setMovable(false);
  toolbar_->setFloatable(false);
  layout->addWidget(toolbar_);
  setup_actions();

  undo_stack_ = new QUndoStack(this);
  connect(undo_stack_, &QUndoStack::cleanChanged, this, [this](bool clean) {
    set_dirty(!clean);
  });

  splitter_ = new QSplitter(Qt::Horizontal, this);
  splitter_->setChildrenCollapsible(false);
  layout->addWidget(splitter_, 1);

  view_stack_ = new QStackedWidget(splitter_);
  splitter_->addWidget(view_stack_);

  preview_ = new PreviewPane(splitter_);
  preview_->setMinimumWidth(320);
  splitter_->addWidget(preview_);
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 2);
	connect(preview_, &PreviewPane::request_previous_audio, this, [this]() { select_adjacent_audio(-1); });
	connect(preview_, &PreviewPane::request_next_audio, this, [this]() { select_adjacent_audio(1); });
	connect(preview_, &PreviewPane::request_previous_video, this, [this]() { select_adjacent_video(-1); });
	connect(preview_, &PreviewPane::request_next_video, this, [this]() { select_adjacent_video(1); });

  details_view_ = new PakTabDetailsView(this, view_stack_);
  details_view_->setHeaderLabels({"Name", "Size", "Modified"});
  details_view_->setRootIsDecorated(false);
  details_view_->setUniformRowHeights(true);
  details_view_->setAlternatingRowColors(true);
  details_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  details_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  details_view_->setExpandsOnDoubleClick(false);
  details_view_->header()->setStretchLastSection(false);
  details_view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  details_view_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  details_view_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  details_view_->header()->setSortIndicatorShown(true);
  details_view_->setSortingEnabled(true);
  details_view_->sortByColumn(0, Qt::AscendingOrder);
  details_view_->setContextMenuPolicy(Qt::CustomContextMenu);
  view_stack_->addWidget(details_view_);

  icon_view_ = new PakTabIconView(this, view_stack_);
  icon_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  icon_view_->setContextMenuPolicy(Qt::CustomContextMenu);
  icon_view_->setSortingEnabled(true);
  view_stack_->addWidget(icon_view_);

  connect(details_view_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    show_context_menu(details_view_, pos);
  });
  connect(icon_view_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    show_context_menu(icon_view_, pos);
  });
  connect(details_view_, &QTreeWidget::itemSelectionChanged, this, &PakTab::update_preview);
  connect(icon_view_, &QListWidget::itemSelectionChanged, this, &PakTab::update_preview);

  connect(details_view_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
    if (!item) {
      return;
    }
    const bool is_dir = item->data(0, Qt::UserRole).toBool();
    if (is_dir) {
      enter_directory(item->text(0));
      return;
    }

    const QString pak_path = item->data(0, kRolePakPath).toString();
    const QString leaf = pak_leaf_name(pak_path.isEmpty() ? item->text(0) : pak_path);
    if (file_ext_lower(leaf) == "wad") {
      QString err;
      if (!mount_wad_from_selected_file(pak_path, &err) && !err.isEmpty()) {
        if (preview_) {
          preview_->show_message(leaf.isEmpty() ? "WAD" : leaf, err);
        }
      }
      return;
    }

    update_preview();
    if (preview_) {
      preview_->start_playback_from_beginning();
    }
  });

  connect(icon_view_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
    if (!item) {
      return;
    }
    const bool is_dir = item->data(Qt::UserRole).toBool();
    if (is_dir) {
      enter_directory(item->text());
      return;
    }

    const QString pak_path = item->data(kRolePakPath).toString();
    const QString leaf = pak_leaf_name(pak_path.isEmpty() ? item->text() : pak_path);
    if (file_ext_lower(leaf) == "wad") {
      QString err;
      if (!mount_wad_from_selected_file(pak_path, &err) && !err.isEmpty()) {
        if (preview_) {
          preview_->show_message(leaf.isEmpty() ? "WAD" : leaf, err);
        }
      }
      return;
    }

    update_preview();
    if (preview_) {
      preview_->start_playback_from_beginning();
    }
  });

  // Delete shortcuts: Del prompts, Shift+Del skips confirmation.
  auto* del = new QShortcut(QKeySequence::Delete, this);
  del->setContext(Qt::WidgetWithChildrenShortcut);
  connect(del, &QShortcut::activated, this, [this]() { delete_selected(false); });

  auto* del_force = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete), this);
  del_force->setContext(Qt::WidgetWithChildrenShortcut);
  connect(del_force, &QShortcut::activated, this, [this]() { delete_selected(true); });

  auto* cut_sc = new QShortcut(QKeySequence::Cut, this);
  cut_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(cut_sc, &QShortcut::activated, this, [this]() { copy_selected(true); });

  auto* copy_sc = new QShortcut(QKeySequence::Copy, this);
  copy_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(copy_sc, &QShortcut::activated, this, [this]() { copy_selected(false); });

  auto* paste_sc = new QShortcut(QKeySequence::Paste, this);
  paste_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(paste_sc, &QShortcut::activated, this, [this]() { paste_from_clipboard(); });

  auto* rename_sc = new QShortcut(QKeySequence(Qt::Key_F2), this);
  rename_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(rename_sc, &QShortcut::activated, this, [this]() { rename_selected(); });

  auto* undo_sc = new QShortcut(QKeySequence::Undo, this);
  undo_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(undo_sc, &QShortcut::activated, this, [this]() { undo(); });

  auto* redo_sc = new QShortcut(QKeySequence::Redo, this);
  redo_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(redo_sc, &QShortcut::activated, this, [this]() { redo(); });

  auto* redo_sc2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
  redo_sc2->setContext(Qt::WidgetWithChildrenShortcut);
  connect(redo_sc2, &QShortcut::activated, this, [this]() { redo(); });

  update_view_controls();
}

void PakTab::setup_actions() {
  if (!toolbar_) {
    return;
  }

  add_files_action_ = toolbar_->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Add Files...");
  add_files_action_->setToolTip("Add files to the current folder");
  connect(add_files_action_, &QAction::triggered, this, &PakTab::add_files);

  add_folder_action_ = toolbar_->addAction(style()->standardIcon(QStyle::SP_DirIcon), "Add Folder...");
  add_folder_action_->setToolTip("Add a folder (recursively) to the current folder");
  connect(add_folder_action_, &QAction::triggered, this, &PakTab::add_folder);

  new_folder_action_ =
    toolbar_->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), "New Folder...");
  new_folder_action_->setToolTip("Create a new folder in the current folder");
  connect(new_folder_action_, &QAction::triggered, this, &PakTab::new_folder);

  delete_action_ = toolbar_->addAction(style()->standardIcon(QStyle::SP_TrashIcon), "Delete");
  delete_action_->setToolTip("Delete selected item (Del). Shift+Del skips confirmation.");
  connect(delete_action_, &QAction::triggered, this, [this]() {
    const bool force = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    delete_selected(force);
  });

  toolbar_->addSeparator();

  view_button_ = new QToolButton(toolbar_);
  view_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  view_button_->setToolTip("Change view mode");
  view_button_->setPopupMode(QToolButton::InstantPopup);

  auto* view_menu = new QMenu(view_button_);
  view_group_ = new QActionGroup(view_menu);
  view_group_->setExclusive(true);

  view_auto_action_ = view_menu->addAction("Auto");
  view_auto_action_->setCheckable(true);
  view_group_->addAction(view_auto_action_);
  connect(view_auto_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::Auto); });

  view_menu->addSeparator();

  view_details_action_ = view_menu->addAction("Details");
  view_details_action_->setCheckable(true);
  view_details_action_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  view_group_->addAction(view_details_action_);
  connect(view_details_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::Details); });

  view_list_action_ = view_menu->addAction("List");
  view_list_action_->setCheckable(true);
  view_list_action_->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
  view_group_->addAction(view_list_action_);
  connect(view_list_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::List); });

  view_small_icons_action_ = view_menu->addAction("Small Icons");
  view_small_icons_action_->setCheckable(true);
  view_group_->addAction(view_small_icons_action_);
  connect(view_small_icons_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::SmallIcons); });

  view_large_icons_action_ = view_menu->addAction("Large Icons");
  view_large_icons_action_->setCheckable(true);
  view_group_->addAction(view_large_icons_action_);
  connect(view_large_icons_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::LargeIcons); });

  view_gallery_action_ = view_menu->addAction("Gallery");
  view_gallery_action_->setCheckable(true);
  view_group_->addAction(view_gallery_action_);
  connect(view_gallery_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::Gallery); });

  view_button_->setMenu(view_menu);
  toolbar_->addWidget(view_button_);
}

void PakTab::show_context_menu(QWidget* view, const QPoint& pos) {
  if (!view || !loaded_) {
    return;
  }

  QMenu menu(this);

  auto* cut_action = menu.addAction("Cut");
  cut_action->setShortcut(QKeySequence::Cut);
  connect(cut_action, &QAction::triggered, this, [this]() { copy_selected(true); });

  auto* copy_action = menu.addAction("Copy");
  copy_action->setShortcut(QKeySequence::Copy);
  connect(copy_action, &QAction::triggered, this, [this]() { copy_selected(false); });

  auto* paste_action = menu.addAction("Paste");
  paste_action->setShortcut(QKeySequence::Paste);
  connect(paste_action, &QAction::triggered, this, [this]() { paste_from_clipboard(); });

  auto* rename_action = menu.addAction("Rename");
  rename_action->setShortcut(QKeySequence(Qt::Key_F2));
  connect(rename_action, &QAction::triggered, this, [this]() { rename_selected(); });

  menu.addSeparator();
  if (add_files_action_) {
    menu.addAction(add_files_action_);
  }
  if (add_folder_action_) {
    menu.addAction(add_folder_action_);
  }
  if (new_folder_action_) {
    menu.addAction(new_folder_action_);
  }
  if (delete_action_) {
    menu.addSeparator();
    menu.addAction(delete_action_);
  }

  QPoint global = view->mapToGlobal(pos);
  if (auto* area = qobject_cast<QAbstractScrollArea*>(view)) {
    global = area->viewport()->mapToGlobal(pos);
  }
  menu.exec(global);
}

QString PakTab::current_prefix() const {
  return join_prefix(current_dir_);
}

void PakTab::set_view_mode(ViewMode mode) {
  if (view_mode_ == mode) {
    return;
  }
  view_mode_ = mode;
  if (view_mode_ != ViewMode::Auto) {
    effective_view_ = view_mode_;
  }
  refresh_listing();
}

void PakTab::apply_auto_view(int file_count, int image_count, int video_count, int model_count) {
  // Auto: prefer Gallery when there's a meaningful amount of visual assets.
  const int visual_count = image_count + video_count + model_count;
  const bool show_gallery = (file_count > 0) && (visual_count * 100 >= file_count * 10);
  effective_view_ = show_gallery ? ViewMode::Gallery : ViewMode::Details;
}

void PakTab::update_view_controls() {
  if (view_auto_action_) {
    view_auto_action_->setChecked(view_mode_ == ViewMode::Auto);
  }
  if (view_details_action_) {
    view_details_action_->setChecked(view_mode_ == ViewMode::Details);
  }
  if (view_list_action_) {
    view_list_action_->setChecked(view_mode_ == ViewMode::List);
  }
  if (view_small_icons_action_) {
    view_small_icons_action_->setChecked(view_mode_ == ViewMode::SmallIcons);
  }
  if (view_large_icons_action_) {
    view_large_icons_action_->setChecked(view_mode_ == ViewMode::LargeIcons);
  }
  if (view_gallery_action_) {
    view_gallery_action_->setChecked(view_mode_ == ViewMode::Gallery);
  }

  if (!view_stack_) {
    return;
  }

  const bool use_details = (effective_view_ == ViewMode::Details);
  view_stack_->setCurrentWidget(use_details ? static_cast<QWidget*>(details_view_)
                                            : static_cast<QWidget*>(icon_view_));
  if (!use_details) {
    configure_icon_view();
  }

  if (view_button_) {
    QIcon icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    switch (effective_view_) {
      case ViewMode::Details:
        icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
        break;
      case ViewMode::List:
        icon = style()->standardIcon(QStyle::SP_FileDialogListView);
        break;
      case ViewMode::SmallIcons:
      case ViewMode::LargeIcons:
      case ViewMode::Gallery:
        icon = style()->standardIcon(QStyle::SP_FileDialogContentsView);
        break;
      case ViewMode::Auto:
        break;
    }
    view_button_->setIcon(icon);
  }
}

void PakTab::configure_icon_view() {
  if (!icon_view_) {
    return;
  }

  QListView::ViewMode mode = QListView::IconMode;
  QSize icon = QSize(64, 64);
  QSize grid = QSize(160, 128);
  QListView::Flow flow = QListView::LeftToRight;
  bool word_wrap = true;
  bool wrapping = true;
  int spacing = 10;

  switch (effective_view_) {
    case ViewMode::List:
      mode = QListView::ListMode;
      icon = QSize(18, 18);
      grid = QSize();
      flow = QListView::TopToBottom;
      word_wrap = false;
      wrapping = false;
      break;
    case ViewMode::SmallIcons:
      mode = QListView::IconMode;
      icon = QSize(32, 32);
      grid = QSize(120, 96);
      break;
    case ViewMode::LargeIcons:
      mode = QListView::IconMode;
      icon = QSize(64, 64);
      grid = QSize(160, 128);
      break;
    case ViewMode::Gallery:
      mode = QListView::IconMode;
      icon = QSize(128, 128);
      spacing = 2;
      {
        const QFontMetrics fm(icon_view_->font());
        const int text_lines = 2;
        const int text_h = fm.lineSpacing() * text_lines;
        grid = QSize(icon.width() + 20, icon.height() + text_h + 18);
      }
      break;
    case ViewMode::Details:
    case ViewMode::Auto:
      break;
  }

  icon_view_->setViewMode(mode);
  icon_view_->setIconSize(icon);
  icon_view_->setWordWrap(word_wrap);
  icon_view_->setWrapping(wrapping);
  icon_view_->setResizeMode(QListView::Adjust);
  icon_view_->setMovement(QListView::Static);
  icon_view_->setFlow(flow);
  icon_view_->setSpacing(spacing);
  icon_view_->setGridSize(grid);
}

void PakTab::stop_thumbnail_generation() {
  ++thumbnail_generation_;
  icon_items_by_path_.clear();
  thumbnail_pool_.clear();
}

void PakTab::queue_thumbnail(const QString& pak_path,
                             const QString& leaf,
                             const QString& source_path,
                             qint64 size,
                             const QSize& icon_size) {
  if (!icon_view_) {
    return;
  }
  if (pak_path.isEmpty() || leaf.isEmpty() || !icon_size.isValid()) {
    return;
  }

  const QString ext = file_ext_lower(leaf);
  const bool is_image = is_image_file_name(leaf);
  const bool is_cinematic = (ext == "cin" || ext == "roq");
  const bool is_model = is_model_file_name(leaf);
  if (!is_image && !is_cinematic && !is_model) {
    return;
  }

  if ((ext == "lmp" || ext == "mip") && !quake1_palette_loaded_) {
    ensure_quake1_palette(nullptr);
  }

  // Capture state for this thumbnail generation.
  const quint64 gen = thumbnail_generation_;
  PakTab* self = this;
  const QVector<QRgb> quake1_palette = quake1_palette_;
  const QVector<QRgb> quake2_palette = quake2_palette_;

  auto* task = QRunnable::create([self, gen, pak_path, leaf, source_path, size, icon_size, quake1_palette, quake2_palette]() {
    QImage image;

    const QString ext = file_ext_lower(leaf);
    if (is_image_file_name(leaf)) {
       ImageDecodeOptions options;
       if (ext == "lmp" && quake1_palette.size() == 256) {
         options.palette = &quake1_palette;
       }
       if (ext == "mip" && quake1_palette.size() == 256) {
         options.palette = &quake1_palette;
       }
       if (ext == "wal" && quake2_palette.size() == 256) {
         options.palette = &quake2_palette;
       }

      ImageDecodeResult decoded;
      if (!source_path.isEmpty()) {
        decoded = decode_image_file(source_path, options);
      } else {
        constexpr qint64 kMaxThumbBytes = 32LL * 1024 * 1024;
        QByteArray bytes;
        QString err;
        if (self->view_archive().read_entry_bytes(pak_path, &bytes, &err, kMaxThumbBytes)) {
          decoded = decode_image_bytes(bytes, leaf, options);
        }
      }

      if (decoded.ok()) {
        image = decoded.image;
      }
    } else if (ext == "cin" || ext == "roq") {
      std::unique_ptr<CinematicDecoder> dec;
      QString err;

      if (!source_path.isEmpty()) {
        dec = open_cinematic_file(source_path, &err);
      } else {
        // Avoid trying to thumbnail extremely large cinematics.
        constexpr qint64 kMaxCinematicBytes = 256LL * 1024 * 1024;
        const qint64 max_bytes = (size > 0) ? std::min(size, kMaxCinematicBytes) : kMaxCinematicBytes;

        QByteArray bytes;
        if (self->view_archive().read_entry_bytes(pak_path, &bytes, &err, max_bytes)) {
          QTemporaryFile tmp(QDir(QDir::tempPath()).filePath(QString("pakfu-thumb-XXXXXX.%1").arg(ext)));
          tmp.setAutoRemove(true);
          if (tmp.open()) {
            tmp.write(bytes);
            tmp.flush();
            tmp.close();
            dec = open_cinematic_file(tmp.fileName(), &err);
          }
        }
      }

      if (dec) {
        CinematicFrame frame;
        if (dec->decode_frame(0, &frame, &err) && !frame.image.isNull()) {
          image = frame.image;
        }
      }
    } else if (is_model_file_name(leaf)) {
      QString model_path = source_path;
      QString err;
      QTemporaryFile tmp;
      if (model_path.isEmpty()) {
        constexpr qint64 kMaxModelBytes = 128LL * 1024 * 1024;
        const qint64 max_bytes = (size > 0) ? std::min(size, kMaxModelBytes) : kMaxModelBytes;
        QByteArray bytes;
        if (self->view_archive().read_entry_bytes(pak_path, &bytes, &err, max_bytes)) {
          tmp.setAutoRemove(true);
          tmp.setFileTemplate(QDir(QDir::tempPath()).filePath(QString("pakfu-thumb-XXXXXX.%1").arg(ext)));
          if (tmp.open()) {
            tmp.write(bytes);
            tmp.flush();
            tmp.close();
            model_path = tmp.fileName();
          }
        }
      }

      if (!model_path.isEmpty()) {
        QString load_err;
        const std::optional<LoadedModel> model = load_model_file(model_path, &load_err);
        if (model) {
          image = render_model_thumbnail(*model, icon_size);
        }
      }
    }

    if (image.isNull()) {
      return;
    }

    const QImage scaled = image.scaled(icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage square(icon_size, QImage::Format_ARGB32_Premultiplied);
    square.fill(Qt::transparent);
    {
      QPainter p(&square);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      const int ox = (icon_size.width() - scaled.width()) / 2;
      const int oy = (icon_size.height() - scaled.height()) / 2;
      p.drawImage(QPoint(ox, oy), scaled);
    }
    image = std::move(square);
    QMetaObject::invokeMethod(self, [self, gen, pak_path, image = std::move(image)]() mutable {
      if (!self || self->thumbnail_generation_ != gen) {
        return;
      }
      QListWidgetItem* item = self->icon_items_by_path_.value(pak_path, nullptr);
      if (!item) {
        return;
      }
      QPixmap pm = QPixmap::fromImage(image);
      item->setIcon(QIcon(pm));
    }, Qt::QueuedConnection);
  });
  task->setAutoDelete(true);
  thumbnail_pool_.start(task);
}

QString PakTab::selected_pak_path(bool* is_dir) const {
  if (is_dir) {
    *is_dir = false;
  }
  if (!loaded_) {
    return {};
  }

  auto try_details = [&]() -> QString {
    if (!details_view_) {
      return {};
    }
    const QList<QTreeWidgetItem*> items = details_view_->selectedItems();
    if (items.isEmpty() || !items.first()) {
      return {};
    }
    const auto* item = items.first();
    const bool dir = item->data(0, kRoleIsDir).toBool();
    if (is_dir) {
      *is_dir = dir;
    }
    const QString stored = item->data(0, kRolePakPath).toString();
    if (!stored.isEmpty()) {
      return stored;
    }
    QString name = item->text(0);
    if (dir && name.endsWith('/')) {
      name.chop(1);
    }
    return normalize_pak_path(current_prefix() + name + (dir ? "/" : ""));
  };

  auto try_icons = [&]() -> QString {
    if (!icon_view_) {
      return {};
    }
    const QList<QListWidgetItem*> items = icon_view_->selectedItems();
    if (items.isEmpty() || !items.first()) {
      return {};
    }
    auto* item = items.first();
    const bool dir = item->data(kRoleIsDir).toBool();
    if (is_dir) {
      *is_dir = dir;
    }
    const QString stored = item->data(kRolePakPath).toString();
    if (!stored.isEmpty()) {
      return stored;
    }
    QString name = item->text();
    if (dir && name.endsWith('/')) {
      name.chop(1);
    }
    return normalize_pak_path(current_prefix() + name + (dir ? "/" : ""));
  };

  if (view_stack_ && view_stack_->currentWidget() == icon_view_) {
    const QString r = try_icons();
    return r.isEmpty() ? try_details() : r;
  }
  if (view_stack_ && view_stack_->currentWidget() == details_view_) {
    const QString r = try_details();
    return r.isEmpty() ? try_icons() : r;
  }

  const QString r = try_details();
  return r.isEmpty() ? try_icons() : r;
}

QVector<QPair<QString, bool>> PakTab::selected_items() const {
  QVector<QPair<QString, bool>> out;
  if (!loaded_) {
    return out;
  }

  auto add_item = [&](const QString& path, bool is_dir) {
    QString p = normalize_pak_path(path);
    if (p.isEmpty()) {
      return;
    }
    if (is_dir && !p.endsWith('/')) {
      p += '/';
    }
    out.push_back(qMakePair(p, is_dir));
  };

  if (view_stack_ && view_stack_->currentWidget() == icon_view_ && icon_view_) {
    const QList<QListWidgetItem*> items = icon_view_->selectedItems();
    for (const QListWidgetItem* item : items) {
      if (!item) {
        continue;
      }
      add_item(item->data(kRolePakPath).toString(), item->data(kRoleIsDir).toBool());
    }
    return out;
  }

  if (details_view_) {
    const QList<QTreeWidgetItem*> items = details_view_->selectedItems();
    for (const QTreeWidgetItem* item : items) {
      if (!item) {
        continue;
      }
      add_item(item->data(0, kRolePakPath).toString(), item->data(0, kRoleIsDir).toBool());
    }
  }

  if (out.isEmpty() && icon_view_) {
    const QList<QListWidgetItem*> items = icon_view_->selectedItems();
    for (const QListWidgetItem* item : items) {
      if (!item) {
        continue;
      }
      add_item(item->data(kRolePakPath).toString(), item->data(kRoleIsDir).toBool());
    }
  }

  return out;
}

void PakTab::rebuild_added_index() {
  added_index_by_name_.clear();
  added_index_by_name_.reserve(added_files_.size());
  for (int i = 0; i < added_files_.size(); ++i) {
    added_index_by_name_.insert(added_files_[i].pak_name, i);
  }
}

void PakTab::remove_added_file_by_name(const QString& pak_name_in) {
  const QString pak_name = normalize_pak_path(pak_name_in);
  const int idx = added_index_by_name_.value(pak_name, -1);
  if (idx < 0 || idx >= added_files_.size()) {
    return;
  }
  added_files_.removeAt(idx);
  rebuild_added_index();
}

bool PakTab::is_deleted_path(const QString& pak_name_in) const {
  const QString pak_name = normalize_pak_path(pak_name_in);
  if (deleted_files_.contains(pak_name)) {
    return true;
  }
  for (const QString& d : deleted_dir_prefixes_) {
    if (!d.isEmpty() && pak_name.startsWith(d)) {
      return true;
    }
  }
  return false;
}

void PakTab::clear_deletions_under(const QString& pak_name_in) {
  const QString pak_name = normalize_pak_path(pak_name_in);
  deleted_files_.remove(pak_name);

  // Remove any directory deletion markers that would hide this path.
  QSet<QString> keep;
  keep.reserve(deleted_dir_prefixes_.size());
  for (const QString& d : deleted_dir_prefixes_) {
    if (d.isEmpty()) {
      continue;
    }
    if (!pak_name.startsWith(d)) {
      keep.insert(d);
    }
  }
  deleted_dir_prefixes_.swap(keep);
}

namespace {
bool copy_file_stream(const QString& src_path, const QString& dest_path, QString* error) {
  QFile src(src_path);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QString("Unable to open file: %1").arg(src_path);
    }
    return false;
  }

  const QFileInfo out_info(dest_path);
  if (!out_info.dir().exists()) {
    QDir d(out_info.dir().absolutePath());
    if (!d.mkpath(".")) {
      if (error) {
        *error = QString("Unable to create output directory: %1").arg(out_info.dir().absolutePath());
      }
      return false;
    }
  }

  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to create output file: %1").arg(dest_path);
    }
    return false;
  }

  constexpr qint64 kChunk = 1 << 16;
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunk));
  while (true) {
    const qint64 got = src.read(buffer.data(), buffer.size());
    if (got < 0) {
      if (error) {
        *error = QString("Unable to read file: %1").arg(src_path);
      }
      return false;
    }
    if (got == 0) {
      break;
    }
    if (out.write(buffer.constData(), got) != got) {
      if (error) {
        *error = QString("Unable to write output file: %1").arg(dest_path);
      }
      return false;
    }
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize output file: %1").arg(dest_path);
    }
    return false;
  }

  return true;
}
}  // namespace

QString PakTab::ensure_export_root() {
  if (export_temp_dir_) {
    return export_temp_dir_->path();
  }

  export_temp_dir_.reset(new QTemporaryDir(QDir::tempPath() + "/PakFu-XXXXXX"));
  if (!export_temp_dir_ || !export_temp_dir_->isValid()) {
    export_temp_dir_.reset();
    return {};
  }

  return export_temp_dir_->path();
}

bool PakTab::export_dir_prefix_to_fs(const QString& dir_prefix_in, const QString& dest_dir, QString* error) {
  const QString prefix = normalize_pak_path(dir_prefix_in);
  if (prefix.isEmpty() || !prefix.endsWith('/')) {
    if (error) {
      *error = "Invalid directory prefix.";
    }
    return false;
  }

  QDir dest(dest_dir);
  if (!dest.exists() && !dest.mkpath(".")) {
    if (error) {
      *error = QString("Unable to create export directory: %1").arg(dest_dir);
    }
    return false;
  }

  // Create any empty virtual directories (best-effort).
  for (const QString& vdir_in : virtual_dirs_) {
    const QString vdir = normalize_pak_path(vdir_in);
    if (!vdir.startsWith(prefix) || is_deleted_path(vdir)) {
      continue;
    }
    const QString rel = vdir.mid(prefix.size());
    if (rel.isEmpty()) {
      continue;
    }
    dest.mkpath(rel);
  }

  QSet<QString> written;

  // Base archive entries (skip overridden).
  if (archive_.is_loaded()) {
    for (const ArchiveEntry& e : archive_.entries()) {
      const QString name = normalize_pak_path(e.name);
      if (!name.startsWith(prefix) || is_deleted_path(name)) {
        continue;
      }
      if (added_index_by_name_.contains(name)) {
        continue;  // overridden by added file
      }
      const QString rel = name.mid(prefix.size());
      if (rel.isEmpty()) {
        continue;
      }
      const QString out_path = dest.filePath(rel);
      QString err;
      if (!archive_.extract_entry_to_file(name, out_path, &err)) {
        if (error) {
          *error = err.isEmpty() ? QString("Unable to export entry: %1").arg(name) : err;
        }
        return false;
      }
      written.insert(name);
    }
  }

  // Added/overridden files.
  for (const AddedFile& f : added_files_) {
    const QString name = normalize_pak_path(f.pak_name);
    if (!name.startsWith(prefix) || is_deleted_path(name)) {
      continue;
    }
    const QString rel = name.mid(prefix.size());
    if (rel.isEmpty()) {
      continue;
    }
    const QString out_path = dest.filePath(rel);
    QString err;
    if (!copy_file_stream(f.source_path, out_path, &err)) {
      if (error) {
        *error = err.isEmpty() ? QString("Unable to export file: %1").arg(name) : err;
      }
      return false;
    }
    written.insert(name);
  }

  return true;
}

bool PakTab::export_path_to_temp(const QString& pak_path_in, bool is_dir, QString* out_fs_path, QString* error) {
  if (out_fs_path) {
    out_fs_path->clear();
  }

  const QString root = ensure_export_root();
  if (root.isEmpty()) {
    if (error) {
      *error = "Unable to create temporary export directory.";
    }
    return false;
  }

  const QString op_dir = QDir(root).filePath(QString("export-%1").arg(export_seq_++));
  if (!QDir().mkpath(op_dir)) {
    if (error) {
      *error = "Unable to create temporary export directory.";
    }
    return false;
  }

  const QString pak_path = normalize_pak_path(pak_path_in);
  const QString leaf = pak_leaf_name(pak_path);

  if (is_dir) {
    if (wad_mounted_) {
      if (error) {
        *error = "Folders are not available inside a mounted WAD.";
      }
      return false;
    }
    const QString dest_dir = QDir(op_dir).filePath(leaf.isEmpty() ? "folder" : leaf);
    if (!QDir().mkpath(dest_dir)) {
      if (error) {
        *error = "Unable to create temporary export directory.";
      }
      return false;
    }
    QString dir_prefix = pak_path;
    if (!dir_prefix.endsWith('/')) {
      dir_prefix += '/';
    }
    if (!export_dir_prefix_to_fs(dir_prefix, dest_dir, error)) {
      return false;
    }
    if (out_fs_path) {
      *out_fs_path = dest_dir;
    }
    return true;
  }

  const QString dest_file = QDir(op_dir).filePath(leaf.isEmpty() ? "file.bin" : leaf);

  // Prefer an overridden/added source file when present.
  const int added_idx = added_index_by_name_.value(pak_path, -1);
  if (!wad_mounted_ && added_idx >= 0 && added_idx < added_files_.size()) {
    QString err;
    if (!copy_file_stream(added_files_[added_idx].source_path, dest_file, &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to export file." : err;
      }
      return false;
    }
    if (out_fs_path) {
      *out_fs_path = dest_file;
    }
    return true;
  }

  if (!view_archive().is_loaded()) {
    if (error) {
      *error = "Unable to export from an unloaded PAK.";
    }
    return false;
  }

  QString err;
  if (!view_archive().extract_entry_to_file(pak_path, dest_file, &err)) {
    if (error) {
      *error = err.isEmpty() ? "Unable to export file." : err;
    }
    return false;
  }

  if (out_fs_path) {
    *out_fs_path = dest_file;
  }
  return true;
}

void PakTab::delete_selected(bool skip_confirmation) {
  if (!loaded_) {
    return;
  }
  if (wad_mounted_) {
    QMessageBox::information(this, "Mounted WAD", "This WAD view is read-only. Click the Root breadcrumb to go back.");
    return;
  }

  const QVector<QPair<QString, bool>> raw = selected_items();
  if (raw.isEmpty()) {
    return;
  }

  // Capture for undo.
  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QSet<QString> dir_prefixes;
  QSet<QString> files;
  for (const auto& it : raw) {
    if (it.second) {
      QString d = normalize_pak_path(it.first);
      if (!d.endsWith('/')) {
        d += '/';
      }
      dir_prefixes.insert(d);
    } else {
      files.insert(normalize_pak_path(it.first));
    }
  }

  // Reduce nested directory selections.
  QStringList dirs = dir_prefixes.values();
  std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.size() < b.size(); });
  QSet<QString> reduced_dirs;
  for (const QString& d : dirs) {
    bool covered = false;
    for (const QString& keep : reduced_dirs) {
      if (!keep.isEmpty() && d.startsWith(keep)) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      reduced_dirs.insert(d);
    }
  }

  // Remove file selections that are already covered by a selected directory.
  QSet<QString> reduced_files;
  for (const QString& f : files) {
    bool covered = false;
    for (const QString& d : reduced_dirs) {
      if (!d.isEmpty() && f.startsWith(d)) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      reduced_files.insert(f);
    }
  }

  // Best-effort count of affected files.
  int affected_files = 0;
  if (!reduced_dirs.isEmpty() || !reduced_files.isEmpty()) {
    for (const ArchiveEntry& e : archive_.entries()) {
      const QString name = normalize_pak_path(e.name);
      if (is_deleted_path(name)) {
        continue;
      }
      if (reduced_files.contains(name)) {
        ++affected_files;
        continue;
      }
      for (const QString& d : reduced_dirs) {
        if (!d.isEmpty() && name.startsWith(d)) {
          ++affected_files;
          break;
        }
      }
    }
    for (const AddedFile& f : added_files_) {
      const QString name = normalize_pak_path(f.pak_name);
      if (is_deleted_path(name)) {
        continue;
      }
      if (reduced_files.contains(name)) {
        ++affected_files;
        continue;
      }
      for (const QString& d : reduced_dirs) {
        if (!d.isEmpty() && name.startsWith(d)) {
          ++affected_files;
          break;
        }
      }
    }
  }

  const bool force = skip_confirmation || (QApplication::keyboardModifiers() & Qt::ShiftModifier);
  if (!force) {
    const int item_count = reduced_files.size() + reduced_dirs.size();
    QString title = "Delete";
    QString text = item_count == 1 ? "Delete selected item from this PAK?" : QString("Delete %1 selected items from this PAK?").arg(item_count);
    QString info = "This does not delete any source files on disk.";
    if (!reduced_dirs.isEmpty()) {
      info = QString("This will remove %1 file(s) from the archive.\n\n%2").arg(affected_files).arg(info);
    }

    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Cancel, this);
    box.setInformativeText(info);
    QAbstractButton* del_btn = box.addButton("Delete", QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != del_btn) {
      return;
    }
  }

  bool changed = false;

  // Apply directory deletions.
  for (const QString& d : reduced_dirs) {
    if (!deleted_dir_prefixes_.contains(d)) {
      deleted_dir_prefixes_.insert(d);
      changed = true;
    }
  }

  // Remove any added files under deleted directories.
  if (!reduced_dirs.isEmpty()) {
    bool removed_added = false;
    for (int i = added_files_.size() - 1; i >= 0; --i) {
      const QString name = normalize_pak_path(added_files_[i].pak_name);
      bool under = false;
      for (const QString& d : reduced_dirs) {
        if (!d.isEmpty() && name.startsWith(d)) {
          under = true;
          break;
        }
      }
      if (under) {
        added_files_.removeAt(i);
        removed_added = true;
      }
    }
    if (removed_added) {
      rebuild_added_index();
      changed = true;
    }

    // Remove virtual dirs under deleted directories.
    QSet<QString> kept_dirs;
    kept_dirs.reserve(virtual_dirs_.size());
    for (const QString& vd : virtual_dirs_) {
      const QString name = normalize_pak_path(vd);
      bool under = false;
      for (const QString& d : reduced_dirs) {
        if (!d.isEmpty() && name.startsWith(d)) {
          under = true;
          break;
        }
      }
      if (!under) {
        kept_dirs.insert(vd);
      } else {
        changed = true;
      }
    }
    virtual_dirs_.swap(kept_dirs);

    // Remove exact file deletions under deleted directories (directory deletion supersedes them).
    QSet<QString> kept_deleted_files;
    kept_deleted_files.reserve(deleted_files_.size());
    for (const QString& f : deleted_files_) {
      const QString name = normalize_pak_path(f);
      bool under = false;
      for (const QString& d : reduced_dirs) {
        if (!d.isEmpty() && name.startsWith(d)) {
          under = true;
          break;
        }
      }
      if (!under) {
        kept_deleted_files.insert(f);
      } else {
        changed = true;
      }
    }
    deleted_files_.swap(kept_deleted_files);
  }

  // Apply file deletions.
  for (const QString& f : reduced_files) {
    if (!deleted_files_.contains(f)) {
      deleted_files_.insert(f);
      changed = true;
    }
    remove_added_file_by_name(f);
  }

  if (!changed) {
    return;
  }

  if (undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             "Delete",
                                             before_added,
                                             before_virtual,
                                             before_deleted_files,
                                             before_deleted_dirs,
                                             added_files_,
                                             virtual_dirs_,
                                             deleted_files_,
                                             deleted_dir_prefixes_));
  } else {
    set_dirty(true);
  }

  refresh_listing();
}

bool PakTab::import_urls(const QList<QUrl>& urls,
                         const QString& dest_prefix,
                         QStringList* failures,
                         QProgressDialog* progress) {
  bool changed = false;

  if (progress) {
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(250);
    progress->setRange(0, 0);
    progress->setValue(0);
  }

  int processed = 0;
  for (const QUrl& url : urls) {
    if (progress && progress->wasCanceled()) {
      break;
    }
    if (!url.isLocalFile()) {
      continue;
    }
    const QString local = url.toLocalFile();
    const QFileInfo info(local);
    if (!info.exists()) {
      continue;
    }
    if (progress) {
      progress->setLabelText(QString("Importing %1…").arg(info.fileName().isEmpty() ? local : info.fileName()));
      if ((processed++ % 8) == 0) {
        QCoreApplication::processEvents();
      }
    }

    if (info.isDir()) {
      QStringList folder_failures;
      const bool did =
        add_folder_from_path(info.absoluteFilePath(), dest_prefix, QString(), &folder_failures, progress);
      changed = changed || did;
      if (failures) {
        failures->append(folder_failures);
      }
      continue;
    }
    if (info.isFile()) {
      const QString pak_name = dest_prefix + info.fileName();
      QString err;
      if (!add_file_mapping(pak_name, info.absoluteFilePath(), &err)) {
        if (failures) {
          failures->push_back(err.isEmpty() ? QString("Failed to add: %1").arg(local) : err);
        }
      } else {
        changed = true;
      }
    }
  }

  return changed;
}

void PakTab::import_urls_with_undo(const QList<QUrl>& urls, const QString& dest_prefix, const QString& label) {
  if (!loaded_) {
    return;
  }

  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QStringList failures;
  QProgressDialog progress(label, "Cancel", 0, 0, this);
  const bool changed = import_urls(urls, dest_prefix, &failures, &progress);

  if (progress.wasCanceled()) {
    added_files_ = before_added;
    virtual_dirs_ = before_virtual;
    deleted_files_ = before_deleted_files;
    deleted_dir_prefixes_ = before_deleted_dirs;
    rebuild_added_index();
    refresh_listing();
    return;
  }

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, label, failures.mid(0, 12).join("\n"));
  }

  if (!changed) {
    return;
  }

  if (undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             label,
                                             before_added,
                                             before_virtual,
                                             before_deleted_files,
                                             before_deleted_dirs,
                                             added_files_,
                                             virtual_dirs_,
                                             deleted_files_,
                                             deleted_dir_prefixes_));
  } else {
    set_dirty(true);
  }

  refresh_listing();
}

QMimeData* PakTab::make_mime_data_for_items(const QVector<QPair<QString, bool>>& items,
                                            bool cut,
                                            QStringList* failures,
                                            QProgressDialog* progress) {
  QList<QUrl> urls;
  QJsonArray json_items;

  if (progress) {
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(250);
    progress->setRange(0, items.size());
    progress->setValue(0);
  }

  int idx = 0;
  for (const auto& it : items) {
    if (progress) {
      progress->setValue(idx);
      progress->setLabelText(QString("%1 %2…")
                               .arg(cut ? "Preparing move for" : "Preparing copy of")
                               .arg(pak_leaf_name(it.first).isEmpty() ? it.first : pak_leaf_name(it.first)));
      if ((idx % 2) == 0) {
        QCoreApplication::processEvents();
      }
      if (progress->wasCanceled()) {
        return nullptr;
      }
    }

    const QString pak_path = it.first;
    const bool is_dir = it.second;

    QString exported;
    QString err;
    if (!export_path_to_temp(pak_path, is_dir, &exported, &err)) {
      if (failures) {
        failures->push_back(err.isEmpty() ? QString("Unable to export: %1").arg(pak_path) : err);
      }
      continue;
    }

    urls.push_back(QUrl::fromLocalFile(exported));

    QJsonObject obj;
    obj.insert("pak_path", pak_path);
    obj.insert("is_dir", is_dir);
    json_items.push_back(obj);

    ++idx;
  }

  if (urls.isEmpty()) {
    return nullptr;
  }

  QJsonObject root;
  root.insert("cut", cut);
  root.insert("items", json_items);

  auto* mime = new QMimeData();
  mime->setUrls(urls);
  mime->setData(kPakFuMimeType, QJsonDocument(root).toJson(QJsonDocument::Compact));
  return mime;
}

void PakTab::copy_selected(bool cut) {
  if (!loaded_) {
    return;
  }

  const QVector<QPair<QString, bool>> items = selected_items();
  if (items.isEmpty()) {
    return;
  }

  QProgressDialog progress(cut ? "Cut" : "Copy", "Cancel", 0, items.size(), this);
  QStringList failures;
  QMimeData* mime = make_mime_data_for_items(items, cut, &failures, &progress);
  if (progress.wasCanceled()) {
    return;
  }

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, cut ? "Cut" : "Copy", failures.mid(0, 12).join("\n"));
  }

  if (!mime) {
    return;
  }

  QApplication::clipboard()->setMimeData(mime);
}

void PakTab::paste_from_clipboard() {
  if (!loaded_) {
    return;
  }
  if (wad_mounted_) {
    QMessageBox::information(this, "Mounted WAD", "This WAD view is read-only. Click the Root breadcrumb to go back.");
    return;
  }

  const QMimeData* mime = QApplication::clipboard()->mimeData();
  if (!mime) {
    return;
  }

  QList<QUrl> urls = mime->urls();
  if (urls.isEmpty()) {
    return;
  }

  bool is_cut = false;
  QVector<QPair<QString, bool>> cut_items;
  if (mime->hasFormat(kPakFuMimeType)) {
    const QByteArray payload = mime->data(kPakFuMimeType);
    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error == QJsonParseError::NoError && doc.isObject()) {
      const QJsonObject obj = doc.object();
      is_cut = obj.value("cut").toBool(false);
      const QJsonArray items = obj.value("items").toArray();
      for (const QJsonValue& v : items) {
        if (!v.isObject()) {
          continue;
        }
        const QJsonObject it = v.toObject();
        const QString pak_path = normalize_pak_path(it.value("pak_path").toString());
        const bool dir = it.value("is_dir").toBool(false);
        if (pak_path.isEmpty()) {
          continue;
        }
        cut_items.push_back(qMakePair(pak_path, dir));
      }
    }
  }

  // Capture for undo.
  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QStringList failures;
  bool changed = false;
  const QString dest_prefix = current_prefix();

  QProgressDialog progress(is_cut ? "Moving items…" : "Copying items…", "Cancel", 0, 0, this);
  changed = import_urls(urls, dest_prefix, &failures, &progress);

  if (progress.wasCanceled()) {
    added_files_ = before_added;
    virtual_dirs_ = before_virtual;
    deleted_files_ = before_deleted_files;
    deleted_dir_prefixes_ = before_deleted_dirs;
    rebuild_added_index();
    refresh_listing();
    return;
  }

  // If this was a cut from (potentially) this tab, delete the original items after a successful paste.
  if (changed && is_cut && !cut_items.isEmpty()) {
    for (const auto& it : cut_items) {
      const QString p = normalize_pak_path(it.first);
      if (it.second) {
        deleted_dir_prefixes_.insert(p.endsWith('/') ? p : (p + "/"));
      } else {
        deleted_files_.insert(p);
        remove_added_file_by_name(p);
      }
    }
  }

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, "Paste", failures.mid(0, 12).join("\n"));
  }

  if (!changed) {
    refresh_listing();
    return;
  }

  if (undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             is_cut ? "Move Items" : "Paste",
                                             before_added,
                                             before_virtual,
                                             before_deleted_files,
                                             before_deleted_dirs,
                                             added_files_,
                                             virtual_dirs_,
                                             deleted_files_,
                                             deleted_dir_prefixes_));
  } else {
    set_dirty(true);
  }

  refresh_listing();

  // After a cut+paste, convert the clipboard to a copy payload (so repeated pastes don't keep deleting).
  if (is_cut) {
    QJsonObject root;
    root.insert("cut", false);
    root.insert("items", QJsonArray());
    auto* next = new QMimeData();
    next->setUrls(urls);
    next->setData(kPakFuMimeType, QJsonDocument(root).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(next);
  }
}

void PakTab::rename_selected() {
  if (!loaded_) {
    return;
  }
  if (wad_mounted_) {
    QMessageBox::information(this, "Mounted WAD", "This WAD view is read-only. Click the Root breadcrumb to go back.");
    return;
  }

  const QVector<QPair<QString, bool>> items = selected_items();
  if (items.size() != 1) {
    return;
  }

  const QString old_path = normalize_pak_path(items.first().first);
  const bool is_dir = items.first().second;
  const QString old_leaf = pak_leaf_name(old_path);

  bool ok = false;
  const QString prompt = is_dir ? "New folder name:" : "New file name:";
  QString name = QInputDialog::getText(this, "Rename", prompt, QLineEdit::Normal, old_leaf, &ok).trimmed();
  if (!ok || name.isEmpty() || name == "." || name == "..") {
    return;
  }
  if (name.contains('/') || name.contains('\\') || name.contains(':')) {
    QMessageBox::warning(this, "Rename", "Name contains invalid characters.");
    return;
  }

  const QString new_path = normalize_pak_path(current_prefix() + name + (is_dir ? "/" : ""));
  if (new_path == old_path || new_path.isEmpty()) {
    return;
  }

  // Capture for undo.
  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  // Export old selection to temp, then import at the new name, then delete old.
  QString exported;
  QString err;
  if (!export_path_to_temp(old_path, is_dir, &exported, &err)) {
    QMessageBox::warning(this, "Rename", err.isEmpty() ? "Unable to export selection for rename." : err);
    return;
  }

  bool changed = false;
  QStringList failures;

  if (is_dir) {
    const QString forced_folder = name;
    const bool did = add_folder_from_path(exported, current_prefix(), forced_folder, &failures);
    changed = changed || did;
    deleted_dir_prefixes_.insert(old_path.endsWith('/') ? old_path : (old_path + "/"));
    changed = true;
  } else {
    if (!add_file_mapping(new_path, exported, &err)) {
      failures.push_back(err.isEmpty() ? "Unable to create renamed file." : err);
    } else {
      deleted_files_.insert(old_path);
      remove_added_file_by_name(old_path);
      changed = true;
    }
  }

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, "Rename", failures.mid(0, 12).join("\n"));
  }

  if (!changed) {
    refresh_listing();
    return;
  }

  if (undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             "Rename",
                                             before_added,
                                             before_virtual,
                                             before_deleted_files,
                                             before_deleted_dirs,
                                             added_files_,
                                             virtual_dirs_,
                                             deleted_files_,
                                             deleted_dir_prefixes_));
  } else {
    set_dirty(true);
  }

  refresh_listing();
}

bool PakTab::add_file_mapping(const QString& pak_name_in, const QString& source_path_in, QString* error) {
  const QString pak_name = normalize_pak_path(pak_name_in);
  if (!is_safe_entry_name(pak_name)) {
    if (error) {
      *error = QString("Refusing unsafe archive path: %1").arg(pak_name);
    }
    return false;
  }

  const QByteArray name_bytes = pak_name.toLatin1();
  if (name_bytes.isEmpty() || name_bytes.size() > kPakNameBytes) {
    if (error) {
      *error = QString("Archive path is too long for PAK format: %1").arg(pak_name);
    }
    return false;
  }

  const QFileInfo info(source_path_in);
  if (!info.exists() || !info.isFile()) {
    if (error) {
      *error = QString("File not found: %1").arg(source_path_in);
    }
    return false;
  }

  const qint64 size64 = info.size();
  if (size64 < 0 || size64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = QString("File is too large for PAK format: %1").arg(info.fileName());
    }
    return false;
  }

  AddedFile f;
  f.pak_name = pak_name;
  f.source_path = info.absoluteFilePath();
  f.size = static_cast<quint32>(size64);
  f.mtime_utc_secs = info.lastModified().toUTC().toSecsSinceEpoch();

  clear_deletions_under(pak_name);

  int idx = added_index_by_name_.value(pak_name, -1);
  if (idx >= 0 && idx < added_files_.size()) {
    added_files_[idx] = f;
  } else {
    idx = added_files_.size();
    added_files_.push_back(f);
    added_index_by_name_.insert(pak_name, idx);
  }

  const QStringList parts = pak_name.split('/', Qt::SkipEmptyParts);
  QString acc;
  for (int i = 0; i + 1 < parts.size(); ++i) {
    acc = acc.isEmpty() ? parts[i] : (acc + "/" + parts[i]);
    virtual_dirs_.insert(acc + "/");
  }

  return true;
}

void PakTab::add_files() {
  if (!loaded_) {
    return;
  }
  if (wad_mounted_) {
    QMessageBox::information(this, "Mounted WAD", "This WAD view is read-only. Click the Root breadcrumb to go back.");
    return;
  }

  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QFileDialog dialog(this);
  dialog.setWindowTitle("Add Files");
  dialog.setFileMode(QFileDialog::ExistingFiles);
  dialog.setNameFilters({"All files (*.*)"});
  if (!default_directory_.isEmpty() && QFileInfo::exists(default_directory_)) {
    dialog.setDirectory(default_directory_);
  }
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
    return;
  }
  default_directory_ = QFileInfo(selected.first()).absolutePath();

  QStringList failures;
  bool changed = false;
  QProgressDialog progress("Adding files…", "Cancel", 0, selected.size(), this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(250);
  progress.setValue(0);

  int idx = 0;
  for (const QString& path : selected) {
    if (progress.wasCanceled()) {
      break;
    }
    progress.setValue(idx++);
    progress.setLabelText(QString("Adding %1…").arg(QFileInfo(path).fileName()));
    if ((idx % 4) == 0) {
      QCoreApplication::processEvents();
    }

    const QString pak_name = current_prefix() + QFileInfo(path).fileName();
    QString err;
    if (!add_file_mapping(pak_name, path, &err)) {
      failures.push_back(err.isEmpty() ? QString("Failed to add: %1").arg(path) : err);
    } else {
      changed = true;
    }
  }

  if (progress.wasCanceled()) {
    added_files_ = before_added;
    virtual_dirs_ = before_virtual;
    deleted_files_ = before_deleted_files;
    deleted_dir_prefixes_ = before_deleted_dirs;
    rebuild_added_index();
    refresh_listing();
    return;
  }
  progress.setValue(selected.size());

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, "Add Files", failures.join("\n"));
  }

  if (changed) {
    if (undo_stack_) {
      undo_stack_->push(new PakTabStateCommand(this,
                                               "Add Files",
                                               before_added,
                                               before_virtual,
                                               before_deleted_files,
                                               before_deleted_dirs,
                                               added_files_,
                                               virtual_dirs_,
                                               deleted_files_,
                                               deleted_dir_prefixes_));
    } else {
      set_dirty(true);
    }
  }
  refresh_listing();
}

bool PakTab::add_folder_from_path(const QString& folder_path_in,
                                  const QString& dest_prefix_in,
                                  const QString& forced_folder_name,
                                  QStringList* failures,
                                  QProgressDialog* progress) {
  const QFileInfo folder_info(folder_path_in);
  if (!folder_info.exists() || !folder_info.isDir()) {
    if (failures) {
      failures->push_back(QString("Folder not found: %1").arg(folder_path_in));
    }
    return false;
  }

  const QString folder_path = folder_info.absoluteFilePath();
  QString folder_name = forced_folder_name.trimmed();
  if (folder_name.isEmpty()) {
    folder_name = folder_info.fileName().isEmpty() ? "folder" : folder_info.fileName();
  }
  if (folder_name.contains('/') || folder_name.contains('\\') || folder_name.contains(':')) {
    if (failures) {
      failures->push_back("Folder name contains invalid characters.");
    }
    return false;
  }

  const QString dest_prefix = normalize_pak_path(dest_prefix_in);
  const QString pak_root = normalize_pak_path(dest_prefix + folder_name) + "/";
  virtual_dirs_.insert(pak_root);
  clear_deletions_under(pak_root);

  QDir base(folder_path);
  bool changed = false;

  const QString label = QString("Adding folder %1…").arg(folder_name);
  int processed = 0;
  if (progress) {
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(250);
    progress->setRange(0, 0);
    progress->setValue(0);
    progress->setLabelText(label);
  }

  QDirIterator it(folder_path, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    if (progress && progress->wasCanceled()) {
      break;
    }
    const QString file_path = it.next();
    const QString rel = normalize_pak_path(base.relativeFilePath(file_path));
    const QString pak_name = pak_root + rel;
    QString err;
    if (!add_file_mapping(pak_name, file_path, &err)) {
      if (failures) {
        failures->push_back(err.isEmpty() ? QString("Failed to add: %1").arg(file_path) : err);
      }
    } else {
      changed = true;
    }

    if (progress && (++processed % 64) == 0) {
      progress->setLabelText(QString("%1 (%2 files)…").arg(label).arg(processed));
      QCoreApplication::processEvents();
    }
  }

  return changed;
}

void PakTab::add_folder() {
  if (!loaded_) {
    return;
  }
  if (wad_mounted_) {
    QMessageBox::information(this, "Mounted WAD", "This WAD view is read-only. Click the Root breadcrumb to go back.");
    return;
  }

  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QFileDialog dialog(this);
  dialog.setWindowTitle("Add Folder");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  if (!default_directory_.isEmpty() && QFileInfo::exists(default_directory_)) {
    dialog.setDirectory(default_directory_);
  }
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
    return;
  }
  default_directory_ = QFileInfo(selected.first()).absoluteFilePath();

  QStringList failures;
  QProgressDialog progress("Adding folder…", "Cancel", 0, 0, this);
  const bool changed = add_folder_from_path(selected.first(), current_prefix(), QString(), &failures, &progress);
  if (progress.wasCanceled()) {
    added_files_ = before_added;
    virtual_dirs_ = before_virtual;
    deleted_files_ = before_deleted_files;
    deleted_dir_prefixes_ = before_deleted_dirs;
    rebuild_added_index();
    refresh_listing();
    return;
  }

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, "Add Folder", failures.mid(0, 12).join("\n"));
  }

  if (changed && undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             "Add Folder",
                                             before_added,
                                             before_virtual,
                                             before_deleted_files,
                                             before_deleted_dirs,
                                             added_files_,
                                             virtual_dirs_,
                                             deleted_files_,
                                             deleted_dir_prefixes_));
  } else if (changed) {
    set_dirty(true);
  }
  refresh_listing();
}

void PakTab::new_folder() {
  if (!loaded_) {
    return;
  }
  if (wad_mounted_) {
    QMessageBox::information(this, "Mounted WAD", "This WAD view is read-only. Click the Root breadcrumb to go back.");
    return;
  }

  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  bool ok = false;
  const QString name = QInputDialog::getText(
    this,
    "New Folder",
    "Folder name:",
    QLineEdit::Normal,
    QString(),
    &ok).trimmed();
  if (!ok || name.isEmpty()) {
    return;
  }
  if (name.contains('/') || name.contains('\\') || name.contains(':') || name == "." || name == "..") {
    QMessageBox::warning(this, "New Folder", "Folder name contains invalid characters.");
    return;
  }

  const QString dir_path = normalize_pak_path(current_prefix() + name) + "/";
  if (!is_safe_entry_name(dir_path)) {
    QMessageBox::warning(this, "New Folder", "Folder name is not valid for PAK paths.");
    return;
  }

  clear_deletions_under(dir_path);
  virtual_dirs_.insert(dir_path);
  if (undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             "New Folder",
                                             before_added,
                                             before_virtual,
                                             before_deleted_files,
                                             before_deleted_dirs,
                                             added_files_,
                                             virtual_dirs_,
                                             deleted_files_,
                                             deleted_dir_prefixes_));
  } else {
    set_dirty(true);
  }
  refresh_listing();
}

bool PakTab::write_pak_file(const QString& dest_path, QString* error) {
  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }

  if (mode_ == Mode::ExistingPak && archive_.is_loaded() && archive_.format() != Archive::Format::Pak) {
    if (error) {
      *error = "This archive was loaded from a ZIP-based format; saving as Quake PAK is not supported (use Save As... with a ZIP format instead).";
    }
    return false;
  }

  // Ensure we have a source archive loaded if we are repacking an existing PAK.
  if (mode_ == Mode::ExistingPak && !archive_.is_loaded() && !pak_path_.isEmpty()) {
    QString load_err;
    if (!archive_.load(pak_path_, &load_err)) {
      if (error) {
        *error = load_err.isEmpty() ? "Unable to load PAK." : load_err;
      }
      return false;
    }
  }

  QFile src;
  qint64 src_size = 0;
  const bool have_src = archive_.is_loaded() && !archive_.path().isEmpty();
  if (have_src) {
    src.setFileName(archive_.path());
    if (!src.open(QIODevice::ReadOnly)) {
      if (error) {
        *error = "Unable to open source PAK for reading.";
      }
      return false;
    }
    src_size = src.size();
  }

  QSaveFile out(abs);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create destination PAK.";
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  header[0] = 'P';
  header[1] = 'A';
  header[2] = 'C';
  header[3] = 'K';
  if (out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to write PAK header.";
    }
    return false;
  }

  QVector<ArchiveEntry> new_entries;
  new_entries.reserve((have_src ? archive_.entries().size() : 0) + added_files_.size());

  constexpr qint64 kChunk = 1 << 16;
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunk));

  auto ensure_u32_pos = [&](qint64 pos, const char* message) -> bool {
    if (pos < 0 || pos > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = message;
      }
      return false;
    }
    return true;
  };

  if (have_src) {
    for (const ArchiveEntry& e : archive_.entries()) {
      const QString name = normalize_pak_path(e.name);
      if (is_deleted_path(name)) {
        continue;
      }
      if (added_index_by_name_.contains(name)) {
        continue;  // overridden by an added/modified file
      }
      if (!is_safe_entry_name(name)) {
        if (error) {
          *error = QString("Refusing to save unsafe entry: %1").arg(name);
        }
        return false;
      }
      const QByteArray name_bytes = name.toLatin1();
      if (name_bytes.isEmpty() || name_bytes.size() > kPakNameBytes) {
        if (error) {
          *error = QString("PAK entry name is too long: %1").arg(name);
        }
        return false;
      }

      const qint64 end = static_cast<qint64>(e.offset) + static_cast<qint64>(e.size);
      if (end < 0 || end > src_size) {
        if (error) {
          *error = QString("PAK entry is out of bounds: %1").arg(name);
        }
        return false;
      }

      const qint64 out_offset64 = out.pos();
      if (!ensure_u32_pos(out_offset64, "PAK output exceeds format limits.")) {
        return false;
      }
      const quint32 out_offset = static_cast<quint32>(out_offset64);

      if (!src.seek(static_cast<qint64>(e.offset))) {
        if (error) {
          *error = QString("Unable to seek source entry: %1").arg(name);
        }
        return false;
      }

      quint32 remaining = e.size;
      while (remaining > 0) {
        const int to_read =
          static_cast<int>(std::min<quint32>(remaining, static_cast<quint32>(buffer.size())));
        const qint64 got = src.read(buffer.data(), to_read);
        if (got <= 0) {
          if (error) {
            *error = QString("Unable to read source entry: %1").arg(name);
          }
          return false;
        }
        if (out.write(buffer.constData(), got) != got) {
          if (error) {
            *error = QString("Unable to write destination entry: %1").arg(name);
          }
          return false;
        }
        remaining -= static_cast<quint32>(got);
      }

      ArchiveEntry out_entry;
      out_entry.name = name;
      out_entry.offset = out_offset;
      out_entry.size = e.size;
      new_entries.push_back(out_entry);
    }
  }

  for (const AddedFile& f : added_files_) {
    const QString name = normalize_pak_path(f.pak_name);
    if (is_deleted_path(name)) {
      continue;
    }
    if (!is_safe_entry_name(name)) {
      if (error) {
        *error = QString("Refusing to save unsafe entry: %1").arg(name);
      }
      return false;
    }
    const QByteArray name_bytes = name.toLatin1();
    if (name_bytes.isEmpty() || name_bytes.size() > kPakNameBytes) {
      if (error) {
        *error = QString("PAK entry name is too long: %1").arg(name);
      }
      return false;
    }

    QFile in(f.source_path);
    if (!in.open(QIODevice::ReadOnly)) {
      if (error) {
        *error = QString("Unable to open file: %1").arg(f.source_path);
      }
      return false;
    }

    const qint64 in_size64 = in.size();
    if (in_size64 < 0 || in_size64 > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = QString("File is too large for PAK format: %1").arg(f.source_path);
      }
      return false;
    }
    const quint32 in_size = static_cast<quint32>(in_size64);

    const qint64 out_offset64 = out.pos();
    if (!ensure_u32_pos(out_offset64, "PAK output exceeds format limits.")) {
      return false;
    }
    const quint32 out_offset = static_cast<quint32>(out_offset64);

    quint32 remaining = in_size;
    while (remaining > 0) {
      const int to_read =
        static_cast<int>(std::min<quint32>(remaining, static_cast<quint32>(buffer.size())));
      const qint64 got = in.read(buffer.data(), to_read);
      if (got <= 0) {
        if (error) {
          *error = QString("Unable to read file: %1").arg(f.source_path);
        }
        return false;
      }
      if (out.write(buffer.constData(), got) != got) {
        if (error) {
          *error = QString("Unable to write destination entry: %1").arg(name);
        }
        return false;
      }
      remaining -= static_cast<quint32>(got);
    }

    ArchiveEntry out_entry;
    out_entry.name = name;
    out_entry.offset = out_offset;
    out_entry.size = in_size;
    new_entries.push_back(out_entry);
  }

  const qint64 dir_offset64 = out.pos();
  if (!ensure_u32_pos(dir_offset64, "PAK output exceeds format limits.")) {
    return false;
  }
  const quint32 dir_offset = static_cast<quint32>(dir_offset64);

  const qint64 dir_length64 = static_cast<qint64>(new_entries.size()) * kPakDirEntrySize;
  if (dir_length64 < 0 || dir_length64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = "PAK directory exceeds format limits.";
    }
    return false;
  }
  const quint32 dir_length = static_cast<quint32>(dir_length64);

  QByteArray dir;
  dir.resize(static_cast<int>(dir_length));
  dir.fill('\0');
  for (int i = 0; i < new_entries.size(); ++i) {
    const ArchiveEntry& e = new_entries[i];
    const QByteArray name_bytes = e.name.toLatin1();
    if (name_bytes.isEmpty() || name_bytes.size() > kPakNameBytes) {
      if (error) {
        *error = QString("PAK entry name is too long: %1").arg(e.name);
      }
      return false;
    }
    const int base = i * kPakDirEntrySize;
    std::memcpy(dir.data() + base, name_bytes.constData(), static_cast<size_t>(name_bytes.size()));
    write_u32_le(&dir, base + kPakNameBytes, e.offset);
    write_u32_le(&dir, base + kPakNameBytes + 4, e.size);
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = "Unable to write PAK directory.";
    }
    return false;
  }

  // Close the source PAK before committing in case we're overwriting in-place.
  src.close();

  write_u32_le(&header, 4, dir_offset);
  write_u32_le(&header, 8, dir_length);
  if (!out.seek(0) || out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to update PAK header.";
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize destination PAK.";
    }
    return false;
  }

  return true;
}

bool PakTab::write_zip_file(const QString& dest_path, bool quakelive_encrypt_pk3, QString* error) {
  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }

  if (mode_ == Mode::ExistingPak && archive_.is_loaded() && archive_.format() != Archive::Format::Zip) {
    if (error) {
      *error = "This archive was loaded from a Quake PAK; saving as ZIP-based formats is not supported (use Save As... with .pak instead).";
    }
    return false;
  }

  // Ensure we have a source archive loaded if we are repacking an existing ZIP.
  if (mode_ == Mode::ExistingPak && !archive_.is_loaded() && !pak_path_.isEmpty()) {
    QString load_err;
    if (!archive_.load(pak_path_, &load_err)) {
      if (error) {
        *error = load_err.isEmpty() ? "Unable to load archive." : load_err;
      }
      return false;
    }
  }

  QFile src_file;
  mz_zip_archive src_zip{};
  bool have_src_zip = false;

  if (mode_ == Mode::ExistingPak && archive_.is_loaded() && archive_.format() == Archive::Format::Zip) {
    const QString src_path = archive_.readable_path();
    if (!src_path.isEmpty()) {
      src_file.setFileName(src_path);
      if (!src_file.open(QIODevice::ReadOnly)) {
        if (error) {
          *error = "Unable to open source ZIP for reading.";
        }
        return false;
      }
      mz_zip_zero_struct(&src_zip);
      src_zip.m_pRead = mz_read_qfile;
      src_zip.m_pNeeds_keepalive = mz_keepalive_qiodevice;
      src_zip.m_pIO_opaque = &src_file;
      const qint64 src_size = src_file.size();
      if (src_size < 0 || !mz_zip_reader_init(&src_zip, static_cast<mz_uint64>(src_size), 0)) {
        const mz_zip_error zerr = mz_zip_get_last_error(&src_zip);
        const char* msg = mz_zip_get_error_string(zerr);
        if (error) {
          *error = msg ? QString("Unable to read source ZIP (%1).").arg(QString::fromLatin1(msg)) : "Unable to read source ZIP.";
        }
        src_file.close();
        return false;
      }
      have_src_zip = true;
    }
  }

  QTemporaryFile temp_zip;
  temp_zip.setAutoRemove(true);
  if (!temp_zip.open()) {
    if (error) {
      *error = "Unable to create temporary ZIP for writing.";
    }
    if (have_src_zip) {
      mz_zip_reader_end(&src_zip);
    }
    return false;
  }

  mz_zip_archive out_zip{};
  mz_zip_zero_struct(&out_zip);
  out_zip.m_pWrite = mz_write_qiodevice;
  out_zip.m_pNeeds_keepalive = mz_keepalive_qiodevice;
  out_zip.m_pIO_opaque = &temp_zip;

  if (!mz_zip_writer_init(&out_zip, 0)) {
    const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
    const char* msg = mz_zip_get_error_string(zerr);
    if (error) {
      *error = msg ? QString("Unable to initialize ZIP writer (%1).").arg(QString::fromLatin1(msg)) : "Unable to initialize ZIP writer.";
    }
    if (have_src_zip) {
      mz_zip_reader_end(&src_zip);
    }
    return false;
  }

  const auto add_error = [&](const QString& message) {
    if (error) {
      *error = message;
    }
  };

  // Clone preserved entries from the source ZIP without recompressing.
  if (have_src_zip) {
    const mz_uint file_count = mz_zip_reader_get_num_files(&src_zip);
    for (mz_uint i = 0; i < file_count; ++i) {
      mz_zip_archive_file_stat st{};
      if (!mz_zip_reader_file_stat(&src_zip, i, &st)) {
        continue;
      }
      QString name = QString::fromUtf8(st.m_filename);
      name = normalize_pak_path(name);
      if (name.isEmpty()) {
        continue;
      }
      if (st.m_is_directory && !name.endsWith('/')) {
        name += '/';
      }

      if (is_deleted_path(name)) {
        continue;
      }
      if (added_index_by_name_.contains(name)) {
        continue;
      }
      if (!is_safe_entry_name(name)) {
        mz_zip_writer_end(&out_zip);
        mz_zip_reader_end(&src_zip);
        add_error(QString("Refusing to save unsafe entry: %1").arg(name));
        return false;
      }

      if (!mz_zip_writer_add_from_zip_reader(&out_zip, &src_zip, i)) {
        const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
        const char* msg = mz_zip_get_error_string(zerr);
        mz_zip_writer_end(&out_zip);
        mz_zip_reader_end(&src_zip);
        add_error(msg ? QString("Unable to copy ZIP entry (%1).").arg(QString::fromLatin1(msg)) : "Unable to copy ZIP entry.");
        return false;
      }
    }
  }

  // Ensure empty directories are preserved as explicit directory entries.
  for (const QString& dir_path_in : virtual_dirs_) {
    QString name = normalize_pak_path(dir_path_in);
    if (name.isEmpty()) {
      continue;
    }
    if (!name.endsWith('/')) {
      name += '/';
    }
    if (is_deleted_path(name)) {
      continue;
    }
    if (!is_safe_entry_name(name)) {
      mz_zip_writer_end(&out_zip);
      if (have_src_zip) {
        mz_zip_reader_end(&src_zip);
      }
      add_error(QString("Refusing to save unsafe directory entry: %1").arg(name));
      return false;
    }

    const QByteArray name_utf8 = name.toUtf8();
    if (!mz_zip_writer_add_mem_ex(&out_zip, name_utf8.constData(), "", 0, nullptr, 0, 0, 0, 0)) {
      const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
      const char* msg = mz_zip_get_error_string(zerr);
      mz_zip_writer_end(&out_zip);
      if (have_src_zip) {
        mz_zip_reader_end(&src_zip);
      }
      add_error(msg ? QString("Unable to add directory entry (%1).").arg(QString::fromLatin1(msg)) : "Unable to add directory entry.");
      return false;
    }
  }

  // Add modified/new files from disk.
  for (const AddedFile& f : added_files_) {
    const QString name = normalize_pak_path(f.pak_name);
    if (name.isEmpty()) {
      continue;
    }
    if (is_deleted_path(name)) {
      continue;
    }
    if (!is_safe_entry_name(name)) {
      mz_zip_writer_end(&out_zip);
      if (have_src_zip) {
        mz_zip_reader_end(&src_zip);
      }
      add_error(QString("Refusing to save unsafe entry: %1").arg(name));
      return false;
    }

    QFile in(f.source_path);
    if (!in.open(QIODevice::ReadOnly)) {
      mz_zip_writer_end(&out_zip);
      if (have_src_zip) {
        mz_zip_reader_end(&src_zip);
      }
      add_error(QString("Unable to open file: %1").arg(f.source_path));
      return false;
    }

    const qint64 size = in.size();
    if (size < 0) {
      mz_zip_writer_end(&out_zip);
      if (have_src_zip) {
        mz_zip_reader_end(&src_zip);
      }
      add_error(QString("Unable to read file size: %1").arg(f.source_path));
      return false;
    }

    const QByteArray name_utf8 = name.toUtf8();
    MZ_TIME_T mtime = 0;
    const MZ_TIME_T* mtime_ptr = nullptr;
    if (f.mtime_utc_secs > 0) {
      mtime = static_cast<MZ_TIME_T>(f.mtime_utc_secs);
      mtime_ptr = &mtime;
    }

    if (!mz_zip_writer_add_read_buf_callback(&out_zip,
                                             name_utf8.constData(),
                                             mz_read_qfile,
                                             &in,
                                             static_cast<mz_uint64>(size),
                                             mtime_ptr,
                                             nullptr,
                                             0,
                                             MZ_DEFAULT_COMPRESSION,
                                             nullptr,
                                             0,
                                             nullptr,
                                             0)) {
      const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
      const char* msg = mz_zip_get_error_string(zerr);
      mz_zip_writer_end(&out_zip);
      if (have_src_zip) {
        mz_zip_reader_end(&src_zip);
      }
      add_error(msg ? QString("Unable to add file to ZIP (%1).").arg(QString::fromLatin1(msg)) : "Unable to add file to ZIP.");
      return false;
    }
  }

  if (!mz_zip_writer_finalize_archive(&out_zip)) {
    const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
    const char* msg = mz_zip_get_error_string(zerr);
    mz_zip_writer_end(&out_zip);
    if (have_src_zip) {
      mz_zip_reader_end(&src_zip);
    }
    add_error(msg ? QString("Unable to finalize ZIP (%1).").arg(QString::fromLatin1(msg)) : "Unable to finalize ZIP.");
    return false;
  }

  mz_zip_writer_end(&out_zip);
  if (have_src_zip) {
    mz_zip_reader_end(&src_zip);
  }

  if (!temp_zip.flush() || !temp_zip.seek(0)) {
    if (error) {
      *error = "Unable to prepare ZIP output for commit.";
    }
    return false;
  }

  QSaveFile out(abs);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create destination archive.";
    }
    return false;
  }

  if (quakelive_encrypt_pk3) {
    QString enc_err;
    if (!quakelive_pk3_xor_stream(temp_zip, out, &enc_err)) {
      if (error) {
        *error = enc_err.isEmpty() ? "Unable to encrypt Quake Live PK3." : enc_err;
      }
      return false;
    }
  } else {
    QByteArray buf;
    buf.resize(1 << 16);
    for (;;) {
      const qint64 got = temp_zip.read(buf.data(), buf.size());
      if (got < 0) {
        if (error) {
          *error = "Unable to read temporary ZIP.";
        }
        return false;
      }
      if (got == 0) {
        break;
      }
      if (out.write(buf.constData(), got) != got) {
        if (error) {
          *error = "Unable to write destination archive.";
        }
        return false;
      }
    }
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize destination archive.";
    }
    return false;
  }

  return true;
}

void PakTab::load_archive() {
  // Leaving any mounted WAD view when (re)loading the outer archive.
  wad_mounted_ = false;
  wad_archive_.reset();
  wad_mount_name_.clear();
  wad_mount_fs_path_.clear();
  outer_dir_before_wad_mount_.clear();

  QString err;
  if (!archive_.load(pak_path_, &err)) {
    loaded_ = false;
    load_error_ = err;
    refresh_listing();
    return;
  }

  loaded_ = true;
  load_error_.clear();
  added_files_.clear();
  added_index_by_name_.clear();
  virtual_dirs_.clear();
  deleted_files_.clear();
  deleted_dir_prefixes_.clear();
  set_dirty(false);
  if (undo_stack_) {
    undo_stack_->clear();
    undo_stack_->setClean();
  }

  // Root listing.
  set_current_dir({});
}

void PakTab::set_current_dir(const QStringList& parts) {
  current_dir_ = parts;

  QString root = "Root";
  if (mode_ == Mode::ExistingPak) {
    const QFileInfo info(pak_path_);
    root = info.fileName().isEmpty() ? "PAK" : info.fileName();
  } else if (!pak_path_.isEmpty()) {
    const QFileInfo info(pak_path_);
    root = info.fileName().isEmpty() ? "PAK" : info.fileName();
  } else {
    root = "New PAK";
  }

  QStringList crumbs;
  crumbs.push_back(root);
  if (wad_mounted_) {
    crumbs.push_back(wad_mount_name_.isEmpty() ? "WAD" : wad_mount_name_);
  }
  for (const QString& p : parts) {
    crumbs.push_back(p);
  }
  if (breadcrumbs_) {
    breadcrumbs_->set_crumbs(crumbs);
  }

  refresh_listing();
}

void PakTab::refresh_listing() {
  stop_thumbnail_generation();
  if (details_view_) {
    details_view_->clear();
  }
  if (icon_view_) {
    icon_view_->clear();
  }

  const bool can_edit = loaded_ && !wad_mounted_;
  if (add_files_action_) {
    add_files_action_->setEnabled(can_edit);
  }
  if (add_folder_action_) {
    add_folder_action_->setEnabled(can_edit);
  }
  if (new_folder_action_) {
    new_folder_action_->setEnabled(can_edit);
  }
  if (delete_action_) {
    delete_action_->setEnabled(can_edit);
  }

  if (!loaded_) {
    const QString msg = load_error_.isEmpty() ? "Failed to load archive." : load_error_;
    if (details_view_) {
      auto* item = new PakTreeItem();
      item->setText(0, msg);
      item->setFlags(Qt::NoItemFlags);
      details_view_->addTopLevelItem(item);
    }
    if (icon_view_) {
      auto* item = new QListWidgetItem(msg);
      item->setFlags(Qt::NoItemFlags);
      icon_view_->addItem(item);
    }
    effective_view_ = ViewMode::Details;
    update_view_controls();
    return;
  }

  QHash<QString, quint32> added_sizes;
  QHash<QString, QString> added_sources;
  QHash<QString, qint64> added_mtimes;
  if (!wad_mounted_) {
    added_sizes.reserve(added_files_.size());
    added_sources.reserve(added_files_.size());
    added_mtimes.reserve(added_files_.size());
    for (const AddedFile& f : added_files_) {
      added_sizes.insert(f.pak_name, f.size);
      added_sources.insert(f.pak_name, f.source_path);
      added_mtimes.insert(f.pak_name, f.mtime_utc_secs);
    }
  }

  const QVector<ArchiveEntry> empty_entries;
  const QVector<ChildListing> children =
    list_children(view_archive().is_loaded() ? view_archive().entries() : empty_entries,
                  added_sizes,
                  added_sources,
                  added_mtimes,
                  wad_mounted_ ? QSet<QString>{} : virtual_dirs_,
                  wad_mounted_ ? QSet<QString>{} : deleted_files_,
                  wad_mounted_ ? QSet<QString>{} : deleted_dir_prefixes_,
                  current_dir_);
  if (children.isEmpty()) {
    const QString msg = (mode_ == Mode::NewPak)
      ? "Empty archive. Use Add Files/Add Folder to add content, then Save As."
      : "No entries in this folder.";
    if (details_view_) {
      auto* item = new PakTreeItem();
      item->setText(0, msg);
      item->setFlags(Qt::NoItemFlags);
      details_view_->addTopLevelItem(item);
    }
    if (icon_view_) {
      auto* item = new QListWidgetItem(msg);
      item->setFlags(Qt::NoItemFlags);
      icon_view_->addItem(item);
    }
    effective_view_ = ViewMode::Details;
    update_view_controls();
    return;
  }

  int file_count = 0;
  int image_count = 0;
  int video_count = 0;
  int model_count = 0;
  for (const ChildListing& child : children) {
    if (child.is_dir) {
      continue;
    }
    ++file_count;
    if (is_image_file_name(child.name)) {
      ++image_count;
    }
    if (is_video_file_name(child.name)) {
      ++video_count;
    }
    if (is_model_file_name(child.name)) {
      ++model_count;
    }
  }

  if (view_mode_ == ViewMode::Auto) {
    apply_auto_view(file_count, image_count, video_count, model_count);
  } else {
    effective_view_ = view_mode_;
  }

  update_view_controls();

  const QIcon dir_icon = style()->standardIcon(QStyle::SP_DirIcon);
  const QIcon file_icon = style()->standardIcon(QStyle::SP_FileIcon);
  const QIcon audio_icon = style()->standardIcon(QStyle::SP_MediaVolume);
  const QIcon cfg_icon = make_badged_icon(file_icon, QSize(32, 32), "{}", palette());
  const QIcon wad_base = make_archive_icon(file_icon, QSize(32, 32), palette());
  const QIcon wad_icon = make_badged_icon(wad_base, QSize(32, 32), "WAD", palette());
  const QIcon model_icon = make_badged_icon(file_icon, QSize(32, 32), "3D", palette());

  const bool show_details = (effective_view_ == ViewMode::Details);
  const bool want_thumbs = (effective_view_ == ViewMode::LargeIcons || effective_view_ == ViewMode::Gallery);
  const bool want_wal_palette = want_thumbs && std::any_of(children.begin(), children.end(), [](const ChildListing& c) {
    return !c.is_dir && file_ext_lower(c.name) == "wal";
  });
  if (want_wal_palette) {
    QString pal_err;
    (void)ensure_quake2_palette(&pal_err);
  }

  if (show_details && details_view_) {
    const bool sorting = details_view_->isSortingEnabled();
    details_view_->setSortingEnabled(false);

    for (const ChildListing& child : children) {
      const QString full_path =
        normalize_pak_path(current_prefix() + child.name + (child.is_dir ? "/" : ""));

      auto* item = new PakTreeItem();
      item->setText(0, child.is_dir ? (child.name + "/") : child.name);
      item->setData(0, kRoleIsDir, child.is_dir);
      item->setData(0, kRolePakPath, full_path);
      item->setData(0, kRoleIsAdded, child.is_added);
      item->setData(0, kRoleIsOverridden, child.is_overridden);
      if (child.is_dir) {
        item->setIcon(0, dir_icon);
      } else {
        const QString leaf = child.name;
        const QString ext = file_ext_lower(leaf);
        if (is_supported_audio_file(leaf)) {
          item->setIcon(0, audio_icon);
        } else if (ext == "wad") {
          item->setIcon(0, wad_icon);
        } else if (is_model_file_name(leaf)) {
          item->setIcon(0, model_icon);
        } else if (ext == "cfg") {
          item->setIcon(0, cfg_icon);
        } else {
          item->setIcon(0, file_icon);
        }
      }

      item->setData(1, kRoleSize, child.is_dir ? static_cast<qint64>(-1) : static_cast<qint64>(child.size));
      item->setText(1, child.is_dir ? "" : format_size(child.size));

      item->setData(2, kRoleMtime, child.is_dir ? static_cast<qint64>(-1) : child.mtime_utc_secs);
      item->setText(2, child.is_dir ? "" : format_mtime(child.mtime_utc_secs));

      if (child.is_overridden) {
        item->setToolTip(0, QString("Modified: %1\nFrom: %2").arg(full_path, child.source_path));
      } else if (child.is_added) {
        item->setToolTip(0, QString("Added: %1\nFrom: %2").arg(full_path, child.source_path));
      } else {
        item->setToolTip(0, full_path);
      }

      if (child.is_added || child.is_overridden) {
        QFont f = item->font(0);
        f.setItalic(true);
        for (int col = 0; col < 3; ++col) {
          item->setFont(col, f);
        }
        if (child.is_added) {
          item->setForeground(0, QBrush(palette().color(QPalette::Highlight)));
        }
      }

      details_view_->addTopLevelItem(item);
    }

    details_view_->setSortingEnabled(sorting);
    if (sorting) {
      details_view_->sortItems(details_view_->sortColumn(), details_view_->header()->sortIndicatorOrder());
    }

    update_preview();
    return;
  }

  if (!show_details && icon_view_) {
    const bool sorting = icon_view_->isSortingEnabled();
    icon_view_->setSortingEnabled(false);

    const QSize icon_size = icon_view_->iconSize().isValid() ? icon_view_->iconSize() : QSize(64, 64);
    const QIcon cfg_icon = make_badged_icon(file_icon, icon_size, "{}", palette());
    const QIcon wad_base = make_archive_icon(file_icon, icon_size, palette());
    const QIcon wad_icon = make_badged_icon(wad_base, icon_size, "WAD", palette());
    const QIcon model_icon = make_badged_icon(file_icon, icon_size, "3D", palette());

    for (const ChildListing& child : children) {
      const QString full_path =
        normalize_pak_path(current_prefix() + child.name + (child.is_dir ? "/" : ""));

      const QString label = child.is_dir ? (child.name + "/") : child.name;
      auto* item = new PakIconItem(label);
      item->setData(kRoleIsDir, child.is_dir);
      item->setData(kRolePakPath, full_path);
      item->setData(kRoleSize, static_cast<qint64>(child.size));
      item->setData(kRoleMtime, child.mtime_utc_secs);
      item->setData(kRoleIsAdded, child.is_added);
      item->setData(kRoleIsOverridden, child.is_overridden);

      icon_items_by_path_.insert(full_path, item);

      QIcon icon = child.is_dir ? dir_icon : file_icon;
      if (!child.is_dir) {
        const QString leaf = child.name;
        const QString ext = file_ext_lower(leaf);
        if (is_supported_audio_file(leaf)) {
          icon = audio_icon;
        } else if (ext == "wad") {
          icon = wad_icon;
        } else if (is_model_file_name(leaf)) {
          icon = model_icon;
          if (want_thumbs) {
            // Thumbnail will be set asynchronously.
            queue_thumbnail(full_path, leaf, child.source_path, static_cast<qint64>(child.size), icon_size);
          }
        } else if (ext == "cfg") {
          icon = cfg_icon;
        } else if (want_thumbs && (is_image_file_name(leaf) || ext == "cin" || ext == "roq")) {
          // Thumbnail will be set asynchronously.
          queue_thumbnail(full_path, leaf, child.source_path, static_cast<qint64>(child.size), icon_size);
        }
      }
      item->setIcon(icon);

      if (child.is_overridden) {
        item->setToolTip(QString("Modified: %1\nFrom: %2").arg(full_path, child.source_path));
      } else if (child.is_added) {
        item->setToolTip(QString("Added: %1\nFrom: %2").arg(full_path, child.source_path));
      } else {
        item->setToolTip(full_path);
      }

      if (child.is_added || child.is_overridden) {
        QFont f = item->font();
        f.setItalic(true);
        item->setFont(f);
        if (child.is_added) {
          item->setForeground(QBrush(palette().color(QPalette::Highlight)));
        }
      }

      icon_view_->addItem(item);
    }

    icon_view_->setSortingEnabled(sorting);
    if (sorting) {
      icon_view_->sortItems();
    }
  }

  update_preview();
}

/*
=============
PakTab::select_adjacent_audio

Select the previous or next audio entry in the active view.
=============
*/
void PakTab::select_adjacent_audio(int delta) {
	if (delta == 0) {
		return;
	}
	if (view_stack_ && view_stack_->currentWidget() == details_view_ && details_view_) {
		const QList<QTreeWidgetItem*> items = details_view_->selectedItems();
		if (items.size() != 1) {
			return;
		}
		QTreeWidgetItem* current = items.first();
		QTreeWidgetItem* parent = current->parent();
		const int count = parent ? parent->childCount() : details_view_->topLevelItemCount();
		const int start = parent ? parent->indexOfChild(current) : details_view_->indexOfTopLevelItem(current);
		for (int i = start + delta; i >= 0 && i < count; i += delta) {
			QTreeWidgetItem* candidate = parent ? parent->child(i) : details_view_->topLevelItem(i);
			if (!candidate) {
				continue;
			}
			const bool is_dir = candidate->data(0, kRoleIsDir).toBool();
			if (is_dir) {
				continue;
			}
			const QString pak_path = candidate->data(0, kRolePakPath).toString();
			const QString leaf = pak_leaf_name(pak_path);
			if (!is_supported_audio_file(leaf)) {
				continue;
			}
			details_view_->clearSelection();
			candidate->setSelected(true);
			details_view_->setCurrentItem(candidate);
			details_view_->scrollToItem(candidate);
			if (preview_) {
				QTimer::singleShot(0, preview_, [preview = preview_]() {
					if (preview) {
						preview->start_playback_from_beginning();
					}
				});
			}
			return;
		}
		return;
	}
	if (!icon_view_) {
		return;
	}
	const QList<QListWidgetItem*> items = icon_view_->selectedItems();
	if (items.size() != 1) {
		return;
	}
	QListWidgetItem* current = items.first();
	const int count = icon_view_->count();
	const int start = icon_view_->row(current);
	for (int i = start + delta; i >= 0 && i < count; i += delta) {
		QListWidgetItem* candidate = icon_view_->item(i);
		if (!candidate) {
			continue;
		}
		const bool is_dir = candidate->data(kRoleIsDir).toBool();
		if (is_dir) {
			continue;
		}
		const QString pak_path = candidate->data(kRolePakPath).toString();
		const QString leaf = pak_leaf_name(pak_path);
		if (!is_supported_audio_file(leaf)) {
			continue;
		}
		icon_view_->clearSelection();
		candidate->setSelected(true);
		icon_view_->setCurrentItem(candidate);
		icon_view_->scrollToItem(candidate);
		if (preview_) {
			QTimer::singleShot(0, preview_, [preview = preview_]() {
				if (preview) {
					preview->start_playback_from_beginning();
				}
			});
		}
		return;
	}
}

/*
=============
PakTab::select_adjacent_video

Select the previous or next video/cinematic entry in the active view.
=============
*/
void PakTab::select_adjacent_video(int delta) {
	if (delta == 0) {
		return;
	}
	if (view_stack_ && view_stack_->currentWidget() == details_view_ && details_view_) {
		const QList<QTreeWidgetItem*> items = details_view_->selectedItems();
		if (items.size() != 1) {
			return;
		}
		QTreeWidgetItem* current = items.first();
		QTreeWidgetItem* parent = current->parent();
		const int count = parent ? parent->childCount() : details_view_->topLevelItemCount();
		const int start = parent ? parent->indexOfChild(current) : details_view_->indexOfTopLevelItem(current);
		for (int i = start + delta; i >= 0 && i < count; i += delta) {
			QTreeWidgetItem* candidate = parent ? parent->child(i) : details_view_->topLevelItem(i);
			if (!candidate) {
				continue;
			}
			const bool is_dir = candidate->data(0, kRoleIsDir).toBool();
			if (is_dir) {
				continue;
			}
			const QString pak_path = candidate->data(0, kRolePakPath).toString();
			const QString leaf = pak_leaf_name(pak_path);
			if (!is_video_file_name(leaf)) {
				continue;
			}
			details_view_->clearSelection();
			candidate->setSelected(true);
			details_view_->setCurrentItem(candidate);
			details_view_->scrollToItem(candidate);
			if (preview_) {
				QTimer::singleShot(0, preview_, [preview = preview_]() {
					if (preview) {
						preview->start_playback_from_beginning();
					}
				});
			}
			return;
		}
		return;
	}
	if (!icon_view_) {
		return;
	}
	const QList<QListWidgetItem*> items = icon_view_->selectedItems();
	if (items.size() != 1) {
		return;
	}
	QListWidgetItem* current = items.first();
	const int count = icon_view_->count();
	const int start = icon_view_->row(current);
	for (int i = start + delta; i >= 0 && i < count; i += delta) {
		QListWidgetItem* candidate = icon_view_->item(i);
		if (!candidate) {
			continue;
		}
		const bool is_dir = candidate->data(kRoleIsDir).toBool();
		if (is_dir) {
			continue;
		}
		const QString pak_path = candidate->data(kRolePakPath).toString();
		const QString leaf = pak_leaf_name(pak_path);
		if (!is_video_file_name(leaf)) {
			continue;
		}
		icon_view_->clearSelection();
		candidate->setSelected(true);
		icon_view_->setCurrentItem(candidate);
		icon_view_->scrollToItem(candidate);
		if (preview_) {
			QTimer::singleShot(0, preview_, [preview = preview_]() {
				if (preview) {
					preview->start_playback_from_beginning();
				}
			});
		}
		return;
	}
}

bool PakTab::mount_wad_from_selected_file(const QString& pak_path_in, QString* error) {
  if (error) {
    error->clear();
  }
  if (!loaded_) {
    if (error) {
      *error = "Archive is not loaded.";
    }
    return false;
  }
  if (wad_mounted_) {
    if (error) {
      *error = "Already viewing a mounted WAD.";
    }
    return false;
  }

  const QString pak_path = normalize_pak_path(pak_path_in);
  if (pak_path.isEmpty()) {
    if (error) {
      *error = "Invalid WAD path.";
    }
    return false;
  }

  const QString leaf = pak_leaf_name(pak_path);
  if (file_ext_lower(leaf) != "wad") {
    if (error) {
      *error = "Not a WAD file.";
    }
    return false;
  }

  QString wad_fs_path;

  // Prefer an overridden/added source file when present.
  const int added_idx = added_index_by_name_.value(pak_path, -1);
  if (added_idx >= 0 && added_idx < added_files_.size()) {
    wad_fs_path = added_files_[added_idx].source_path;
  } else {
    QString err;
    if (!export_path_to_temp(pak_path, false, &wad_fs_path, &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to export WAD for viewing." : err;
      }
      return false;
    }
  }

  if (wad_fs_path.isEmpty() || !QFileInfo::exists(wad_fs_path)) {
    if (error) {
      *error = "Unable to locate WAD file on disk.";
    }
    return false;
  }

  auto inner = std::make_unique<Archive>();
  QString load_err;
  if (!inner->load(wad_fs_path, &load_err) || !inner->is_loaded() || inner->format() != Archive::Format::Wad) {
    if (error) {
      *error = load_err.isEmpty() ? "Unable to open WAD." : load_err;
    }
    return false;
  }

  outer_dir_before_wad_mount_ = current_dir_;
  wad_mounted_ = true;
  wad_archive_ = std::move(inner);
  wad_mount_name_ = leaf;
  wad_mount_fs_path_ = wad_fs_path;

  set_current_dir({});
  return true;
}

void PakTab::unmount_wad() {
  if (!wad_mounted_) {
    return;
  }

  wad_mounted_ = false;
  wad_archive_.reset();
  wad_mount_name_.clear();
  wad_mount_fs_path_.clear();

  const QStringList restore = outer_dir_before_wad_mount_;
  outer_dir_before_wad_mount_.clear();
  set_current_dir(restore);
}

bool PakTab::ensure_quake2_palette(QString* error) {
  if (error) {
    error->clear();
  }
  if (quake2_palette_loaded_) {
    if (quake2_palette_.size() == 256) {
      return true;
    }
    if (error) {
      *error = quake2_palette_error_.isEmpty() ? "Quake II palette is not available." : quake2_palette_error_;
    }
    return false;
  }

  quake2_palette_loaded_ = true;
  quake2_palette_.clear();
  quake2_palette_error_.clear();

  QStringList attempts;

  const auto try_pcx_bytes = [&](const QByteArray& pcx_bytes, const QString& where) -> bool {
    QVector<QRgb> palette;
    QString pal_err;
    if (!extract_pcx_palette_256(pcx_bytes, &palette, &pal_err) || palette.size() != 256) {
      attempts.push_back(QString("%1: %2").arg(where, pal_err.isEmpty() ? "invalid palette" : pal_err));
      return false;
    }
    quake2_palette_ = std::move(palette);
    return true;
  };

  const auto try_pak = [&](const QString& pak_path, const QString& where) -> bool {
    if (pak_path.isEmpty()) {
      return false;
    }
    if (!QFileInfo::exists(pak_path)) {
      attempts.push_back(QString("%1: pak not found (%2)").arg(where, pak_path));
      return false;
    }
    PakArchive pak;
    QString pak_err;
    if (!pak.load(pak_path, &pak_err)) {
      attempts.push_back(QString("%1: %2").arg(where, pak_err.isEmpty() ? "unable to load pak" : pak_err));
      return false;
    }
    QByteArray pcx_bytes;
    QString read_err;
    constexpr qint64 kMaxPcxBytes = 8LL * 1024 * 1024;
    if (!pak.read_entry_bytes("pics/colormap.pcx", &pcx_bytes, &read_err, kMaxPcxBytes)) {
      attempts.push_back(QString("%1: %2").arg(where, read_err.isEmpty() ? "pics/colormap.pcx not found" : read_err));
      return false;
    }
    return try_pcx_bytes(pcx_bytes, where + ": pics/colormap.pcx");
  };

  const auto try_archive = [&](const Archive& ar, const QString& where) -> bool {
    if (!ar.is_loaded()) {
      return false;
    }
    QByteArray pcx_bytes;
    QString read_err;
    constexpr qint64 kMaxPcxBytes = 8LL * 1024 * 1024;
    if (ar.read_entry_bytes("pics/colormap.pcx", &pcx_bytes, &read_err, kMaxPcxBytes)) {
      if (try_pcx_bytes(pcx_bytes, where + ": pics/colormap.pcx")) {
        return true;
      }
    } else {
      attempts.push_back(QString("%1: %2").arg(where, read_err.isEmpty() ? "pics/colormap.pcx not found" : read_err));
    }
    return false;
  };

  // 1) Current archive (most common when viewing pak0.pak).
  // If we're mounted into a WAD, prefer the outer archive for palette lookup.
  if (wad_mounted_) {
    if (try_archive(archive_, "Outer Archive")) {
      return true;
    }
  }
  if (try_archive(view_archive(), wad_mounted_ ? "Mounted WAD" : "Current Archive")) {
    return true;
  }

  // 2) pak0.pak next to the currently-open PAK (covers mods where WALs are in pak1/pak2).
  if (!pak_path_.isEmpty()) {
    const QFileInfo info(pak_path_);
    const QString dir = info.absolutePath();
    if (!dir.isEmpty()) {
      const QString candidate = QDir(dir).filePath("pak0.pak");
      if (try_pak(candidate, "Sibling pak0.pak")) {
        return true;
      }
    }
  }

  // 3) Game-set default directory (or fallback directory).
  if (!default_directory_.isEmpty()) {
    const QDir base(default_directory_);
    const QString pak0 = base.filePath("pak0.pak");
    if (try_pak(pak0, "Default Dir pak0.pak")) {
      return true;
    }
    const QString baseq2_pak0 = base.filePath("baseq2/pak0.pak");
    if (try_pak(baseq2_pak0, "Default Dir baseq2/pak0.pak")) {
      return true;
    }
    const QString rerelease_pak0 = base.filePath("rerelease/baseq2/pak0.pak");
    if (try_pak(rerelease_pak0, "Default Dir rerelease/baseq2/pak0.pak")) {
      return true;
    }

    // If the PCX is unpacked on disk, use it directly.
    const QString pcx_path = base.filePath("pics/colormap.pcx");
    if (QFileInfo::exists(pcx_path)) {
      QFile f(pcx_path);
      if (f.open(QIODevice::ReadOnly)) {
        if (try_pcx_bytes(f.readAll(), "Default Dir pics/colormap.pcx")) {
          return true;
        }
      } else {
        attempts.push_back("Default Dir pics/colormap.pcx: unable to open file");
      }
    }
  }

  quake2_palette_error_ = attempts.isEmpty()
                            ? "Unable to locate Quake II palette (pics/colormap.pcx)."
                            : QString("Unable to locate Quake II palette (pics/colormap.pcx).\nTried:\n- %1").arg(attempts.join("\n- "));
  if (error) {
    *error = quake2_palette_error_;
  }
  return false;
}

bool PakTab::ensure_quake1_palette(QString* error) {
  if (error) {
    error->clear();
  }
  if (quake1_palette_loaded_) {
    if (quake1_palette_.size() == 256) {
      return true;
    }
    if (error) {
      *error = quake1_palette_error_.isEmpty() ? "Quake palette is not available." : quake1_palette_error_;
    }
    return false;
  }

  quake1_palette_loaded_ = true;
  quake1_palette_.clear();
  quake1_palette_error_.clear();

  QStringList attempts;

  const auto try_lmp_bytes = [&](const QByteArray& lmp_bytes, const QString& where) -> bool {
    QVector<QRgb> palette;
    QString pal_err;
    if (!extract_lmp_palette_256(lmp_bytes, &palette, &pal_err) || palette.size() != 256) {
      attempts.push_back(QString("%1: %2").arg(where, pal_err.isEmpty() ? "invalid palette" : pal_err));
      return false;
    }
    quake1_palette_ = std::move(palette);
    return true;
  };

  const auto try_pak = [&](const QString& pak_path, const QString& where) -> bool {
    if (pak_path.isEmpty()) {
      return false;
    }
    if (!QFileInfo::exists(pak_path)) {
      attempts.push_back(QString("%1: pak not found (%2)").arg(where, pak_path));
      return false;
    }
    PakArchive pak;
    QString pak_err;
    if (!pak.load(pak_path, &pak_err)) {
      attempts.push_back(QString("%1: %2").arg(where, pak_err.isEmpty() ? "unable to load pak" : pak_err));
      return false;
    }
    QByteArray lmp_bytes;
    QString read_err;
    constexpr qint64 kMaxLmpBytes = 1024 * 1024;
    if (!pak.read_entry_bytes("gfx/palette.lmp", &lmp_bytes, &read_err, kMaxLmpBytes)) {
      attempts.push_back(QString("%1: %2").arg(where, read_err.isEmpty() ? "gfx/palette.lmp not found" : read_err));
      return false;
    }
    return try_lmp_bytes(lmp_bytes, where + ": gfx/palette.lmp");
  };

  const auto try_archive_palette = [&](const Archive& ar, const QString& where) -> bool {
    if (!ar.is_loaded()) {
      return false;
    }

    QByteArray lmp_bytes;
    QString read_err;
    constexpr qint64 kMaxLmpBytes = 1024 * 1024;

    if (ar.read_entry_bytes("gfx/palette.lmp", &lmp_bytes, &read_err, kMaxLmpBytes)) {
      if (try_lmp_bytes(lmp_bytes, where + ": gfx/palette.lmp")) {
        return true;
      }
    } else {
      attempts.push_back(QString("%1: %2").arg(where, read_err.isEmpty() ? "gfx/palette.lmp not found" : read_err));
    }

    // Some WAD2 texture packs include a raw 256*RGB palette lump named "palette" or "palette.lmp".
    lmp_bytes.clear();
    read_err.clear();
    if (ar.read_entry_bytes("palette.lmp", &lmp_bytes, &read_err, kMaxLmpBytes)) {
      if (try_lmp_bytes(lmp_bytes, where + ": palette.lmp")) {
        return true;
      }
    } else {
      attempts.push_back(QString("%1: %2").arg(where, read_err.isEmpty() ? "palette.lmp not found" : read_err));
    }

    lmp_bytes.clear();
    read_err.clear();
    if (ar.read_entry_bytes("palette", &lmp_bytes, &read_err, kMaxLmpBytes)) {
      if (try_lmp_bytes(lmp_bytes, where + ": palette")) {
        return true;
      }
    } else {
      attempts.push_back(QString("%1: %2").arg(where, read_err.isEmpty() ? "palette not found" : read_err));
    }

    return false;
  };

  // 1) Current archive (most common when viewing pak0.pak, but also supports WADs that contain a raw palette lump).
  // If we're mounted into a WAD, prefer the outer archive for palette lookup.
  if (wad_mounted_) {
    if (try_archive_palette(archive_, "Outer Archive")) {
      return true;
    }
  }
  if (try_archive_palette(view_archive(), wad_mounted_ ? "Mounted WAD" : "Current Archive")) {
    return true;
  }

  // 2) pak0.pak next to the currently-open PAK (covers mods where LMPs are in pak1/pak2).
  if (!pak_path_.isEmpty()) {
    const QFileInfo info(pak_path_);
    const QString dir = info.absolutePath();
    if (!dir.isEmpty()) {
      const QString candidate = QDir(dir).filePath("pak0.pak");
      if (try_pak(candidate, "Sibling pak0.pak")) {
        return true;
      }
    }
  }

  // 3) Game-set default directory (or fallback directory).
  if (!default_directory_.isEmpty()) {
    const QDir base(default_directory_);
    const QString pak0 = base.filePath("pak0.pak");
    if (try_pak(pak0, "Default Dir pak0.pak")) {
      return true;
    }
    const QString id1_pak0 = base.filePath("id1/pak0.pak");
    if (try_pak(id1_pak0, "Default Dir id1/pak0.pak")) {
      return true;
    }
    const QString rerelease_id1_pak0 = base.filePath("rerelease/id1/pak0.pak");
    if (try_pak(rerelease_id1_pak0, "Default Dir rerelease/id1/pak0.pak")) {
      return true;
    }

    // If the LMP is unpacked on disk, use it directly.
    const QString palette_path = base.filePath("gfx/palette.lmp");
    if (QFileInfo::exists(palette_path)) {
      QFile f(palette_path);
      if (f.open(QIODevice::ReadOnly)) {
        if (try_lmp_bytes(f.readAll(), "Default Dir gfx/palette.lmp")) {
          return true;
        }
      } else {
        attempts.push_back("Default Dir gfx/palette.lmp: unable to open file");
      }
    }

    const QString id1_palette_path = base.filePath("id1/gfx/palette.lmp");
    if (QFileInfo::exists(id1_palette_path)) {
      QFile f(id1_palette_path);
      if (f.open(QIODevice::ReadOnly)) {
        if (try_lmp_bytes(f.readAll(), "Default Dir id1/gfx/palette.lmp")) {
          return true;
        }
      } else {
        attempts.push_back("Default Dir id1/gfx/palette.lmp: unable to open file");
      }
    }
  }

  quake1_palette_error_ = attempts.isEmpty()
                            ? "Unable to locate Quake palette (gfx/palette.lmp)."
                            : QString("Unable to locate Quake palette (gfx/palette.lmp).\nTried:\n- %1").arg(attempts.join("\n- "));
  if (error) {
    *error = quake1_palette_error_;
  }
  return false;
}

/*
=============
PakTab::update_preview

Update the preview pane based on the current selection.
=============
*/
void PakTab::update_preview() {
  if (!preview_) {
    return;
  }

  if (!loaded_) {
    preview_->show_message("Preview", load_error_.isEmpty() ? "PAK is not loaded." : load_error_);
    return;
  }

  QString pak_path;
  bool is_dir = false;
  qint64 size = -1;
  qint64 mtime = -1;

  if (view_stack_ && view_stack_->currentWidget() == details_view_ && details_view_) {
    const QList<QTreeWidgetItem*> items = details_view_->selectedItems();
    if (items.isEmpty()) {
      preview_->show_placeholder();
      return;
    }
    if (items.size() > 1) {
      preview_->show_message("Multiple items",
                             QString("%1 items selected.").arg(items.size()));
      return;
    }
    const QTreeWidgetItem* item = items.first();
    is_dir = item->data(0, kRoleIsDir).toBool();
    pak_path = item->data(0, kRolePakPath).toString();
    size = item->data(1, kRoleSize).toLongLong();
    mtime = item->data(2, kRoleMtime).toLongLong();
  } else if (icon_view_) {
    const QList<QListWidgetItem*> items = icon_view_->selectedItems();
    if (items.isEmpty()) {
      preview_->show_placeholder();
      return;
    }
    if (items.size() > 1) {
      preview_->show_message("Multiple items",
                             QString("%1 items selected.").arg(items.size()));
      return;
    }
    const QListWidgetItem* item = items.first();
    is_dir = item->data(kRoleIsDir).toBool();
    pak_path = item->data(kRolePakPath).toString();
    size = item->data(kRoleSize).toLongLong();
    mtime = item->data(kRoleMtime).toLongLong();
  } else {
    preview_->show_placeholder();
    return;
  }

  if (pak_path.isEmpty()) {
    preview_->show_placeholder();
    return;
  }

  const QString leaf = pak_leaf_name(pak_path);
  const QString subtitle = (!is_dir && size >= 0)
                             ? QString("Size: %1  •  Modified: %2")
                                 .arg(format_size(static_cast<quint32>(qMin<qint64>(size, std::numeric_limits<quint32>::max()))),
                                      format_mtime(mtime))
                             : QString("Modified: %1").arg(format_mtime(mtime));

  if (is_dir) {
    preview_->show_message(leaf.isEmpty() ? "Folder" : (leaf + "/"),
                           "Folder. Double-click to open.");
    return;
  }

  const QString ext = file_ext_lower(leaf);
  const bool is_audio = is_supported_audio_file(leaf);
  const bool is_cinematic = (ext == "roq" || ext == "cin");
  const bool is_video = (is_cinematic || ext == "mp4" || ext == "mkv");
  const bool is_model = is_model_file_name(leaf);

  QString source_path;
  const int added_idx = added_index_by_name_.value(normalize_pak_path(pak_path), -1);
  if (added_idx >= 0 && added_idx < added_files_.size()) {
    source_path = added_files_[added_idx].source_path;
  }

  if (is_image_file_name(leaf)) {
    ImageDecodeOptions decode_options;
    if (ext == "wal") {
      QString pal_err;
      if (!ensure_quake2_palette(&pal_err)) {
        preview_->show_message(leaf, pal_err.isEmpty() ? "Unable to locate Quake II palette required for WAL preview." : pal_err);
        return;
      }
      decode_options.palette = &quake2_palette_;
    }
    if (ext == "lmp") {
      const QString lower = leaf.toLower();
      const bool is_palette_lmp = lower.endsWith("palette.lmp");

      QString pal_err;
      if (!ensure_quake1_palette(&pal_err)) {
        if (!is_palette_lmp) {
          preview_->show_message(leaf, pal_err.isEmpty() ? "Unable to locate Quake palette required for LMP preview." : pal_err);
          return;
        }
      } else {
        decode_options.palette = &quake1_palette_;
      }
    }
    if (ext == "mip") {
      QString pal_err;
      if (ensure_quake1_palette(&pal_err)) {
        decode_options.palette = &quake1_palette_;
      }
    }
    if (!source_path.isEmpty()) {
      preview_->show_image_from_file(leaf, subtitle, source_path, decode_options);
      return;
    }
    QByteArray bytes;
    QString err;
    constexpr qint64 kMaxImageBytes = 32LL * 1024 * 1024;
    if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, kMaxImageBytes)) {
      preview_->show_message(leaf, err.isEmpty() ? "Unable to read image from PAK." : err);
      return;
    }
    preview_->show_image_from_bytes(leaf, subtitle, bytes, decode_options);
    return;
  }

	if (is_audio) {
	QString audio_path = source_path;
	if (audio_path.isEmpty()) {
		QString err;
		if (!export_path_to_temp(pak_path, false, &audio_path, &err)) {
			preview_->show_message(leaf, err.isEmpty() ? "Unable to export audio for preview." : err);
			return;
		}
	}
	if (audio_path.isEmpty()) {
		preview_->show_message(leaf, "Unable to export audio for preview.");
		return;
	}
	preview_->show_audio_from_file(leaf, subtitle, audio_path);
	return;
	}

  if (is_video) {
    if (!is_cinematic) {
      preview_->show_message(leaf, "Video preview is not implemented yet.");
      return;
    }

    QString video_path = source_path;
    if (video_path.isEmpty()) {
      QString err;
      if (!export_path_to_temp(pak_path, false, &video_path, &err)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to export cinematic for preview." : err);
        return;
      }
    }
    if (video_path.isEmpty()) {
      preview_->show_message(leaf, "Unable to export cinematic for preview.");
      return;
    }

    preview_->show_cinematic_from_file(leaf, subtitle, video_path);
    return;
  }

  if (is_model) {
    QString model_path = source_path;
    QString skin_path;

    const auto file_base_name = [](const QString& name) -> QString {
      const int dot = name.lastIndexOf('.');
      return dot >= 0 ? name.left(dot) : name;
    };

    const QString model_base = file_base_name(leaf);

    const auto score_skin = [&](const QString& skin_leaf) -> int {
      const QString ext = file_ext_lower(skin_leaf);
      const QString base = file_base_name(skin_leaf);

      int score = 0;
      if (!model_base.isEmpty()) {
        if (base.compare(model_base, Qt::CaseInsensitive) == 0) {
          score += 100;
        } else if (base.startsWith(model_base, Qt::CaseInsensitive)) {
          score += 70;
        }
      }
      if (base.compare("skin", Qt::CaseInsensitive) == 0) {
        score += 80;
      }

      if (ext == "png") {
        score += 20;
      } else if (ext == "tga") {
        score += 18;
      } else if (ext == "jpg" || ext == "jpeg") {
        score += 16;
      } else if (ext == "pcx") {
        score += 14;
      } else if (ext == "wal") {
        score += 12;
      } else if (ext == "dds") {
        score += 10;
      }

      return score;
    };

    const auto find_skin_on_disk = [&](const QString& model_fs_path) -> QString {
      const QFileInfo mi(model_fs_path);
      const QDir d(mi.absolutePath());
      if (!d.exists()) {
        return {};
      }

      const QStringList filters = {
        "*.png", "*.tga", "*.jpg", "*.jpeg", "*.pcx", "*.wal", "*.dds"
      };
      const QStringList files = d.entryList(filters, QDir::Files, QDir::Name);
      if (files.isEmpty()) {
        return {};
      }

      QString best;
      int best_score = -1;
      for (const QString& f : files) {
        const int s = score_skin(f);
        if (s > best_score) {
          best_score = s;
          best = f;
        }
      }
      return best.isEmpty() ? QString() : d.filePath(best);
    };

    const auto find_skin_in_archive = [&](const QString& model_pak_path) -> QString {
      const QString normalized = normalize_pak_path(model_pak_path);
      const int slash = normalized.lastIndexOf('/');
      const QString dir_prefix = (slash >= 0) ? normalized.left(slash + 1) : QString();

      struct Candidate {
        QString pak_path;
        QString leaf;
        int score = 0;
      };

      QVector<Candidate> candidates;
      candidates.reserve(64);

      const auto consider = [&](const QString& pak_name) {
        const QString p = normalize_pak_path(pak_name);
        if (!dir_prefix.isEmpty() && !p.startsWith(dir_prefix)) {
          return;
        }
        const QString rest = dir_prefix.isEmpty() ? p : p.mid(dir_prefix.size());
        if (rest.isEmpty() || rest.contains('/')) {
          return;
        }
        const QString leaf_name = pak_leaf_name(p);
        if (!is_image_file_name(leaf_name)) {
          return;
        }
        candidates.push_back(Candidate{p, leaf_name, score_skin(leaf_name)});
      };

      for (const ArchiveEntry& e : view_archive().entries()) {
        consider(e.name);
      }
      if (!wad_mounted_) {
        for (const AddedFile& f : added_files_) {
          consider(f.pak_name);
        }
      }

      if (candidates.isEmpty()) {
        return {};
      }

      std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) {
          return a.score > b.score;
        }
        return a.leaf.compare(b.leaf, Qt::CaseInsensitive) < 0;
      });

      return candidates.first().pak_path;
    };

    if (model_path.isEmpty()) {
      QString err;
      if (!export_path_to_temp(pak_path, false, &model_path, &err)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to export model for preview." : err);
        return;
      }

      // Try to find and export a skin from the same folder in the archive.
      const QString skin_pak = find_skin_in_archive(pak_path);
      if (!skin_pak.isEmpty()) {
        const QString op_dir = QFileInfo(model_path).absolutePath();
        const QString dest_skin = QDir(op_dir).filePath(pak_leaf_name(skin_pak));

        QString skin_err;
        const int skin_added_idx = added_index_by_name_.value(normalize_pak_path(skin_pak), -1);
        if (skin_added_idx >= 0 && skin_added_idx < added_files_.size()) {
          if (copy_file_stream(added_files_[skin_added_idx].source_path, dest_skin, &skin_err)) {
            skin_path = dest_skin;
          }
        } else {
          if (view_archive().extract_entry_to_file(skin_pak, dest_skin, &skin_err)) {
            skin_path = dest_skin;
          }
        }
      }

      // For MD3 (and other multi-surface formats), try to extract per-surface textures referenced by the model so the
      // model viewer can auto-load them from the exported temp directory.
      if (ext == "md3") {
        QString model_err;
        const std::optional<LoadedModel> loaded_model = load_model_file(model_path, &model_err);
        if (loaded_model && !loaded_model->surfaces.isEmpty()) {
          const QString op_dir = QFileInfo(model_path).absolutePath();
          const QString normalized_model = normalize_pak_path(pak_path);
          const int slash = normalized_model.lastIndexOf('/');
          const QString model_dir_prefix = (slash >= 0) ? normalized_model.left(slash + 1) : QString();

          const QStringList img_exts = {"png", "tga", "jpg", "jpeg", "pcx", "wal", "dds"};

          // Build a quick case-insensitive lookup across the currently-viewed archive + added files.
          QHash<QString, QString> by_lower;
          by_lower.reserve(view_archive().entries().size() + (wad_mounted_ ? 0 : added_files_.size()));
          for (const ArchiveEntry& e : view_archive().entries()) {
            const QString n = normalize_pak_path(e.name);
            if (!n.isEmpty()) {
              by_lower.insert(n.toLower(), e.name);
            }
          }
          if (!wad_mounted_) {
            for (const AddedFile& f : added_files_) {
              const QString n = normalize_pak_path(f.pak_name);
              if (!n.isEmpty()) {
                by_lower.insert(n.toLower(), f.pak_name);
              }
            }
          }

          auto find_entry_ci = [&](const QString& want) -> QString {
            const QString key = normalize_pak_path(want).toLower();
            return key.isEmpty() ? QString() : by_lower.value(key);
          };

          auto extract_entry_to_model_dir = [&](const QString& found_entry) -> void {
            const QString entry_leaf = pak_leaf_name(found_entry);
            if (entry_leaf.isEmpty()) {
              return;
            }
            const QString dest = QDir(op_dir).filePath(entry_leaf);
            if (QFileInfo::exists(dest)) {
              return;
            }

            QString tex_err;
            const int tex_added_idx = added_index_by_name_.value(normalize_pak_path(found_entry), -1);
            if (tex_added_idx >= 0 && tex_added_idx < added_files_.size()) {
              (void)copy_file_stream(added_files_[tex_added_idx].source_path, dest, &tex_err);
              return;
            }
            (void)view_archive().extract_entry_to_file(found_entry, dest, &tex_err);
          };

          QSet<QString> extracted_lower;
          extracted_lower.reserve(32);

          auto consider_shader = [&](const QString& shader_hint) {
            QString sh = shader_hint;
            if (sh.isEmpty()) {
              return;
            }
            sh.replace('\\', '/');
            while (sh.startsWith('/')) {
              sh.remove(0, 1);
            }

            const QFileInfo sfi(sh);
            const QString leaf_name = sfi.fileName();
            const QString base_name = sfi.completeBaseName();
            const QString ext_name = sfi.suffix().toLower();

            QVector<QString> candidates;
            candidates.reserve(32);

            auto add_candidate = [&](const QString& cand) {
              const QString c = normalize_pak_path(cand);
              if (c.isEmpty()) {
                return;
              }
              candidates.push_back(c);
            };

            const bool has_known_ext = img_exts.contains(ext_name);
            if (has_known_ext) {
              add_candidate(sh);
              if (!model_dir_prefix.isEmpty() && !sh.contains('/')) {
                add_candidate(model_dir_prefix + sh);
              }
            } else {
              for (const QString& e : img_exts) {
                add_candidate(QString("%1.%2").arg(sh, e));
              }
              if (!model_dir_prefix.isEmpty() && !sh.contains('/')) {
                for (const QString& e : img_exts) {
                  add_candidate(QString("%1%2.%3").arg(model_dir_prefix, sh, e));
                }
              }
            }

            // If shader includes a path, also try the leaf/base next to the model.
            if (!leaf_name.isEmpty()) {
              if (has_known_ext) {
                add_candidate(model_dir_prefix + leaf_name);
              } else if (!base_name.isEmpty()) {
                for (const QString& e : img_exts) {
                  add_candidate(QString("%1%2.%3").arg(model_dir_prefix, base_name, e));
                }
              }
            }

            for (const QString& want : candidates) {
              const QString found = find_entry_ci(want);
              if (found.isEmpty()) {
                continue;
              }
              const QString leaf_lower = pak_leaf_name(found).toLower();
              if (leaf_lower.isEmpty() || extracted_lower.contains(leaf_lower)) {
                continue;
              }
              extracted_lower.insert(leaf_lower);
              extract_entry_to_model_dir(found);
              if (extracted_lower.size() >= 32) {
                break;
              }
            }
          };

          for (const ModelSurface& s : loaded_model->surfaces) {
            consider_shader(s.shader);
            if (extracted_lower.size() >= 32) {
              break;
            }
          }
        }
      }
    }
    if (model_path.isEmpty()) {
      preview_->show_message(leaf, "Unable to export model for preview.");
      return;
    }
    if (!source_path.isEmpty()) {
      skin_path = find_skin_on_disk(model_path);
    }
    preview_->show_model_from_file(leaf, subtitle, model_path, skin_path);
    return;
  }

  // Text preview (best-effort).
  if (is_text_file_name(leaf)) {
    constexpr qint64 kMaxTextBytes = 512LL * 1024;
    QByteArray bytes;
    bool truncated = (size >= 0 && size > kMaxTextBytes);
    QString err;
    if (!source_path.isEmpty()) {
      QFile f(source_path);
      if (f.open(QIODevice::ReadOnly)) {
        bytes = f.read(kMaxTextBytes);
      } else {
        err = "Unable to open source file for preview.";
      }
    } else {
      if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, kMaxTextBytes)) {
        // handled below
      }
    }
    if (!err.isEmpty()) {
      preview_->show_message(leaf, err);
      return;
    }

    const QString text = QString::fromUtf8(bytes);
    if (!looks_like_text(bytes)) {
      preview_->show_binary(leaf, subtitle, bytes.left(4096), truncated);
      return;
    }
    const QString sub = truncated ? (subtitle + "  (Preview truncated)") : subtitle;
    if (ext == "cfg") {
      preview_->show_cfg(leaf, sub, text);
    } else if (ext == "json") {
      preview_->show_json(leaf, sub, text);
    } else if (ext == "txt") {
      preview_->show_txt(leaf, sub, text);
    } else if (ext == "menu") {
      preview_->show_menu(leaf, sub, text);
    } else if (ext == "shader") {
      preview_->show_shader(leaf, sub, text);
    } else {
      preview_->show_text(leaf, sub, text);
    }
    return;
  }

  // Binary/info preview.
  constexpr qint64 kMaxBinBytes = 4096;
  QByteArray bytes;
  QString err;
  if (!source_path.isEmpty()) {
    QFile f(source_path);
    if (f.open(QIODevice::ReadOnly)) {
      bytes = f.read(kMaxBinBytes);
    } else {
      err = "Unable to open source file for preview.";
    }
  } else {
    view_archive().read_entry_bytes(pak_path, &bytes, &err, kMaxBinBytes);
  }
  if (!err.isEmpty()) {
    preview_->show_message(leaf, err);
    return;
  }
  const bool truncated = (size >= 0 && size > kMaxBinBytes);
  preview_->show_binary(leaf, subtitle, bytes, truncated);
}

void PakTab::enter_directory(const QString& name) {
  QString dir = name;
  if (dir.endsWith('/')) {
    dir.chop(1);
  }
  if (dir.isEmpty()) {
    return;
  }
  QStringList next = current_dir_;
  next.push_back(dir);
  set_current_dir(next);
}

void PakTab::activate_crumb(int index) {
  if (!breadcrumbs_) {
    return;
  }

  // Index 0 is always the outer archive "Root" crumb.
  if (index <= 0) {
    if (wad_mounted_) {
      unmount_wad();
      return;
    }
    set_current_dir({});
    return;
  }

  const QStringList crumbs = breadcrumbs_->crumbs();

  if (wad_mounted_) {
    // Index 1 is the mounted WAD root.
    if (index == 1) {
      set_current_dir({});
      return;
    }

    // Keep crumbs[2..index] as the current directory within the mounted WAD.
    QStringList next;
    for (int i = 2; i <= index && i < crumbs.size(); ++i) {
      next.push_back(crumbs[i]);
    }
    set_current_dir(next);
    return;
  }

  // Keep crumbs[1..index] as the current directory within the outer archive.
  QStringList next;
  for (int i = 1; i <= index && i < crumbs.size(); ++i) {
    next.push_back(crumbs[i]);
  }
  set_current_dir(next);
}
