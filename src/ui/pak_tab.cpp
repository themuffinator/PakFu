#include "pak_tab.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
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
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimeZone>
#include <QBrush>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUndoCommand>
#include <QUuid>
#include <QVBoxLayout>

#include "archive/path_safety.h"
#include "formats/bsp_preview.h"
#include "formats/cinematic.h"
#include "formats/idtech_asset_loader.h"
#include "formats/idwav_audio.h"
#include "formats/image_loader.h"
#include "formats/miptex_image.h"
#include "formats/wal_image.h"
#include "formats/lmp_image.h"
#include "formats/model.h"
#include "formats/pcx_image.h"
#include "formats/quake3_skin.h"
#include "formats/quake3_shader.h"
#include "formats/sprite_loader.h"
#include "third_party/miniz/miniz.h"
#include "pak/pak_archive.h"
#include "platform/file_associations.h"
#include "ui/breadcrumb_bar.h"
#include "ui/preview_pane.h"
#include "ui/preview_renderer.h"
#include "ui/ui_icons.h"
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

QString normalize_pak_path(QString path);
bool is_wad_archive_ext(const QString& ext);
bool is_mountable_archive_ext(const QString& ext);
bool is_mountable_archive_file_name(const QString& name);
bool copy_file_stream(const QString& src_path, const QString& dest_path, QString* error);

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

bool is_quake2_game(GameId id) {
  return id == GameId::Quake2 || id == GameId::Quake2Rerelease || id == GameId::Quake2RTX;
}

QString glow_path_for_fs(const QString& base_path) {
  if (base_path.isEmpty()) {
    return {};
  }
  const QFileInfo fi(base_path);
  const QString base = fi.completeBaseName();
  if (base.isEmpty() || base.endsWith("_glow", Qt::CaseInsensitive)) {
    return {};
  }
  return QDir(fi.absolutePath()).filePath(QString("%1_glow.png").arg(base));
}

QString glow_path_for_pak(const QString& pak_path) {
  const QString normalized = normalize_pak_path(pak_path);
  if (normalized.isEmpty()) {
    return {};
  }
  const int slash = normalized.lastIndexOf('/');
  const QString dir = (slash >= 0) ? normalized.left(slash + 1) : QString();
  const QString leaf = (slash >= 0) ? normalized.mid(slash + 1) : normalized;
  const QFileInfo fi(leaf);
  const QString base = fi.completeBaseName();
  if (base.isEmpty() || base.endsWith("_glow", Qt::CaseInsensitive)) {
    return {};
  }
  return dir + base + "_glow.png";
}

QImage apply_glow_overlay(const QImage& base, const QImage& glow) {
  if (base.isNull() || glow.isNull()) {
    return base;
  }

  QImage base_img = base.convertToFormat(QImage::Format_ARGB32);
  QImage glow_img = glow.convertToFormat(QImage::Format_ARGB32);
  if (base_img.isNull() || glow_img.isNull()) {
    return base;
  }
  if (glow_img.size() != base_img.size()) {
    glow_img = glow_img.scaled(base_img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }

  const int w = base_img.width();
  const int h = base_img.height();

  auto to_linear = [](int c) -> float {
    const float f = static_cast<float>(c) / 255.0f;
    return std::pow(f, 2.2f);
  };
  auto to_srgb = [](float c) -> int {
    const float clamped = std::clamp(c, 0.0f, 1.0f);
    return static_cast<int>(std::round(std::pow(clamped, 1.0f / 2.2f) * 255.0f));
  };

  for (int y = 0; y < h; ++y) {
    auto* base_line = reinterpret_cast<QRgb*>(base_img.scanLine(y));
    const auto* glow_line = reinterpret_cast<const QRgb*>(glow_img.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb b = base_line[x];
      const QRgb g = glow_line[x];
      const int ga = qAlpha(g);
      if (ga <= 0) {
        continue;
      }

      const float glow_alpha = static_cast<float>(ga) / 255.0f;
      const float glow_rgb = std::max({qRed(g), qGreen(g), qBlue(g)}) / 255.0f;
      const float glow_mask = std::clamp(glow_alpha * glow_rgb, 0.0f, 1.0f);
      if (glow_mask <= 0.0f) {
        continue;
      }

      // Fullbright pixels keep their base color without lighting influence.
      const float base_r = to_linear(qRed(b));
      const float base_g = to_linear(qGreen(b));
      const float base_b = to_linear(qBlue(b));
      const int out_a = qAlpha(b);

      base_line[x] = qRgba(to_srgb(base_r), to_srgb(base_g), to_srgb(base_b), out_a);
    }
  }

  return base_img;
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
                                    qint64 fallback_mtime_utc_secs,
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
    item.mtime_utc_secs = (e.mtime_utc_secs >= 0) ? e.mtime_utc_secs : fallback_mtime_utc_secs;
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
constexpr int kSinDirEntrySize = 128;
constexpr int kSinNameBytes = 120;
constexpr int kWadHeaderSize = 12;
constexpr int kWadDirEntrySize = 32;
constexpr int kWadNameBytes = 16;
constexpr quint8 kWadTypeNone = 0;
constexpr quint8 kWadTypeQpic = static_cast<quint8>('B');
constexpr quint8 kWadTypeMiptexWad2 = static_cast<quint8>('D');
constexpr quint8 kWadTypeLumpy = 64;

[[nodiscard]] quint32 read_u32_le_from(const char* p) {
  const quint32 b0 = static_cast<quint8>(p[0]);
  const quint32 b1 = static_cast<quint8>(p[1]);
  const quint32 b2 = static_cast<quint8>(p[2]);
  const quint32 b3 = static_cast<quint8>(p[3]);
  return (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

[[nodiscard]] bool looks_like_qpic_lump_bytes(const QByteArray& bytes) {
  if (bytes.size() < 8) {
    return false;
  }
  const quint32 w = read_u32_le_from(bytes.constData() + 0);
  const quint32 h = read_u32_le_from(bytes.constData() + 4);
  if (w == 0 || h == 0) {
    return false;
  }
  constexpr quint32 kMaxDim = 16384;
  if (w > kMaxDim || h > kMaxDim) {
    return false;
  }
  const quint64 want = 8ULL + static_cast<quint64>(w) * static_cast<quint64>(h);
  return want == static_cast<quint64>(bytes.size());
}

bool derive_wad2_lump_name(const QString& entry_name_in, QString* out_lump_name, QString* error) {
  if (error) {
    error->clear();
  }

  const QString entry_name = normalize_pak_path(entry_name_in);
  if (entry_name.isEmpty()) {
    if (error) {
      *error = "WAD entry name is empty.";
    }
    return false;
  }
  if (entry_name.contains('/')) {
    if (error) {
      *error = QString("WAD entries cannot contain folders: %1").arg(entry_name);
    }
    return false;
  }

  QString lump_name = entry_name;
  const int dot = lump_name.lastIndexOf('.');
  if (dot > 0) {
    const QString ext = lump_name.mid(dot + 1).toLower();
    if (ext == "mip" || ext == "lmp") {
      lump_name = lump_name.left(dot);
    }
  }

  if (lump_name.isEmpty()) {
    if (error) {
      *error = QString("WAD entry has an invalid lump name: %1").arg(entry_name);
    }
    return false;
  }

  const QByteArray lump_latin1 = lump_name.toLatin1();
  if (QString::fromLatin1(lump_latin1) != lump_name) {
    if (error) {
      *error = QString("WAD entry name must be Latin-1: %1").arg(entry_name);
    }
    return false;
  }
  if (lump_latin1.size() > kWadNameBytes) {
    if (error) {
      *error = QString("WAD lump names are limited to %1 bytes: %2").arg(kWadNameBytes).arg(lump_name);
    }
    return false;
  }

  if (out_lump_name) {
    *out_lump_name = lump_name;
  }
  return true;
}

[[nodiscard]] quint8 derive_wad2_lump_type(const QString& entry_name_in,
                                           const QString& lump_name,
                                           const QByteArray* bytes = nullptr) {
  const QString lower = normalize_pak_path(entry_name_in).toLower();
  if (lower.endsWith(".mip")) {
    return kWadTypeMiptexWad2;
  }
  if (lower.endsWith(".lmp")) {
    if (lump_name.compare("palette", Qt::CaseInsensitive) == 0) {
      return kWadTypeLumpy;
    }
    if (bytes && looks_like_qpic_lump_bytes(*bytes)) {
      return kWadTypeQpic;
    }
    return kWadTypeQpic;
  }
  if (bytes && looks_like_qpic_lump_bytes(*bytes)) {
    return kWadTypeQpic;
  }
  return kWadTypeNone;
}

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

struct PakFuMimePayload {
  bool cut = false;
  QString source_uid;
  QString source_archive;
  QVector<QPair<QString, bool>> items;
};

bool pak_paths_equal(const QString& a_in, const QString& b_in) {
  const QString a = normalize_pak_path(a_in);
  const QString b = normalize_pak_path(b_in);
#if defined(Q_OS_WIN)
  return a.compare(b, Qt::CaseInsensitive) == 0;
#else
  return a == b;
#endif
}

bool fs_paths_equal(const QString& a_in, const QString& b_in) {
  if (a_in.isEmpty() || b_in.isEmpty()) {
    return false;
  }
  const QString a = QFileInfo(a_in).absoluteFilePath();
  const QString b = QFileInfo(b_in).absoluteFilePath();
#if defined(Q_OS_WIN)
  return a.compare(b, Qt::CaseInsensitive) == 0;
#else
  return a == b;
#endif
}

bool pak_path_is_under(const QString& path_in, const QString& root_in) {
  const QString path = normalize_pak_path(path_in);
  QString root = normalize_pak_path(root_in);
  if (path.isEmpty() || root.isEmpty()) {
    return false;
  }
  if (!root.endsWith('/')) {
    root += '/';
  }
#if defined(Q_OS_WIN)
  return path.startsWith(root, Qt::CaseInsensitive);
#else
  return path.startsWith(root);
#endif
}

QString normalize_local_fs_path(QString path) {
  path = path.trimmed();
  if (path.size() >= 2) {
    const bool quoted =
      (path.startsWith('"') && path.endsWith('"')) ||
      (path.startsWith('\'') && path.endsWith('\''));
    if (quoted) {
      path = path.mid(1, path.size() - 2).trimmed();
    }
  }
  path = QDir::fromNativeSeparators(path);
#if defined(Q_OS_WIN)
  if (path.startsWith("//?/UNC/", Qt::CaseInsensitive)) {
    path = "//" + path.mid(8);
  } else if (path.startsWith("//?/", Qt::CaseInsensitive)) {
    path = path.mid(4);
  }
#endif
  return path;
}

void append_existing_local_path(QList<QUrl>* out, QSet<QString>* seen, const QString& local_path_in) {
  if (!out || !seen) {
    return;
  }
  const QString local_path = normalize_local_fs_path(local_path_in);
  if (local_path.isEmpty()) {
    return;
  }

  const QFileInfo info(local_path);
  if (!info.exists()) {
    return;
  }

  QString key = QDir::cleanPath(info.absoluteFilePath());
#if defined(Q_OS_WIN)
  key = key.toLower();
#endif
  if (seen->contains(key)) {
    return;
  }
  seen->insert(key);
  out->push_back(QUrl::fromLocalFile(info.absoluteFilePath()));
}

QStringList decode_windows_filenamew_payload(const QByteArray& payload) {
  QStringList out;
  if (payload.isEmpty()) {
    return out;
  }

  const int bytes = payload.size() - (payload.size() % 2);
  if (bytes <= 0) {
    return out;
  }

  const uchar* data = reinterpret_cast<const uchar*>(payload.constData());
  const int count = bytes / 2;
  QString current;
  for (int i = 0; i < count; ++i) {
    const ushort ch = static_cast<ushort>(data[(i * 2) + 0]) |
                      static_cast<ushort>(data[(i * 2) + 1] << 8);
    if (ch == 0) {
      if (!current.isEmpty()) {
        out.push_back(current);
        current.clear();
      }
      continue;
    }
    current.append(QChar(ch));
  }
  if (!current.isEmpty()) {
    out.push_back(current);
  }
  return out;
}

QStringList decode_windows_filename_payload(const QByteArray& payload) {
  QStringList out;
  if (payload.isEmpty()) {
    return out;
  }

  const QList<QByteArray> chunks = payload.split('\0');
  out.reserve(chunks.size());
  for (const QByteArray& chunk : chunks) {
    if (chunk.isEmpty()) {
      continue;
    }
    const QString path = QString::fromLocal8Bit(chunk).trimmed();
    if (!path.isEmpty()) {
      out.push_back(path);
    }
  }
  return out;
}

QList<QUrl> local_urls_from_mime(const QMimeData* mime) {
  QList<QUrl> out;
  if (!mime) {
    return out;
  }

  QSet<QString> seen;

  const QList<QUrl> urls = mime->urls();
  for (const QUrl& url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }
    append_existing_local_path(&out, &seen, url.toLocalFile());
  }

  if (mime->hasText()) {
    const QString text = mime->text();
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (QString line : lines) {
      line = line.trimmed();
      if (line.isEmpty() || line.startsWith('#')) {
        continue;
      }
      if (line.startsWith("file:", Qt::CaseInsensitive)) {
        const QUrl url(line);
        if (url.isLocalFile()) {
          append_existing_local_path(&out, &seen, url.toLocalFile());
        }
        continue;
      }
      append_existing_local_path(&out, &seen, line);
    }
  }

#if defined(Q_OS_WIN)
  const QStringList formats = mime->formats();
  for (const QString& format : formats) {
    const QString lower = format.toLower();
    if (lower.startsWith("application/x-qt-windows-mime;value=\"filenamew\"")) {
      const QStringList paths = decode_windows_filenamew_payload(mime->data(format));
      for (const QString& path : paths) {
        append_existing_local_path(&out, &seen, path);
      }
      continue;
    }
    if (lower.startsWith("application/x-qt-windows-mime;value=\"filename\"")) {
      const QStringList paths = decode_windows_filename_payload(mime->data(format));
      for (const QString& path : paths) {
        append_existing_local_path(&out, &seen, path);
      }
    }
  }
#endif

  return out;
}

Qt::DropAction resolve_requested_drop_action(Qt::DropAction drop_action,
                                             Qt::DropAction proposed_action,
                                             Qt::DropActions possible_actions,
                                             Qt::KeyboardModifiers modifiers) {
  auto ensure_supported = [possible_actions](Qt::DropAction wanted) -> Qt::DropAction {
    if (wanted != Qt::IgnoreAction && (possible_actions & wanted)) {
      return wanted;
    }
    if (possible_actions & Qt::CopyAction) {
      return Qt::CopyAction;
    }
    if (possible_actions & Qt::MoveAction) {
      return Qt::MoveAction;
    }
    if (possible_actions & Qt::LinkAction) {
      return Qt::LinkAction;
    }
    return Qt::IgnoreAction;
  };

  Qt::DropAction chosen = (drop_action != Qt::IgnoreAction) ? drop_action : proposed_action;
  if (chosen == Qt::IgnoreAction) {
    if (modifiers & Qt::ControlModifier) {
      chosen = Qt::CopyAction;
    } else if (modifiers & Qt::ShiftModifier) {
      chosen = Qt::MoveAction;
    }
  }

  return ensure_supported(chosen);
}

bool parse_pakfu_mime(const QMimeData* mime, PakFuMimePayload* out) {
  if (!out) {
    return false;
  }
  *out = {};
  if (!mime || !mime->hasFormat(kPakFuMimeType)) {
    return false;
  }
  const QByteArray payload = mime->data(kPakFuMimeType);
  if (payload.isEmpty()) {
    return false;
  }

  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }

  const QJsonObject root = doc.object();
  out->cut = root.value("cut").toBool(false);
  out->source_uid = root.value("source_uid").toString();
  out->source_archive = root.value("source_archive").toString();

  const QJsonArray items = root.value("items").toArray();
  out->items.reserve(items.size());
  for (const QJsonValue& v : items) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject it = v.toObject();
    QString pak_path = normalize_pak_path(it.value("pak_path").toString());
    const bool is_dir = it.value("is_dir").toBool(false);
    if (pak_path.isEmpty()) {
      continue;
    }
    if (is_dir && !pak_path.endsWith('/')) {
      pak_path += '/';
    }
    out->items.push_back(qMakePair(std::move(pak_path), is_dir));
  }

  return true;
}

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
      if (is_mountable_archive_ext(ext)) {
        return 1;  // container files
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
      if (is_mountable_archive_ext(ext)) {
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
    "png", "jpg", "jpeg", "bmp", "gif", "tga", "pcx", "wal", "swl", "dds", "lmp", "mip", "ftx", "tif", "tiff"
  };
  return kImageExts.contains(ext);
}

bool is_sprite_file_ext(const QString& ext) {
  return ext == "spr" || ext == "sp2" || ext == "spr2";
}

bool is_sprite_file_name(const QString& name) {
  const QString lower = name.toLower();
  const int dot = lower.lastIndexOf('.');
  if (dot < 0) {
    return false;
  }
  return is_sprite_file_ext(lower.mid(dot + 1));
}

QImage make_centered_icon_frame(const QImage& image, const QSize& icon_size, bool smooth) {
  if (image.isNull() || !icon_size.isValid()) {
    return {};
  }
  const QImage scaled = image.scaled(icon_size, Qt::KeepAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
  QImage square(icon_size, QImage::Format_ARGB32_Premultiplied);
  square.fill(Qt::transparent);
  QPainter p(&square);
  p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
  const int ox = (icon_size.width() - scaled.width()) / 2;
  const int oy = (icon_size.height() - scaled.height()) / 2;
  p.drawImage(QPoint(ox, oy), scaled);
  return square;
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
  fill.setAlpha(105);

  QColor stroke = fill;
  stroke.setAlpha(200);

  const int w = icon_size.width();
  const int h = icon_size.height();
  const int box_h = qMax(8, h / 4);
  const QRectF box(w * 0.18f, h - box_h - 2.0, w * 0.64f, static_cast<qreal>(box_h));

  QPen pen(stroke);
  pen.setWidth(qMax(1, w / 32));
  p.setPen(pen);
  p.setBrush(fill);
  p.drawRoundedRect(box, 2.0, 2.0);

  // Make WADs look distinct: a small "crate" with slats + tab (instead of a zipper).
  QColor slat = pal.color(QPalette::Base);
  if (!slat.isValid()) {
    slat = Qt::white;
  }
  slat.setAlpha(175);

  const QPen slat_pen(slat, qMax(1, w / 64));
  p.setPen(slat_pen);
  const qreal pad = qMax(2.0, box.width() * 0.08);
  const qreal top = box.top() + 2.0;
  const qreal bot = box.bottom() - 2.0;
  p.drawLine(QPointF(box.left() + pad, top), QPointF(box.left() + pad, bot));
  p.drawLine(QPointF(box.center().x(), top), QPointF(box.center().x(), bot));
  p.drawLine(QPointF(box.right() - pad, top), QPointF(box.right() - pad, bot));

  QRectF tab(box.left() + box.width() * 0.28f,
             box.top() - box.height() * 0.28f,
             box.width() * 0.44f,
             box.height() * 0.28f);
  tab = tab.intersected(QRectF(0, 0, w, h));
  p.setPen(pen);
  QColor tab_fill = fill.lighter(115);
  tab_fill.setAlpha(fill.alpha());
  p.setBrush(tab_fill);
  p.drawRoundedRect(tab, 1.5, 1.5);

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
  const float radius = qMax(0.001f, 0.5f * ext.length());

  const float aspect = static_cast<float>(size.width()) / static_cast<float>(size.height());
  constexpr float fovy_deg = 45.0f;
  constexpr float kPi = 3.14159265358979323846f;
  const float tan_half_fovy = std::tan((fovy_deg * 0.5f) * (kPi / 180.0f));
  const float tan_half_fovx = tan_half_fovy * aspect;
  const float tan_min = qMax(0.001f, qMin(tan_half_fovy, tan_half_fovx));
  const float dist = qMax(1.0f, (radius / tan_min) * 1.10f);

  QMatrix4x4 v;
  {
    // Match ModelViewerWidget defaults (yaw=45, pitch=20).
    const float yaw = 45.0f * (kPi / 180.0f);
    const float pitch = 20.0f * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const QVector3D dir(cp * cy, cp * sy, sp);
    const QVector3D cam_pos = center + dir.normalized() * dist;
    v.lookAt(cam_pos, center, QVector3D(0, 0, 1));
  }

  QMatrix4x4 pmat;
  pmat.perspective(fovy_deg, aspect, qMax(0.001f, radius * 0.02f), qMax(10.0f, radius * 50.0f));

  const QMatrix4x4 mvp = pmat * v;
  const QVector3D light_dir = QVector3D(0.4f, 0.25f, 1.0f).normalized();

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

    const QVector3D n0 = QVector3D(v0.nx, v0.ny, v0.nz).normalized();
    const QVector3D n1 = QVector3D(v1.nx, v1.ny, v1.nz).normalized();
    const QVector3D n2 = QVector3D(v2.nx, v2.ny, v2.nz).normalized();
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

bool is_wad_archive_ext(const QString& ext) {
  return ext == "wad" || ext == "wad2" || ext == "wad3";
}

bool is_quake_wad_archive_ext(const QString& ext) {
  return ext == "wad2" || ext == "wad3";
}

bool is_sin_archive_ext(const QString& ext) {
  return ext == "sin";
}

bool is_sin_archive_path(const QString& path) {
  return is_sin_archive_ext(file_ext_lower(path));
}

bool is_mountable_archive_ext(const QString& ext) {
  return is_wad_archive_ext(ext) || ext == "pak" || is_sin_archive_ext(ext) || ext == "zip" || ext == "pk3" || ext == "pk4" ||
         ext == "pkz" || ext == "resources";
}

bool is_mountable_archive_file_name(const QString& name) {
  return is_mountable_archive_ext(file_ext_lower(name));
}

QSize sanitize_icon_size(const QSize& icon_size, const QSize& fallback = QSize(32, 32)) {
  return icon_size.isValid() ? icon_size : fallback;
}

bool icon_pixmaps_match(const QIcon& a, const QIcon& b, const QSize& icon_size) {
  const QSize size = sanitize_icon_size(icon_size);
  const QPixmap pa = a.pixmap(size);
  const QPixmap pb = b.pixmap(size);
  if (pa.isNull() || pb.isNull()) {
    return false;
  }
  return pa.toImage() == pb.toImage();
}

QIcon platform_file_association_icon(const QString& ext, const QSize& icon_size) {
  const QString normalized_ext = ext.trimmed().toLower();
  if (normalized_ext.isEmpty()) {
    return {};
  }

  const QSize size = sanitize_icon_size(icon_size);
  const QString key = QString("%1@%2x%3").arg(normalized_ext).arg(size.width()).arg(size.height());

  static QFileIconProvider provider;
  static QHash<QString, QIcon> cache;
  static QSet<QString> misses;

  if (cache.contains(key)) {
    return cache.value(key);
  }
  if (misses.contains(key)) {
    return {};
  }

  const QIcon generic = provider.icon(QFileIconProvider::File);
  const QIcon candidate = provider.icon(QFileInfo(QString("pakfu_assoc.%1").arg(normalized_ext)));

  if (candidate.isNull() || icon_pixmaps_match(candidate, generic, size)) {
    misses.insert(key);
    return {};
  }

  cache.insert(key, candidate);
  return candidate;
}

bool try_file_association_icon(const QString& file_name, const QSize& icon_size, QIcon* out) {
  if (out) {
    *out = {};
  }

  const QString ext = file_ext_lower(file_name);
  if (ext.isEmpty()) {
    return false;
  }

  const QSize size = sanitize_icon_size(icon_size);
  const QIcon managed = FileAssociations::icon_for_extension(ext, size);
  if (!managed.isNull()) {
    if (out) {
      *out = managed;
    }
    return true;
  }

  const QIcon platform = platform_file_association_icon(ext, size);
  if (!platform.isNull()) {
    if (out) {
      *out = platform;
    }
    return true;
  }

  return false;
}

/*
=============
is_supported_audio_file

Return true when a file name uses a supported audio extension.
=============
*/
bool is_supported_audio_file(const QString& name) {
	const QString ext = file_ext_lower(name);
	return (ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "idwav" || ext == "bik");
}

bool is_video_file_name(const QString& name) {
	const QString ext = file_ext_lower(name);
	return (ext == "cin" || ext == "roq" || ext == "bik" || ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "ogv" || ext == "webm");
}

bool is_model_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  return (ext == "mdl" || ext == "md2" || ext == "md3" || ext == "mdc" || ext == "md4" || ext == "mdr" || ext == "skb" || ext == "skd" || ext == "mdm" || ext == "glm" || ext == "iqm" || ext == "md5mesh" ||
          ext == "tan" ||
          ext == "obj" ||
          ext == "lwo");
}

bool is_bsp_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  return (ext == "bsp");
}

bool is_font_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  return (ext == "ttf" || ext == "otf");
}

bool is_cfg_like_text_ext(const QString& ext) {
  return ext == "cfg" || ext == "config" || ext == "rc" || ext == "arena" || ext == "bot" || ext == "skin" || ext == "shaderlist";
}

bool is_plain_text_script_ext(const QString& ext) {
  static const QSet<QString> kPlainTextScriptExts = {
    "txt", "log", "md", "ini", "xml", "lst", "lang", "tik", "anim", "cam", "camera", "char", "voice", "gui", "bgui", "efx", "guide", "lipsync", "viseme", "vdf",
    "st", "lip", "tlk", "mus", "snd", "ritualfont",
    "def", "mtr", "sndshd", "af", "pd", "decl", "ent", "map", "sab", "siege", "veh", "npc", "jts", "bset", "weap", "ammo",
    "campaign"
  };
  return kPlainTextScriptExts.contains(ext);
}

bool is_text_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  static const QSet<QString> kTextExts = {
    "cfg", "config", "rc", "arena", "bot", "skin", "shaderlist", "txt", "log", "md", "ini", "json", "xml", "shader", "menu", "script",
    "lst", "lang", "tik", "anim", "cam", "camera", "char", "voice", "gui", "bgui", "efx", "guide", "lipsync", "viseme", "vdf", "st", "lip", "tlk", "mus", "snd", "ritualfont", "def", "mtr", "sndshd", "af", "pd", "decl", "ent", "map",
    "qc", "sab", "siege", "veh", "npc", "jts", "bset", "weap", "ammo", "campaign", "c", "h"
  };
  return kTextExts.contains(ext);
}

QString canonical_doom_lump_name(QString name) {
  name = name.trimmed();
  name.replace('\\', '/');
  const int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.mid(slash + 1);
  }
  const int dot = name.indexOf('.');
  if (dot > 0) {
    name = name.left(dot);
  }
  const int us = name.lastIndexOf('_');
  if (us > 0 && us + 1 < name.size()) {
    bool numeric_suffix = true;
    for (int i = us + 1; i < name.size(); ++i) {
      if (!name[i].isDigit()) {
        numeric_suffix = false;
        break;
      }
    }
    if (numeric_suffix) {
      name = name.left(us);
    }
  }
  return name.toUpper();
}

bool is_doom_map_marker_name(const QString& name) {
  const QString n = canonical_doom_lump_name(name);
  if (n.size() == 4 && n[0] == 'E' && n[2] == 'M' && n[1].isDigit() && n[3].isDigit()) {
    return true;
  }
  if (n.size() == 5 && n.startsWith("MAP") && n[3].isDigit() && n[4].isDigit()) {
    return true;
  }
  return false;
}

bool is_doom_map_lump_name(const QString& name) {
  const QString n = canonical_doom_lump_name(name);
  static const QSet<QString> kMapLumps = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP",
    "BEHAVIOR", "SCRIPTS", "TEXTMAP", "ZNODES", "LEAFS", "GL_VERT", "GL_SEGS", "GL_SSECT", "GL_NODES", "GL_PVS",
    "GL_PORTALS"
  };
  return kMapLumps.contains(n);
}

int doom_count_from_size(qint64 size, int stride) {
  if (size <= 0 || stride <= 0) {
    return 0;
  }
  return static_cast<int>(size / stride);
}

QString format_doom_lump_line(const QString& lump, qint64 size) {
  return QString("%1: %2 bytes").arg(lump).arg(size);
}

QString build_doom_map_summary(const QVector<ArchiveEntry>& entries, int marker_index, QString* error) {
  if (error) {
    error->clear();
  }
  if (marker_index < 0 || marker_index >= entries.size()) {
    if (error) {
      *error = "Invalid Doom map marker index.";
    }
    return {};
  }

  const QString marker = canonical_doom_lump_name(entries[marker_index].name);
  if (!is_doom_map_marker_name(marker)) {
    if (error) {
      *error = "Selected lump is not a Doom map marker.";
    }
    return {};
  }

  struct LumpInfo {
    QString name;
    qint64 size = 0;
  };
  QHash<QString, LumpInfo> lumps;

  for (int i = marker_index + 1; i < entries.size(); ++i) {
    const QString cname = canonical_doom_lump_name(entries[i].name);
    if (is_doom_map_marker_name(cname)) {
      break;
    }
    if (!is_doom_map_lump_name(cname)) {
      continue;
    }
    if (!lumps.contains(cname)) {
      lumps.insert(cname, LumpInfo{cname, entries[i].size});
    }
  }

  if (lumps.isEmpty()) {
    if (error) {
      *error = "No Doom map lumps were found after this marker.";
    }
    return {};
  }

  const bool has_textmap = lumps.contains("TEXTMAP");
  const bool has_behavior = lumps.contains("BEHAVIOR");
  const bool has_gl_nodes = lumps.contains("GL_NODES") || lumps.contains("ZNODES");

  QString map_format = "Doom / Strife binary";
  if (has_textmap) {
    map_format = "UDMF (text map)";
  } else if (has_behavior) {
    map_format = "Hexen binary";
  }

  const int thing_stride = has_behavior ? 20 : 10;
  const int linedef_stride = has_behavior ? 16 : 14;

  const qint64 things_size = lumps.value("THINGS").size;
  const qint64 linedefs_size = lumps.value("LINEDEFS").size;
  const qint64 sidedefs_size = lumps.value("SIDEDEFS").size;
  const qint64 vertexes_size = lumps.value("VERTEXES").size;
  const qint64 segs_size = lumps.value("SEGS").size;
  const qint64 ssectors_size = lumps.value("SSECTORS").size;
  const qint64 nodes_size = lumps.value("NODES").size;
  const qint64 sectors_size = lumps.value("SECTORS").size;
  const qint64 reject_size = lumps.value("REJECT").size;
  const qint64 blockmap_size = lumps.value("BLOCKMAP").size;

  QString summary;
  QTextStream s(&summary);
  s << "Type: idTech1 Doom-family map\n";
  s << "Map marker: " << marker << "\n";
  s << "Format: " << map_format << "\n";
  s << "Lump count: " << lumps.size() << "\n";
  s << "Things: " << doom_count_from_size(things_size, thing_stride);
  if (things_size > 0 && (things_size % thing_stride) != 0) {
    s << " (non-standard size)";
  }
  s << "\n";
  s << "Linedefs: " << doom_count_from_size(linedefs_size, linedef_stride);
  if (linedefs_size > 0 && (linedefs_size % linedef_stride) != 0) {
    s << " (non-standard size)";
  }
  s << "\n";
  s << "Sidedefs: " << doom_count_from_size(sidedefs_size, 30) << "\n";
  s << "Vertexes: " << doom_count_from_size(vertexes_size, 4) << "\n";
  s << "Sectors: " << doom_count_from_size(sectors_size, 26) << "\n";
  s << "BSP segs: " << doom_count_from_size(segs_size, 12) << "\n";
  s << "BSP subsectors: " << doom_count_from_size(ssectors_size, 4) << "\n";
  s << "BSP nodes: " << doom_count_from_size(nodes_size, 28) << "\n";
  if (has_gl_nodes) {
    s << "GL/extended nodes: present\n";
  }
  if (reject_size > 0) {
    s << "REJECT bytes: " << reject_size << "\n";
  }
  if (blockmap_size > 0) {
    s << "BLOCKMAP bytes: " << blockmap_size << "\n";
  }

  static const QStringList kOrder = {"THINGS",   "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",     "SSECTORS",
                                     "NODES",    "SECTORS",  "REJECT",   "BLOCKMAP", "BEHAVIOR", "SCRIPTS",
                                     "TEXTMAP",  "ZNODES",   "LEAFS",    "GL_VERT",  "GL_SEGS",  "GL_SSECT",
                                     "GL_NODES", "GL_PVS",   "GL_PORTALS"};
  s << "Lumps present:\n";
  for (const QString& lump : kOrder) {
    const auto it = lumps.constFind(lump);
    if (it == lumps.constEnd()) {
      continue;
    }
    s << "  " << format_doom_lump_line(it->name, it->size) << "\n";
  }

  return summary;
}

int find_doom_map_marker_index_for_lump(const QVector<ArchiveEntry>& entries, int selected_index) {
  if (selected_index < 0 || selected_index >= entries.size()) {
    return -1;
  }
  const QString selected = canonical_doom_lump_name(entries[selected_index].name);
  if (is_doom_map_marker_name(selected)) {
    return selected_index;
  }
  if (!is_doom_map_lump_name(selected)) {
    return -1;
  }
  for (int i = selected_index - 1; i >= 0; --i) {
    const QString n = canonical_doom_lump_name(entries[i].name);
    if (is_doom_map_marker_name(n)) {
      return i;
    }
  }
  return -1;
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

struct ReducedSelection {
  QStringList dirs;
  QStringList files;
};

ReducedSelection reduce_selected_items(const QVector<QPair<QString, bool>>& raw) {
  QSet<QString> dir_prefixes;
  QSet<QString> files;
  for (const auto& it : raw) {
    QString p = normalize_pak_path(it.first);
    if (p.isEmpty()) {
      continue;
    }
    if (it.second) {
      if (!p.endsWith('/')) {
        p += '/';
      }
      dir_prefixes.insert(p);
    } else {
      files.insert(p);
    }
  }

  QStringList dirs = dir_prefixes.values();
  std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.size() < b.size(); });

  QSet<QString> reduced_dirs_set;
  QStringList reduced_dirs;
  for (const QString& d : dirs) {
    bool covered = false;
    for (const QString& keep : reduced_dirs_set) {
      if (!keep.isEmpty() && d.startsWith(keep)) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      reduced_dirs_set.insert(d);
      reduced_dirs.push_back(d);
    }
  }

  QStringList reduced_files;
  for (const QString& f : files) {
    bool covered = false;
    for (const QString& d : reduced_dirs_set) {
      if (!d.isEmpty() && f.startsWith(d)) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      reduced_files.push_back(f);
    }
  }

  std::sort(reduced_dirs.begin(), reduced_dirs.end(), [](const QString& a, const QString& b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });
  std::sort(reduced_files.begin(), reduced_files.end(), [](const QString& a, const QString& b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });

  return ReducedSelection{reduced_dirs, reduced_files};
}

QString change_file_extension(const QString& path, const QString& new_ext) {
  const QFileInfo info(path);
  const QString base = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
  const QString ext = new_ext.startsWith('.') ? new_ext : ("." + new_ext);
  return QDir(info.absolutePath()).filePath(base + ext);
}

bool write_bytes_file(const QString& path, const QByteArray& bytes, QString* error) {
  const QFileInfo info(path);
  QDir dir(info.absolutePath());
  if (!dir.exists() && !dir.mkpath(".")) {
    if (error) {
      *error = QString("Unable to create output directory: %1").arg(info.absolutePath());
    }
    return false;
  }

  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to open output file: %1").arg(path);
    }
    return false;
  }
  if (out.write(bytes) != bytes.size()) {
    if (error) {
      *error = QString("Unable to write output file: %1").arg(path);
    }
    return false;
  }
  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize output file: %1").arg(path);
    }
    return false;
  }
  return true;
}

bool copy_directory_tree(const QString& source_dir, const QString& dest_dir, QString* error) {
  const QFileInfo src_info(source_dir);
  if (!src_info.exists() || !src_info.isDir()) {
    if (error) {
      *error = QString("Source directory does not exist: %1").arg(source_dir);
    }
    return false;
  }

  QDir dest(dest_dir);
  if (!dest.exists() && !dest.mkpath(".")) {
    if (error) {
      *error = QString("Unable to create destination directory: %1").arg(dest_dir);
    }
    return false;
  }

  const QDir source(source_dir);
  QDirIterator it(source_dir, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString abs_path = it.next();
    const QFileInfo info(abs_path);
    const QString rel = source.relativeFilePath(abs_path);
    const QString out_path = dest.filePath(rel);
    if (info.isDir()) {
      if (!QDir().mkpath(out_path)) {
        if (error) {
          *error = QString("Unable to create destination directory: %1").arg(out_path);
        }
        return false;
      }
      continue;
    }
    QString copy_err;
    if (!copy_file_stream(abs_path, out_path, &copy_err)) {
      if (error) {
        *error = copy_err;
      }
      return false;
    }
  }

  return true;
}

bool extract_archive_prefix_to_directory(const Archive& archive,
                                         const QString& prefix_in,
                                         const QString& dest_dir,
                                         QString* error,
                                         int* extracted_files = nullptr) {
  if (error) {
    *error = {};
  }
  if (extracted_files) {
    *extracted_files = 0;
  }

  QString prefix = normalize_pak_path(prefix_in);
  if (!prefix.isEmpty() && !prefix.endsWith('/')) {
    prefix += '/';
  }

  QDir dest(dest_dir);
  if (!dest.exists() && !dest.mkpath(".")) {
    if (error) {
      *error = QString("Unable to create output directory: %1").arg(dest_dir);
    }
    return false;
  }

  for (const ArchiveEntry& e : archive.entries()) {
    const QString name = normalize_pak_path(e.name);
    if (name.isEmpty()) {
      continue;
    }
    if (!prefix.isEmpty() && !name.startsWith(prefix)) {
      continue;
    }
    if (!is_safe_entry_name(name)) {
      continue;
    }

    const QString rel = prefix.isEmpty() ? name : name.mid(prefix.size());
    if (rel.isEmpty()) {
      continue;
    }

    if (rel.endsWith('/')) {
      dest.mkpath(rel);
      continue;
    }

    const QString out_path = dest.filePath(rel);
    QString ex_err;
    if (!archive.extract_entry_to_file(name, out_path, &ex_err)) {
      if (error) {
        *error = ex_err.isEmpty() ? QString("Unable to extract %1").arg(name) : ex_err;
      }
      return false;
    }
    if (extracted_files) {
      ++(*extracted_files);
    }
  }

  return true;
}

enum class ConversionCategory {
  Image = 0,
  Video,
  Archive,
  Model,
  Sound,
  Map,
  Text,
  Other,
};

QString conversion_category_name(ConversionCategory category) {
  switch (category) {
    case ConversionCategory::Image:
      return "Images";
    case ConversionCategory::Video:
      return "Videos";
    case ConversionCategory::Archive:
      return "Archives";
    case ConversionCategory::Model:
      return "Models";
    case ConversionCategory::Sound:
      return "Sound";
    case ConversionCategory::Map:
      return "Maps";
    case ConversionCategory::Text:
      return "Text";
    case ConversionCategory::Other:
    default:
      return "Other";
  }
}

QString conversion_category_folder_name(ConversionCategory category) {
  switch (category) {
    case ConversionCategory::Image:
      return "images";
    case ConversionCategory::Video:
      return "video";
    case ConversionCategory::Archive:
      return "archives";
    case ConversionCategory::Model:
      return "models";
    case ConversionCategory::Sound:
      return "sound";
    case ConversionCategory::Map:
      return "maps";
    case ConversionCategory::Text:
      return "text";
    case ConversionCategory::Other:
    default:
      return "other";
  }
}

ConversionCategory classify_conversion_category(const QString& file_name) {
  const QString ext = file_ext_lower(file_name);
  if (is_mountable_archive_ext(ext)) {
    return ConversionCategory::Archive;
  }
  if (is_video_file_name(file_name)) {
    return ConversionCategory::Video;
  }
  if (is_supported_audio_file(file_name)) {
    return ConversionCategory::Sound;
  }
  if (is_image_file_name(file_name) || is_sprite_file_name(file_name)) {
    return ConversionCategory::Image;
  }
  if (is_model_file_name(file_name)) {
    return ConversionCategory::Model;
  }
  if (is_bsp_file_name(file_name) || ext == "map") {
    return ConversionCategory::Map;
  }
  if (is_text_file_name(file_name)) {
    return ConversionCategory::Text;
  }
  return ConversionCategory::Other;
}

struct ConversionCategoryCounts {
  int image = 0;
  int video = 0;
  int archive = 0;
  int model = 0;
  int sound = 0;
  int map = 0;
  int text = 0;
  int other = 0;
};

struct BatchConversionOptions {
  QString output_dir;
  bool create_category_subdirs = true;
  bool preserve_selection_layout = true;

  bool process_images = true;
  QString image_format = "png";
  int image_quality = 90;

  bool process_videos = true;
  QString video_mode = "frames_png";
  int video_quality = 90;
  bool video_export_audio = true;

  bool process_archives = true;
  QString archive_mode = "extract";

  bool process_models = true;
  QString model_mode = "obj";

  bool process_sound = true;
  QString sound_mode = "wav";

  bool process_maps = true;
  QString map_mode = "preview";
  int map_preview_size = 1024;

  bool process_text = true;
  QString text_mode = "utf8";
  QString text_newlines = "preserve";

  bool copy_other = true;
};

class BatchConversionDialog final : public QDialog {
public:
  BatchConversionDialog(const ConversionCategoryCounts& counts,
                        const QString& default_output_dir,
                        QWidget* parent = nullptr)
      : QDialog(parent), counts_(counts) {
    setWindowTitle("Batch Asset Conversion");
    setMinimumWidth(840);
    setStyleSheet(
      "QFrame#batchGlobalCard, QFrame#batchCategoryCard {"
      " border: 1px solid palette(mid);"
      " border-radius: 8px;"
      " background-color: palette(base);"
      "}"
      "QFrame#batchCategoryCard QLabel[role=\"sectionTitle\"] {"
      " font-weight: 600;"
      " padding-bottom: 2px;"
      "}"
      "QTabWidget::pane { border: 0px; }"
    );

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    auto* title = new QLabel("Convert selected assets in batch with per-category settings.", this);
    QFont title_font = title->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 1);
    title->setFont(title_font);
    layout->addWidget(title);

    auto* global_card = make_card(this, "batchGlobalCard");
    auto* global_layout = new QVBoxLayout(global_card);
    global_layout->setContentsMargins(12, 10, 12, 10);
    global_layout->setSpacing(6);

    auto* out_row = new QWidget(global_card);
    auto* out_row_layout = new QHBoxLayout(out_row);
    out_row_layout->setContentsMargins(0, 0, 0, 0);
    out_row_layout->setSpacing(8);
    auto* out_label = new QLabel("Output folder:", out_row);
    out_label->setMinimumWidth(kOutputLabelMinWidth);
    out_row_layout->addWidget(out_label);
    output_edit_ = new QLineEdit(default_output_dir, out_row);
    output_edit_->setMinimumWidth(kFieldMinWidth + 120);
    out_row_layout->addWidget(output_edit_, 1);
    auto* browse = new QPushButton("Browse…", out_row);
    browse->setIcon(UiIcons::icon(UiIcons::Id::Browse, browse->style()));
    out_row_layout->addWidget(browse, 0);
    global_layout->addWidget(out_row);

    preserve_layout_check_ = new QCheckBox("Preserve selection directory layout", global_card);
    preserve_layout_check_->setChecked(true);
    global_layout->addWidget(preserve_layout_check_);

    category_folders_check_ = new QCheckBox("Create category subfolders (images, video, archives, ...)", global_card);
    category_folders_check_->setChecked(true);
    global_layout->addWidget(category_folders_check_);
    layout->addWidget(global_card);

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    layout->addWidget(tabs_, 1);

    add_image_tab();
    add_video_tab();
    add_archive_tab();
    add_model_tab();
    add_sound_tab();
    add_map_tab();
    add_text_tab();
    add_other_tab();

    connect(browse, &QPushButton::clicked, this, [this]() {
      QFileDialog dialog(this);
      dialog.setWindowTitle("Choose Output Folder");
      dialog.setFileMode(QFileDialog::Directory);
      dialog.setOption(QFileDialog::ShowDirsOnly, true);
      if (!output_edit_->text().trimmed().isEmpty()) {
        dialog.setDirectory(output_edit_->text().trimmed());
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
      output_edit_->setText(QDir::cleanPath(selected.first()));
    });

    if (image_format_) {
      connect(image_format_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_dynamic_visibility(); });
    }
    if (video_mode_) {
      connect(video_mode_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_dynamic_visibility(); });
    }
    if (map_mode_) {
      connect(map_mode_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_dynamic_visibility(); });
    }
    if (text_mode_) {
      connect(text_mode_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_dynamic_visibility(); });
    }
    refresh_dynamic_visibility();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) {
      ok->setIcon(UiIcons::icon(UiIcons::Id::Configure, ok->style()));
      ok->setText("Convert");
    }
    if (QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel)) {
      cancel->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, cancel->style()));
    }
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
      if (output_edit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Batch Conversion", "Choose an output folder.");
        return;
      }
      accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
  }

  BatchConversionOptions options() const {
    BatchConversionOptions out;
    out.output_dir = QDir::cleanPath(output_edit_ ? output_edit_->text().trimmed() : QString());
    out.preserve_selection_layout = preserve_layout_check_ && preserve_layout_check_->isChecked();
    out.create_category_subdirs = category_folders_check_ && category_folders_check_->isChecked();

    out.process_images = image_enabled_ && image_enabled_->isChecked();
    out.image_format = image_format_ ? image_format_->currentData().toString() : "png";
    out.image_quality = image_quality_ ? image_quality_->value() : 90;

    out.process_videos = video_enabled_ && video_enabled_->isChecked();
    out.video_mode = video_mode_ ? video_mode_->currentData().toString() : "frames_png";
    out.video_quality = video_quality_ ? video_quality_->value() : 90;
    out.video_export_audio = video_audio_ && video_audio_->isChecked();

    out.process_archives = archive_enabled_ && archive_enabled_->isChecked();
    out.archive_mode = archive_mode_ ? archive_mode_->currentData().toString() : "extract";

    out.process_models = model_enabled_ && model_enabled_->isChecked();
    out.model_mode = model_mode_ ? model_mode_->currentData().toString() : "obj";

    out.process_sound = sound_enabled_ && sound_enabled_->isChecked();
    out.sound_mode = sound_mode_ ? sound_mode_->currentData().toString() : "wav";

    out.process_maps = map_enabled_ && map_enabled_->isChecked();
    out.map_mode = map_mode_ ? map_mode_->currentData().toString() : "preview";
    out.map_preview_size = map_preview_size_ ? map_preview_size_->value() : 1024;

    out.process_text = text_enabled_ && text_enabled_->isChecked();
    out.text_mode = text_mode_ ? text_mode_->currentData().toString() : "utf8";
    out.text_newlines = text_newlines_ ? text_newlines_->currentData().toString() : "preserve";

    out.copy_other = other_copy_ && other_copy_->isChecked();
    return out;
  }

private:
  static constexpr int kOutputLabelMinWidth = 120;
  static constexpr int kFormLabelMinWidth = 124;
  static constexpr int kFieldMinWidth = 260;

  static QFrame* make_card(QWidget* parent, const char* object_name) {
    auto* card = new QFrame(parent);
    card->setObjectName(QString::fromLatin1(object_name));
    card->setFrameStyle(QFrame::NoFrame);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    return card;
  }

  static QLabel* make_form_label(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setMinimumWidth(kFormLabelMinWidth);
    return label;
  }

  static QFormLayout* make_form_layout() {
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
  }

  static void tune_field(QWidget* field) {
    if (!field) {
      return;
    }
    field->setMinimumWidth(kFieldMinWidth);
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  QWidget* make_category_tab(const QString& section_title, QVBoxLayout** card_layout_out) {
    if (!tabs_) {
      return nullptr;
    }

    auto* tab = new QWidget(tabs_);
    auto* tab_layout = new QVBoxLayout(tab);
    tab_layout->setContentsMargins(8, 8, 8, 6);
    tab_layout->setSpacing(0);

    auto* card = make_card(tab, "batchCategoryCard");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(12, 10, 12, 10);
    card_layout->setSpacing(8);

    if (!section_title.isEmpty()) {
      auto* title = new QLabel(section_title, card);
      title->setProperty("role", "sectionTitle");
      card_layout->addWidget(title);
    }

    tab_layout->addWidget(card, 0, Qt::AlignTop);
    tab_layout->addStretch(1);
    if (card_layout_out) {
      *card_layout_out = card_layout;
    }
    return tab;
  }

  int count_for_category(ConversionCategory category) const {
    switch (category) {
      case ConversionCategory::Image:
        return counts_.image;
      case ConversionCategory::Video:
        return counts_.video;
      case ConversionCategory::Archive:
        return counts_.archive;
      case ConversionCategory::Model:
        return counts_.model;
      case ConversionCategory::Sound:
        return counts_.sound;
      case ConversionCategory::Map:
        return counts_.map;
      case ConversionCategory::Text:
        return counts_.text;
      case ConversionCategory::Other:
      default:
        return counts_.other;
    }
  }

  bool apply_tab_meta(QWidget* tab, ConversionCategory category, QCheckBox* enabled) {
    if (!tabs_ || !tab || !enabled) {
      return false;
    }
    const int count = count_for_category(category);
    if (count <= 0) {
      enabled->setChecked(false);
      enabled->setEnabled(false);
      return false;
    }
    enabled->setChecked(true);
    enabled->setEnabled(true);
    const QString label = QString("%1 (%2)").arg(conversion_category_name(category)).arg(count);
    tabs_->addTab(tab, label);
    return true;
  }

  void set_row_visible(QLabel* label, QWidget* field, bool visible) const {
    if (label) {
      label->setVisible(visible);
    }
    if (field) {
      field->setVisible(visible);
    }
  }

  void refresh_dynamic_visibility() {
    const bool image_jpg = image_format_ && image_format_->currentData().toString() == "jpg";
    set_row_visible(image_quality_label_, image_quality_, image_jpg);

    const QString video_mode = video_mode_ ? video_mode_->currentData().toString() : QString();
    const bool video_jpg = video_mode == "frames_jpg";
    const bool video_frames = video_mode.startsWith("frames_");
    set_row_visible(video_quality_label_, video_quality_, video_jpg);
    if (video_audio_) {
      video_audio_->setVisible(video_frames);
    }

    const bool map_preview = map_mode_ && map_mode_->currentData().toString() == "preview";
    set_row_visible(map_preview_size_label_, map_preview_size_, map_preview);

    const bool text_utf8 = text_mode_ && text_mode_->currentData().toString() == "utf8";
    set_row_visible(text_newlines_label_, text_newlines_, text_utf8);
  }

  void add_image_tab() {
    if (count_for_category(ConversionCategory::Image) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Image conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    image_enabled_ = new QCheckBox("Process image assets", card);
    image_format_ = new QComboBox(card);
    image_format_->addItem("PNG", "png");
    image_format_->addItem("JPG", "jpg");
    image_format_->addItem("TGA", "tga");
    image_format_->addItem("BMP", "bmp");
    tune_field(image_format_);

    image_quality_ = new QSpinBox(card);
    image_quality_->setRange(1, 100);
    image_quality_->setValue(90);
    tune_field(image_quality_);

    card_layout->addWidget(image_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Output format", card), image_format_);
    image_quality_label_ = make_form_label("JPG quality", card);
    form->addRow(image_quality_label_, image_quality_);
    card_layout->addLayout(form);
    (void)apply_tab_meta(tab, ConversionCategory::Image, image_enabled_);
  }

  void add_video_tab() {
    if (count_for_category(ConversionCategory::Video) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Video conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    video_enabled_ = new QCheckBox("Process video assets", card);
    video_mode_ = new QComboBox(card);
    video_mode_->addItem("Frame sequence (PNG)", "frames_png");
    video_mode_->addItem("Frame sequence (JPG)", "frames_jpg");
    video_mode_->addItem("Copy source file", "copy");
    tune_field(video_mode_);

    video_quality_ = new QSpinBox(card);
    video_quality_->setRange(1, 100);
    video_quality_->setValue(90);
    tune_field(video_quality_);

    video_audio_ = new QCheckBox("Export cinematic audio as WAV when available", card);
    video_audio_->setChecked(true);

    card_layout->addWidget(video_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Conversion mode", card), video_mode_);
    video_quality_label_ = make_form_label("JPG quality", card);
    form->addRow(video_quality_label_, video_quality_);
    card_layout->addLayout(form);
    card_layout->addWidget(video_audio_);
    (void)apply_tab_meta(tab, ConversionCategory::Video, video_enabled_);
  }

  void add_archive_tab() {
    if (count_for_category(ConversionCategory::Archive) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Archive conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    archive_enabled_ = new QCheckBox("Process archive assets", card);
    archive_mode_ = new QComboBox(card);
    archive_mode_->addItem("Extract archive contents", "extract");
    archive_mode_->addItem("Copy source archive", "copy");
    tune_field(archive_mode_);

    card_layout->addWidget(archive_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Conversion mode", card), archive_mode_);
    card_layout->addLayout(form);
    (void)apply_tab_meta(tab, ConversionCategory::Archive, archive_enabled_);
  }

  void add_model_tab() {
    if (count_for_category(ConversionCategory::Model) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Model conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    model_enabled_ = new QCheckBox("Process model assets", card);
    model_mode_ = new QComboBox(card);
    model_mode_->addItem("Wavefront OBJ mesh", "obj");
    model_mode_->addItem("Model summary text", "summary");
    model_mode_->addItem("Copy source file", "copy");
    tune_field(model_mode_);

    card_layout->addWidget(model_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Conversion mode", card), model_mode_);
    card_layout->addLayout(form);
    (void)apply_tab_meta(tab, ConversionCategory::Model, model_enabled_);
  }

  void add_sound_tab() {
    if (count_for_category(ConversionCategory::Sound) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Sound conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    sound_enabled_ = new QCheckBox("Process sound assets", card);
    sound_mode_ = new QComboBox(card);
    sound_mode_->addItem("Convert to WAV where supported", "wav");
    sound_mode_->addItem("Copy source file", "copy");
    tune_field(sound_mode_);

    card_layout->addWidget(sound_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Conversion mode", card), sound_mode_);
    card_layout->addLayout(form);
    (void)apply_tab_meta(tab, ConversionCategory::Sound, sound_enabled_);
  }

  void add_map_tab() {
    if (count_for_category(ConversionCategory::Map) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Map conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    map_enabled_ = new QCheckBox("Process map assets", card);
    map_mode_ = new QComboBox(card);
    map_mode_->addItem("Render BSP preview image", "preview");
    map_mode_->addItem("Map summary text", "summary");
    map_mode_->addItem("Copy source file", "copy");
    tune_field(map_mode_);

    map_preview_size_ = new QSpinBox(card);
    map_preview_size_->setRange(256, 4096);
    map_preview_size_->setSingleStep(128);
    map_preview_size_->setValue(1024);
    tune_field(map_preview_size_);

    card_layout->addWidget(map_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Conversion mode", card), map_mode_);
    map_preview_size_label_ = make_form_label("Preview size", card);
    form->addRow(map_preview_size_label_, map_preview_size_);
    card_layout->addLayout(form);
    (void)apply_tab_meta(tab, ConversionCategory::Map, map_enabled_);
  }

  void add_text_tab() {
    if (count_for_category(ConversionCategory::Text) <= 0 || !tabs_) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Text conversion", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    text_enabled_ = new QCheckBox("Process text assets", card);
    text_mode_ = new QComboBox(card);
    text_mode_->addItem("Normalize to UTF-8 text", "utf8");
    text_mode_->addItem("Copy source file", "copy");
    tune_field(text_mode_);

    text_newlines_ = new QComboBox(card);
    text_newlines_->addItem("Preserve", "preserve");
    text_newlines_->addItem("LF", "lf");
    text_newlines_->addItem("CRLF", "crlf");
    tune_field(text_newlines_);

    card_layout->addWidget(text_enabled_);
    auto* form = make_form_layout();
    form->addRow(make_form_label("Conversion mode", card), text_mode_);
    text_newlines_label_ = make_form_label("Line endings", card);
    form->addRow(text_newlines_label_, text_newlines_);
    card_layout->addLayout(form);
    (void)apply_tab_meta(tab, ConversionCategory::Text, text_enabled_);
  }

  void add_other_tab() {
    const int count = count_for_category(ConversionCategory::Other);
    if (!tabs_ || count <= 0) {
      return;
    }

    QVBoxLayout* card_layout = nullptr;
    auto* tab = make_category_tab("Other assets", &card_layout);
    if (!tab || !card_layout) {
      return;
    }

    QFrame* card = qobject_cast<QFrame*>(card_layout->parentWidget());
    other_copy_ = new QCheckBox("Copy unsupported/other assets unchanged", card);
    other_copy_->setChecked(true);
    card_layout->addWidget(other_copy_);
    tabs_->addTab(tab, QString("Other (%1)").arg(count));
  }

  ConversionCategoryCounts counts_;

  QLineEdit* output_edit_ = nullptr;
  QCheckBox* preserve_layout_check_ = nullptr;
  QCheckBox* category_folders_check_ = nullptr;
  QTabWidget* tabs_ = nullptr;

  QCheckBox* image_enabled_ = nullptr;
  QComboBox* image_format_ = nullptr;
  QLabel* image_quality_label_ = nullptr;
  QSpinBox* image_quality_ = nullptr;

  QCheckBox* video_enabled_ = nullptr;
  QComboBox* video_mode_ = nullptr;
  QLabel* video_quality_label_ = nullptr;
  QSpinBox* video_quality_ = nullptr;
  QCheckBox* video_audio_ = nullptr;

  QCheckBox* archive_enabled_ = nullptr;
  QComboBox* archive_mode_ = nullptr;

  QCheckBox* model_enabled_ = nullptr;
  QComboBox* model_mode_ = nullptr;

  QCheckBox* sound_enabled_ = nullptr;
  QComboBox* sound_mode_ = nullptr;

  QCheckBox* map_enabled_ = nullptr;
  QComboBox* map_mode_ = nullptr;
  QLabel* map_preview_size_label_ = nullptr;
  QSpinBox* map_preview_size_ = nullptr;

  QCheckBox* text_enabled_ = nullptr;
  QComboBox* text_mode_ = nullptr;
  QLabel* text_newlines_label_ = nullptr;
  QComboBox* text_newlines_ = nullptr;

  QCheckBox* other_copy_ = nullptr;
};

QByteArray normalize_text_bytes(const QByteArray& in, const QString& newline_mode) {
  QString text = QString::fromUtf8(in);
  if (text.contains(QChar::ReplacementCharacter)) {
    text = QString::fromLatin1(in);
  }
  if (newline_mode == "lf") {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
  } else if (newline_mode == "crlf") {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace("\n", "\r\n");
  }
  return text.toUtf8();
}

QByteArray pcm_to_wav_bytes(const QByteArray& pcm, const CinematicInfo& info) {
  const int channels = std::max(1, info.audio_channels);
  const int sample_rate = std::max(1, info.audio_sample_rate);
  const int bytes_per_sample = std::clamp(info.audio_bytes_per_sample, 1, 2);
  QByteArray data = pcm;

  if (bytes_per_sample == 1 && info.audio_signed) {
    for (char& c : data) {
      const int s = static_cast<signed char>(c);
      c = static_cast<char>(std::clamp(s + 128, 0, 255));
    }
  }

  QByteArray out;
  auto append_u16 = [&out](quint16 v) {
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
  };
  auto append_u32 = [&out](quint32 v) {
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 24) & 0xFF));
  };

  const quint32 data_size = static_cast<quint32>(data.size());
  const quint16 bits_per_sample = static_cast<quint16>(bytes_per_sample * 8);
  const quint32 byte_rate = static_cast<quint32>(sample_rate * channels * bytes_per_sample);
  const quint16 block_align = static_cast<quint16>(channels * bytes_per_sample);
  const quint32 riff_size = 36u + data_size;

  out.reserve(static_cast<int>(riff_size + 8u));
  out.append("RIFF", 4);
  append_u32(riff_size);
  out.append("WAVE", 4);
  out.append("fmt ", 4);
  append_u32(16);
  append_u16(1);
  append_u16(static_cast<quint16>(channels));
  append_u32(static_cast<quint32>(sample_rate));
  append_u32(byte_rate);
  append_u16(block_align);
  append_u16(bits_per_sample);
  out.append("data", 4);
  append_u32(data_size);
  out.append(data);
  return out;
}

bool write_model_obj(const LoadedModel& model, const QString& out_path, QString* error) {
  const QFileInfo info(out_path);
  QDir dir(info.absolutePath());
  if (!dir.exists() && !dir.mkpath(".")) {
    if (error) {
      *error = QString("Unable to create output directory: %1").arg(info.absolutePath());
    }
    return false;
  }

  QSaveFile out(out_path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error) {
      *error = QString("Unable to write OBJ file: %1").arg(out_path);
    }
    return false;
  }

  QTextStream s(&out);
  s << "# PakFu generated OBJ\n";
  s << "# format: " << model.format << "\n";
  for (const ModelVertex& v : model.mesh.vertices) {
    s << "v " << v.px << " " << v.py << " " << v.pz << "\n";
  }
  for (const ModelVertex& v : model.mesh.vertices) {
    s << "vt " << v.u << " " << (1.0f - v.v) << "\n";
  }
  for (const ModelVertex& v : model.mesh.vertices) {
    s << "vn " << v.nx << " " << v.ny << " " << v.nz << "\n";
  }

  const int tri_count = model.mesh.indices.size() / 3;
  for (int tri = 0; tri < tri_count; ++tri) {
    const int base = tri * 3;
    const int i0 = static_cast<int>(model.mesh.indices[base + 0]) + 1;
    const int i1 = static_cast<int>(model.mesh.indices[base + 1]) + 1;
    const int i2 = static_cast<int>(model.mesh.indices[base + 2]) + 1;
    s << "f "
      << i0 << "/" << i0 << "/" << i0 << " "
      << i1 << "/" << i1 << "/" << i1 << " "
      << i2 << "/" << i2 << "/" << i2 << "\n";
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize OBJ file: %1").arg(out_path);
    }
    return false;
  }
  return true;
}

QString model_summary_text(const LoadedModel& model) {
  QString text;
  QTextStream s(&text);
  s << "Format: " << model.format << "\n";
  s << "Frames: " << model.frame_count << "\n";
  s << "Surface count: " << model.surface_count << "\n";
  s << "Vertices: " << model.mesh.vertices.size() << "\n";
  s << "Triangles: " << (model.mesh.indices.size() / 3) << "\n";
  s << "Bounds min: " << model.mesh.mins.x() << ", " << model.mesh.mins.y() << ", " << model.mesh.mins.z() << "\n";
  s << "Bounds max: " << model.mesh.maxs.x() << ", " << model.mesh.maxs.y() << ", " << model.mesh.maxs.z() << "\n";
  if (!model.surfaces.isEmpty()) {
    s << "Surfaces:\n";
    for (const ModelSurface& surface : model.surfaces) {
      s << "  - " << (surface.name.isEmpty() ? "<unnamed>" : surface.name)
        << " shader=" << (surface.shader.isEmpty() ? "<none>" : surface.shader)
        << " indices=" << surface.index_count << "\n";
    }
  }
  return text;
}

QString bsp_summary_text(const QByteArray& bytes, const QString& file_name) {
  QString text;
  QTextStream s(&text);

  QString version_err;
  const int version = bsp_version_bytes(bytes, &version_err);
  QString family_err;
  const BspFamily family = bsp_family_bytes(bytes, &family_err);

  s << "File: " << file_name << "\n";
  if (version >= 0) {
    s << "Version: " << version << "\n";
  } else if (!version_err.isEmpty()) {
    s << "Version: " << version_err << "\n";
  }

  switch (family) {
    case BspFamily::Quake1:
      s << "Family: Quake 1\n";
      break;
    case BspFamily::Quake2:
      s << "Family: Quake 2\n";
      break;
    case BspFamily::Quake3:
      s << "Family: Quake 3\n";
      break;
    case BspFamily::Unknown:
    default:
      s << "Family: Unknown\n";
      if (!family_err.isEmpty()) {
        s << "Family note: " << family_err << "\n";
      }
      break;
  }

  BspMesh mesh;
  QString mesh_err;
  if (load_bsp_mesh_bytes(bytes, file_name, &mesh, &mesh_err, false)) {
    s << "Vertices: " << mesh.vertices.size() << "\n";
    s << "Triangles: " << (mesh.indices.size() / 3) << "\n";
    s << "Surfaces: " << mesh.surfaces.size() << "\n";
    s << "Bounds min: " << mesh.mins.x() << ", " << mesh.mins.y() << ", " << mesh.mins.z() << "\n";
    s << "Bounds max: " << mesh.maxs.x() << ", " << mesh.maxs.y() << ", " << mesh.maxs.z() << "\n";
  } else if (!mesh_err.isEmpty()) {
    s << "Mesh parse: " << mesh_err << "\n";
  }

  return text;
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
    setSupportedDragActions(Qt::CopyAction | Qt::MoveAction);
  }

protected:
  Qt::DropActions supportedDropActions() const override {
    return Qt::CopyAction | Qt::MoveAction;
  }

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
    if (event && tab_ && tab_->can_accept_mime(event->mimeData())) {
      event->acceptProposedAction();
      return;
    }
    QTreeWidget::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    if (event && tab_ && tab_->can_accept_mime(event->mimeData())) {
      event->acceptProposedAction();
      return;
    }
    QTreeWidget::dragMoveEvent(event);
  }

  void dropEvent(QDropEvent* event) override {
    if (!event || !tab_) {
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

    if (tab_->handle_drop_event(event, dest_prefix)) {
      return;
    }
    QTreeWidget::dropEvent(event);
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
    setSupportedDragActions(Qt::CopyAction | Qt::MoveAction);
  }

protected:
  Qt::DropActions supportedDropActions() const override {
    return Qt::CopyAction | Qt::MoveAction;
  }

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
    if (event && tab_ && tab_->can_accept_mime(event->mimeData())) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent* event) override {
    if (event && tab_ && tab_->can_accept_mime(event->mimeData())) {
      event->acceptProposedAction();
      return;
    }
    QListWidget::dragMoveEvent(event);
  }

  void dropEvent(QDropEvent* event) override {
    if (!event || !tab_) {
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

    if (tab_->handle_drop_event(event, dest_prefix)) {
      return;
    }
    QListWidget::dropEvent(event);
  }

private:
  PakTab* tab_ = nullptr;
};

PakTab::PakTab(Mode mode, const QString& pak_path, QWidget* parent)
    : QWidget(parent), mode_(mode), pak_path_(pak_path) {
  setAcceptDrops(true);
  drag_source_uid_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  thumbnail_pool_.setMaxThreadCount(1);
  sprite_icon_timer_ = new QTimer(this);
  sprite_icon_timer_->setInterval(60);
  connect(sprite_icon_timer_, &QTimer::timeout, this, &PakTab::advance_sprite_icon_animations);
  QSettings settings;
  image_texture_smoothing_ = settings.value("preview/image/textureSmoothing", false).toBool();
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

void PakTab::set_model_texture_smoothing(bool enabled) {
  if (preview_) {
    preview_->set_model_texture_smoothing(enabled);
  }
}

void PakTab::set_image_texture_smoothing(bool enabled) {
  image_texture_smoothing_ = enabled;
  if (preview_) {
    preview_->set_image_texture_smoothing(enabled);
  }
  // Regenerate thumbnails with the new setting.
  refresh_listing();
}

void PakTab::set_preview_renderer(PreviewRenderer renderer) {
  if (preview_) {
    preview_->set_preview_renderer(renderer);
  }
}

void PakTab::set_3d_fov_degrees(int degrees) {
  if (preview_) {
    preview_->set_3d_fov_degrees(degrees);
  }
}

void PakTab::set_game_id(GameId id) {
  if (game_id_ == id) {
    return;
  }
  game_id_ = id;
  if (preview_) {
    preview_->set_glow_enabled(is_quake2_game(game_id_));
  }
  if (loaded_) {
    update_preview();
  }
}

void PakTab::set_pure_pak_protector(bool enabled, bool is_official) {
  pure_pak_protector_enabled_ = enabled;
  official_archive_ = is_official;
  refresh_listing();
}

bool PakTab::is_editable() const {
  if (!loaded_) {
    return false;
  }
  if (archive_.is_loaded() && archive_.format() == Archive::Format::Directory) {
    return false;
  }
  if (is_wad_mounted()) {
    return false;
  }
  if (pure_pak_protector_enabled_ && official_archive_) {
    return false;
  }
  return true;
}

bool PakTab::is_pure_protected() const {
  return pure_pak_protector_enabled_ && official_archive_;
}

bool PakTab::can_extract_all() const {
  if (!loaded_ || !view_archive().is_loaded()) {
    return false;
  }
  const Archive::Format fmt = view_archive().format();
  return fmt != Archive::Format::Unknown && fmt != Archive::Format::Directory;
}

void PakTab::cut() {
  copy_selected(true);
}

void PakTab::copy() {
  if (try_copy_shader_selection_to_clipboard()) {
    return;
  }
  copy_selected(false);
}

void PakTab::paste() {
  if (try_paste_shader_blocks_from_clipboard()) {
    return;
  }
  paste_from_clipboard();
}

void PakTab::rename() {
  rename_selected();
}

void PakTab::extract_selected() {
  if (!loaded_) {
    return;
  }

  const QVector<QPair<QString, bool>> raw = selected_items();
  if (raw.isEmpty()) {
    QMessageBox::information(this, "Extract Selected", "Select one or more files or folders first.");
    return;
  }
  const ReducedSelection selection = reduce_selected_items(raw);
  if (selection.dirs.isEmpty() && selection.files.isEmpty()) {
    QMessageBox::information(this, "Extract Selected", "No extractable items are selected.");
    return;
  }

  QFileDialog dialog(this);
  dialog.setWindowTitle("Extract Selected To");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  const QString base_dir = !default_directory_.isEmpty()
    ? default_directory_
    : (!pak_path_.isEmpty() ? QFileInfo(pak_path_).absolutePath() : QDir::homePath());
  dialog.setDirectory(base_dir);
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

  const QString out_dir = QDir::cleanPath(selected.first());
  if (out_dir.isEmpty()) {
    return;
  }

  QDir out(out_dir);
  if (!out.exists() && !out.mkpath(".")) {
    QMessageBox::warning(this, "Extract Selected", QString("Unable to create output folder:\n%1").arg(out_dir));
    return;
  }

  int extracted_files = 0;
  int extracted_dirs = 0;
  QStringList failures;

  const bool mounted = is_wad_mounted();

  for (const QString& dir_prefix : selection.dirs) {
    const QString leaf = pak_leaf_name(dir_prefix);
    if (leaf.isEmpty()) {
      continue;
    }
    const QString dest_dir = out.filePath(leaf);
    QString err;
    if (mounted) {
      int count = 0;
      if (!extract_archive_prefix_to_directory(view_archive(), dir_prefix, dest_dir, &err, &count)) {
        failures.push_back(err.isEmpty() ? QString("Unable to extract folder: %1").arg(dir_prefix) : err);
        continue;
      }
      ++extracted_dirs;
      extracted_files += count;
      continue;
    }

    QString exported_dir;
    if (!export_path_to_temp(dir_prefix, true, &exported_dir, &err)) {
      failures.push_back(err.isEmpty() ? QString("Unable to extract folder: %1").arg(dir_prefix) : err);
      continue;
    }
    if (!copy_directory_tree(exported_dir, dest_dir, &err)) {
      failures.push_back(err.isEmpty() ? QString("Unable to write folder: %1").arg(dest_dir) : err);
      continue;
    }
    ++extracted_dirs;

    int copied = 0;
    QDirIterator count_it(dest_dir, QDir::Files, QDirIterator::Subdirectories);
    while (count_it.hasNext()) {
      count_it.next();
      ++copied;
    }
    extracted_files += copied;
  }

  for (const QString& pak_path : selection.files) {
    const QString leaf = pak_leaf_name(pak_path);
    if (leaf.isEmpty()) {
      continue;
    }
    const QString dest_file = out.filePath(leaf);
    QString exported_path;
    QString err;
    if (!export_path_to_temp(pak_path, false, &exported_path, &err)) {
      failures.push_back(err.isEmpty() ? QString("Unable to extract file: %1").arg(pak_path) : err);
      continue;
    }
    if (!copy_file_stream(exported_path, dest_file, &err)) {
      failures.push_back(err.isEmpty() ? QString("Unable to write file: %1").arg(dest_file) : err);
      continue;
    }
    ++extracted_files;
  }

  QString summary = QString("Extracted %1 file(s)").arg(extracted_files);
  if (extracted_dirs > 0) {
    summary += QString(" from %1 folder(s)").arg(extracted_dirs);
  }
  summary += QString("\nOutput: %1").arg(out_dir);

  if (!failures.isEmpty()) {
    summary += QString("\n\nFailed: %1 item(s)").arg(failures.size());
    summary += "\n";
    summary += failures.mid(0, 12).join("\n");
    QMessageBox::warning(this, "Extract Selected", summary);
    return;
  }
  QMessageBox::information(this, "Extract Selected", summary);
}

void PakTab::extract_all() {
  if (!can_extract_all()) {
    QMessageBox::information(this, "Extract All", "Extract All is available only when viewing an archive.");
    return;
  }

  QFileDialog dialog(this);
  dialog.setWindowTitle("Extract Archive To");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  const QString base_dir = !default_directory_.isEmpty()
    ? default_directory_
    : (!pak_path_.isEmpty() ? QFileInfo(pak_path_).absolutePath() : QDir::homePath());
  dialog.setDirectory(base_dir);
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

  const QString out_dir = QDir::cleanPath(selected.first());
  if (out_dir.isEmpty()) {
    return;
  }

  QDir out(out_dir);
  if (!out.exists() && !out.mkpath(".")) {
    QMessageBox::warning(this, "Extract All", QString("Unable to create output folder:\n%1").arg(out_dir));
    return;
  }

  int extracted_files = 0;
  QString err;

  if (is_wad_mounted()) {
    if (!extract_archive_prefix_to_directory(view_archive(), QString(), out_dir, &err, &extracted_files)) {
      QMessageBox::warning(this, "Extract All", err.isEmpty() ? "Unable to extract archive." : err);
      return;
    }
  } else {
    int expected_files = 0;
    if (archive_.is_loaded()) {
      for (const ArchiveEntry& e : archive_.entries()) {
        const QString name = normalize_pak_path(e.name);
        if (name.isEmpty() || name.endsWith('/')) {
          continue;
        }
        if (is_deleted_path(name)) {
          continue;
        }
        if (added_index_by_name_.contains(name)) {
          continue;
        }
        ++expected_files;
      }
    }
    for (const AddedFile& f : added_files_) {
      const QString name = normalize_pak_path(f.pak_name);
      if (name.isEmpty() || name.endsWith('/') || is_deleted_path(name)) {
        continue;
      }
      ++expected_files;
    }

    if (!export_dir_prefix_to_fs(QString(), out_dir, &err)) {
      QMessageBox::warning(this, "Extract All", err.isEmpty() ? "Unable to extract archive." : err);
      return;
    }
    extracted_files = expected_files;
  }

  QMessageBox::information(this,
                           "Extract All",
                           QString("Extracted %1 file(s)\nOutput: %2").arg(extracted_files).arg(out_dir));
}

void PakTab::convert_selected_assets() {
  if (!loaded_) {
    return;
  }

  struct PendingAsset {
    QString display_name;
    QString pak_path;
    QString relative_path;
    QString source_fs_path;
    ConversionCategory category = ConversionCategory::Other;
  };

  QVector<PendingAsset> assets;
  QStringList gather_failures;

  const QVector<QPair<QString, bool>> raw = selected_items();
  if (raw.isEmpty()) {
    QMessageBox::information(this, "Batch Conversion", "Select one or more files or folders first.");
    return;
  }
  const ReducedSelection selection = reduce_selected_items(raw);
  const bool mounted = is_wad_mounted();

  auto add_asset = [&assets](PendingAsset item) {
    if (item.relative_path.isEmpty()) {
      return;
    }
    item.relative_path = QDir::fromNativeSeparators(item.relative_path);
    while (item.relative_path.startsWith('/')) {
      item.relative_path.remove(0, 1);
    }
    if (item.relative_path.isEmpty()) {
      return;
    }
    assets.push_back(std::move(item));
  };

  for (const QString& pak_path : selection.files) {
    const QString leaf = pak_leaf_name(pak_path);
    if (leaf.isEmpty()) {
      continue;
    }
    PendingAsset item;
    item.display_name = leaf;
    item.pak_path = pak_path;
    item.relative_path = leaf;
    item.category = classify_conversion_category(leaf);
    add_asset(std::move(item));
  }

  for (const QString& dir_prefix_in : selection.dirs) {
    QString dir_prefix = normalize_pak_path(dir_prefix_in);
    if (!dir_prefix.endsWith('/')) {
      dir_prefix += '/';
    }
    const QString dir_leaf = pak_leaf_name(dir_prefix);
    if (dir_leaf.isEmpty()) {
      continue;
    }

    if (mounted) {
      for (const ArchiveEntry& e : view_archive().entries()) {
        const QString name = normalize_pak_path(e.name);
        if (name.isEmpty() || name.endsWith('/')) {
          continue;
        }
        if (!name.startsWith(dir_prefix)) {
          continue;
        }
        const QString rel = name.mid(dir_prefix.size());
        if (rel.isEmpty()) {
          continue;
        }
        PendingAsset item;
        item.display_name = pak_leaf_name(name);
        item.pak_path = name;
        item.relative_path = dir_leaf + "/" + rel;
        item.category = classify_conversion_category(name);
        add_asset(std::move(item));
      }
      continue;
    }

    QString exported_dir;
    QString err;
    if (!export_path_to_temp(dir_prefix, true, &exported_dir, &err)) {
      gather_failures.push_back(err.isEmpty() ? QString("Unable to prepare folder for conversion: %1").arg(dir_prefix) : err);
      continue;
    }
    const QDir root(exported_dir);
    QDirIterator it(exported_dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString abs_file = it.next();
      const QString rel = root.relativeFilePath(abs_file);
      if (rel.isEmpty()) {
        continue;
      }
      PendingAsset item;
      item.display_name = QFileInfo(abs_file).fileName();
      item.pak_path = QString();
      item.source_fs_path = abs_file;
      item.relative_path = dir_leaf + "/" + QDir::fromNativeSeparators(rel);
      item.category = classify_conversion_category(item.display_name);
      add_asset(std::move(item));
    }
  }

  if (assets.isEmpty()) {
    QString msg = "No files were resolved from the current selection.";
    if (!gather_failures.isEmpty()) {
      msg += "\n\n" + gather_failures.mid(0, 8).join("\n");
    }
    QMessageBox::information(this, "Batch Conversion", msg);
    return;
  }

  ConversionCategoryCounts counts;
  for (const PendingAsset& item : assets) {
    switch (item.category) {
      case ConversionCategory::Image:
        ++counts.image;
        break;
      case ConversionCategory::Video:
        ++counts.video;
        break;
      case ConversionCategory::Archive:
        ++counts.archive;
        break;
      case ConversionCategory::Model:
        ++counts.model;
        break;
      case ConversionCategory::Sound:
        ++counts.sound;
        break;
      case ConversionCategory::Map:
        ++counts.map;
        break;
      case ConversionCategory::Text:
        ++counts.text;
        break;
      case ConversionCategory::Other:
      default:
        ++counts.other;
        break;
    }
  }

  const QString default_out = !default_directory_.isEmpty()
    ? default_directory_
    : (!pak_path_.isEmpty() ? QFileInfo(pak_path_).absolutePath() : QDir::homePath());

  BatchConversionDialog dialog(counts, default_out, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const BatchConversionOptions options = dialog.options();
  if (options.output_dir.isEmpty()) {
    return;
  }

  QDir out_root(options.output_dir);
  if (!out_root.exists() && !out_root.mkpath(".")) {
    QMessageBox::warning(this, "Batch Conversion", QString("Unable to create output folder:\n%1").arg(options.output_dir));
    return;
  }

  auto is_category_enabled = [&options](ConversionCategory category) -> bool {
    switch (category) {
      case ConversionCategory::Image:
        return options.process_images;
      case ConversionCategory::Video:
        return options.process_videos;
      case ConversionCategory::Archive:
        return options.process_archives;
      case ConversionCategory::Model:
        return options.process_models;
      case ConversionCategory::Sound:
        return options.process_sound;
      case ConversionCategory::Map:
        return options.process_maps;
      case ConversionCategory::Text:
        return options.process_text;
      case ConversionCategory::Other:
      default:
        return options.copy_other;
    }
  };

  auto output_path_for = [&options](const PendingAsset& item) -> QString {
    QDir base(options.output_dir);
    if (options.create_category_subdirs) {
      base = QDir(base.filePath(conversion_category_folder_name(item.category)));
    }
    const QString rel = options.preserve_selection_layout
      ? item.relative_path
      : QFileInfo(item.relative_path).fileName();
    return base.filePath(rel);
  };

  QProgressDialog progress("Converting assets...", "Cancel", 0, assets.size(), this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(200);

  int converted_ok = 0;
  int skipped = 0;
  QStringList failures = gather_failures;

  for (int i = 0; i < assets.size(); ++i) {
    if (progress.wasCanceled()) {
      break;
    }

    const PendingAsset& item = assets[i];
    progress.setValue(i);
    progress.setLabelText(QString("Converting %1 (%2/%3)...").arg(item.display_name).arg(i + 1).arg(assets.size()));
    if ((i % 2) == 0) {
      QCoreApplication::processEvents();
    }

    if (!is_category_enabled(item.category)) {
      ++skipped;
      continue;
    }

    QString source_path = item.source_fs_path;
    QString err;
    if (source_path.isEmpty()) {
      if (!export_path_to_temp(item.pak_path, false, &source_path, &err)) {
        failures.push_back(err.isEmpty() ? QString("Unable to export source file: %1").arg(item.display_name) : err);
        continue;
      }
    }

    QFile source_file(source_path);
    if (!source_file.open(QIODevice::ReadOnly)) {
      failures.push_back(QString("Unable to read source file: %1").arg(source_path));
      continue;
    }
    const QByteArray source_bytes = source_file.readAll();
    source_file.close();

    QString target_path = output_path_for(item);
    bool ok = false;

    if (item.category == ConversionCategory::Image) {
      ImageDecodeOptions decode_opts;
      const QString ext = file_ext_lower(item.display_name);
      if (ext == "wal") {
        QString pal_err;
        if (!ensure_quake2_palette(&pal_err) || quake2_palette_.size() != 256) {
          failures.push_back(pal_err.isEmpty() ? QString("Missing Quake II palette for WAL conversion: %1").arg(item.display_name) : pal_err);
          continue;
        }
        decode_opts.palette = &quake2_palette_;
      } else if (ext == "mip") {
        QString pal_err;
        if (ensure_quake1_palette(&pal_err) && quake1_palette_.size() == 256) {
          decode_opts.palette = &quake1_palette_;
        }
      }
      const ImageDecodeResult decoded = decode_image_bytes(source_bytes, item.display_name, decode_opts);
      if (!decoded.ok()) {
        failures.push_back(decoded.error.isEmpty() ? QString("Unable to decode image: %1").arg(item.display_name) : decoded.error);
        continue;
      }
      target_path = change_file_extension(target_path, options.image_format);
      const QByteArray fmt = options.image_format.toUpper().toLatin1();
      const int quality = (options.image_format == "jpg") ? options.image_quality : -1;
      const QFileInfo info(target_path);
      if (!QDir(info.absolutePath()).exists() && !QDir().mkpath(info.absolutePath())) {
        failures.push_back(QString("Unable to create output directory: %1").arg(info.absolutePath()));
        continue;
      }
      ok = decoded.image.save(target_path, fmt.constData(), quality);
      if (!ok) {
        failures.push_back(QString("Unable to save converted image: %1").arg(target_path));
      }
    } else if (item.category == ConversionCategory::Sound) {
      if (options.sound_mode == "copy") {
        ok = copy_file_stream(source_path, target_path, &err);
      } else {
        const QString ext = file_ext_lower(item.display_name);
        if (ext == "idwav") {
          const IdWavDecodeResult decoded = decode_idwav_to_wav_bytes(source_bytes);
          if (!decoded.ok()) {
            failures.push_back(decoded.error.isEmpty() ? QString("Unable to convert IDWAV: %1").arg(item.display_name) : decoded.error);
            continue;
          }
          target_path = change_file_extension(target_path, "wav");
          ok = write_bytes_file(target_path, decoded.wav_bytes, &err);
        } else if (ext == "wav") {
          target_path = change_file_extension(target_path, "wav");
          ok = write_bytes_file(target_path, source_bytes, &err);
        } else {
          failures.push_back(QString("Unsupported sound conversion for %1 (use Copy mode for this format).").arg(item.display_name));
          continue;
        }
      }
      if (!ok) {
        failures.push_back(err.isEmpty() ? QString("Unable to convert sound: %1").arg(item.display_name) : err);
      }
    } else if (item.category == ConversionCategory::Video) {
      if (options.video_mode == "copy") {
        ok = copy_file_stream(source_path, target_path, &err);
        if (!ok) {
          failures.push_back(err.isEmpty() ? QString("Unable to copy video: %1").arg(item.display_name) : err);
        }
      } else {
        QString open_err;
        std::unique_ptr<CinematicDecoder> decoder = open_cinematic_file(source_path, &open_err);
        if (!decoder) {
          failures.push_back(open_err.isEmpty()
                               ? QString("Only CIN/ROQ frame export is supported for %1.").arg(item.display_name)
                               : open_err);
          continue;
        }

        const QFileInfo target_info(target_path);
        const QString frame_root = QDir(target_info.absolutePath()).filePath(target_info.completeBaseName() + "_frames");
        if (!QDir().mkpath(frame_root)) {
          failures.push_back(QString("Unable to create frame output folder: %1").arg(frame_root));
          continue;
        }

        QByteArray pcm_audio;
        int frame_index = 0;
        CinematicFrame frame;
        QString decode_err;
        while (decoder->decode_next(&frame, &decode_err)) {
          const QString image_ext = (options.video_mode == "frames_jpg") ? "jpg" : "png";
          const QString frame_name = QString("frame_%1.%2").arg(frame_index, 6, 10, QLatin1Char('0')).arg(image_ext);
          const QString frame_path = QDir(frame_root).filePath(frame_name);
          const QByteArray fmt = image_ext.toUpper().toLatin1();
          const int quality = (image_ext == "jpg") ? options.video_quality : -1;
          if (!frame.image.save(frame_path, fmt.constData(), quality)) {
            failures.push_back(QString("Unable to write video frame: %1").arg(frame_path));
            break;
          }
          if (options.video_export_audio && !frame.audio_pcm.isEmpty()) {
            pcm_audio += frame.audio_pcm;
          }
          ++frame_index;
        }

        if (frame_index <= 0) {
          failures.push_back(decode_err.isEmpty()
                               ? QString("No frames were decoded from: %1").arg(item.display_name)
                               : decode_err);
          continue;
        }

        if (options.video_export_audio && !pcm_audio.isEmpty()) {
          const QByteArray wav = pcm_to_wav_bytes(pcm_audio, decoder->info());
          const QString audio_path = QDir(frame_root).filePath("audio.wav");
          if (!write_bytes_file(audio_path, wav, &err)) {
            failures.push_back(err.isEmpty() ? QString("Unable to write cinematic audio: %1").arg(audio_path) : err);
          }
        }
        ok = true;
      }
    } else if (item.category == ConversionCategory::Archive) {
      if (options.archive_mode == "copy") {
        ok = copy_file_stream(source_path, target_path, &err);
        if (!ok) {
          failures.push_back(err.isEmpty() ? QString("Unable to copy archive: %1").arg(item.display_name) : err);
        }
      } else {
        Archive nested;
        QString load_err;
        if (!nested.load(source_path, &load_err) || !nested.is_loaded()) {
          failures.push_back(load_err.isEmpty() ? QString("Unable to open nested archive: %1").arg(item.display_name) : load_err);
          continue;
        }
        const QFileInfo info(target_path);
        const QString unpack_dir = QDir(info.absolutePath()).filePath(info.completeBaseName());
        int extracted = 0;
        if (!extract_archive_prefix_to_directory(nested, QString(), unpack_dir, &err, &extracted)) {
          failures.push_back(err.isEmpty() ? QString("Unable to extract nested archive: %1").arg(item.display_name) : err);
          continue;
        }
        ok = true;
      }
    } else if (item.category == ConversionCategory::Model) {
      if (options.model_mode == "copy") {
        ok = copy_file_stream(source_path, target_path, &err);
      } else {
        QString load_err;
        const std::optional<LoadedModel> model = load_model_file(source_path, &load_err);
        if (!model.has_value()) {
          failures.push_back(load_err.isEmpty() ? QString("Unable to decode model: %1").arg(item.display_name) : load_err);
          continue;
        }
        if (options.model_mode == "obj") {
          target_path = change_file_extension(target_path, "obj");
          ok = write_model_obj(*model, target_path, &err);
        } else {
          target_path = change_file_extension(target_path, "txt");
          ok = write_bytes_file(target_path, model_summary_text(*model).toUtf8(), &err);
        }
      }
      if (!ok) {
        failures.push_back(err.isEmpty() ? QString("Unable to convert model: %1").arg(item.display_name) : err);
      }
    } else if (item.category == ConversionCategory::Map) {
      if (options.map_mode == "copy") {
        ok = copy_file_stream(source_path, target_path, &err);
      } else if (options.map_mode == "preview") {
        const BspPreviewResult preview = render_bsp_preview_bytes(source_bytes, item.display_name, BspPreviewStyle::Lightmapped, options.map_preview_size);
        if (!preview.ok()) {
          failures.push_back(preview.error.isEmpty() ? QString("Unable to render map preview: %1").arg(item.display_name) : preview.error);
          continue;
        }
        target_path = change_file_extension(target_path, "png");
        const QFileInfo info(target_path);
        if (!QDir(info.absolutePath()).exists() && !QDir().mkpath(info.absolutePath())) {
          failures.push_back(QString("Unable to create output directory: %1").arg(info.absolutePath()));
          continue;
        }
        ok = preview.image.save(target_path, "PNG");
      } else {
        target_path = change_file_extension(target_path, "txt");
        ok = write_bytes_file(target_path, bsp_summary_text(source_bytes, item.display_name).toUtf8(), &err);
      }
      if (!ok) {
        failures.push_back(err.isEmpty() ? QString("Unable to convert map: %1").arg(item.display_name) : err);
      }
    } else if (item.category == ConversionCategory::Text) {
      if (options.text_mode == "copy") {
        ok = copy_file_stream(source_path, target_path, &err);
      } else {
        if (!looks_like_text(source_bytes)) {
          failures.push_back(QString("Skipped non-text payload: %1").arg(item.display_name));
          ++skipped;
          continue;
        }
        const QByteArray normalized = normalize_text_bytes(source_bytes, options.text_newlines);
        ok = write_bytes_file(target_path, normalized, &err);
      }
      if (!ok) {
        failures.push_back(err.isEmpty() ? QString("Unable to convert text: %1").arg(item.display_name) : err);
      }
    } else {
      ok = copy_file_stream(source_path, target_path, &err);
      if (!ok) {
        failures.push_back(err.isEmpty() ? QString("Unable to copy file: %1").arg(item.display_name) : err);
      }
    }

    if (ok) {
      ++converted_ok;
    }
  }

  progress.setValue(assets.size());

  QString summary = QString("Converted: %1 of %2 file(s)").arg(converted_ok).arg(assets.size());
  if (skipped > 0) {
    summary += QString("\nSkipped: %1").arg(skipped);
  }
  summary += QString("\nOutput: %1").arg(options.output_dir);

  if (!failures.isEmpty()) {
    summary += QString("\n\nIssues (%1):\n%2").arg(failures.size()).arg(failures.mid(0, 16).join("\n"));
    QMessageBox::warning(this, "Batch Conversion", summary);
    return;
  }

  QMessageBox::information(this, "Batch Conversion", summary);
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

void PakTab::dragEnterEvent(QDragEnterEvent* event) {
  if (event && can_accept_mime(event->mimeData())) {
    event->acceptProposedAction();
    return;
  }
  QWidget::dragEnterEvent(event);
}

void PakTab::dragMoveEvent(QDragMoveEvent* event) {
  if (event && can_accept_mime(event->mimeData())) {
    event->acceptProposedAction();
    return;
  }
  QWidget::dragMoveEvent(event);
}

void PakTab::dropEvent(QDropEvent* event) {
  if (handle_drop_event(event, current_prefix())) {
    return;
  }
  QWidget::dropEvent(event);
}

void PakTab::set_dirty(bool dirty) {
  if (dirty_ == dirty) {
    return;
  }
  dirty_ = dirty;
  emit dirty_changed(dirty_);
}

bool PakTab::ensure_editable(const QString& action) {
  if (!loaded_) {
    return false;
  }
  if (archive_.is_loaded() && archive_.format() == Archive::Format::Directory) {
    const QString title = action.isEmpty() ? "Folder View" : action;
    QMessageBox::information(this, title, "Folder views are read-only. Pack it into an archive via Save As...");
    return false;
  }
  if (is_wad_mounted()) {
    QMessageBox::information(this, "Mounted Archive", "This mounted archive view is read-only. Use breadcrumbs to go back.");
    return false;
  }
  if (pure_pak_protector_enabled_ && official_archive_) {
    const QString title = action.isEmpty() ? "Pure PAK Protector" : action;
    QMessageBox::information(
      this,
      title,
      "This archive appears to be an official game archive and is protected from modification.\n\n"
      "Disable Pure PAK Protector in Preferences to edit it, or use Save As to create a copy.");
    return false;
  }
  return true;
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
  if (pure_pak_protector_enabled_ && official_archive_) {
    if (error) {
      *error = "Pure PAK Protector is enabled for this official archive. Disable it in Preferences or use Save As to create a copy.";
    }
    return false;
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
  } else if (archive_.is_loaded() && archive_.format() == Archive::Format::Resources) {
    opts.format = Archive::Format::Resources;
  } else if (archive_.is_loaded() && archive_.format() == Archive::Format::Pak) {
    opts.format = Archive::Format::Pak;
  } else if (archive_.is_loaded() && archive_.format() == Archive::Format::Wad) {
    opts.format = Archive::Format::Wad;
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
    } else if (is_quake_wad_archive_ext(ext)) {
      fmt = Archive::Format::Wad;
    } else if (ext == "resources") {
      fmt = Archive::Format::Resources;
    } else if (ext == "zip" || ext == "pk3" || ext == "pk4" || ext == "pkz") {
      fmt = Archive::Format::Zip;
    } else if (ext == "wad") {
      fmt = Archive::Format::Wad;
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
  if (fmt == Archive::Format::Wad) {
    if (options.quakelive_encrypt_pk3) {
      if (error) {
        *error = "Quake Live PK3 encryption is only supported for ZIP-based archives.";
      }
      return false;
    }
    return write_wad2_file(abs, error);
  }
  if (fmt == Archive::Format::Zip) {
    return write_zip_file(abs, options.quakelive_encrypt_pk3, error);
  }
  if (fmt == Archive::Format::Resources) {
    if (error) {
      *error = "Saving Doom 3 BFG .resources archives is not supported yet.";
    }
    return false;
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
  if (pure_pak_protector_enabled_ && official_archive_ && !pak_path_.isEmpty()) {
    const QString current = QFileInfo(pak_path_).absoluteFilePath();
    if (!current.isEmpty() && current == abs) {
      if (error) {
        *error = "Pure PAK Protector is enabled for this official archive. Disable it in Preferences or use a new destination.";
      }
      return false;
    }
  }

  if (!write_archive_file(abs, options, error)) {
    return false;
  }

  const bool had_mount = is_wad_mounted();
  const QStringList restore_dir = had_mount ? mounted_archives_.front().outer_dir_before_mount : current_dir_;

  QString reload_err;
  if (!archive_.load(abs, &reload_err)) {
    if (error) {
      *error = reload_err.isEmpty() ? "Saved, but failed to reload the new archive." : reload_err;
    }
    return false;
  }

  mounted_archives_.clear();
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
  set_current_dir(restore_dir);
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
  preview_->set_glow_enabled(is_quake2_game(game_id_));
  splitter_->addWidget(preview_);
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 2);
	connect(preview_, &PreviewPane::request_previous_audio, this, [this]() { select_adjacent_audio(-1); });
	connect(preview_, &PreviewPane::request_next_audio, this, [this]() { select_adjacent_audio(1); });
	connect(preview_, &PreviewPane::request_previous_video, this, [this]() { select_adjacent_video(-1); });
	connect(preview_, &PreviewPane::request_next_video, this, [this]() { select_adjacent_video(1); });
	connect(preview_, &PreviewPane::request_image_mip_level, this, [this]() { update_preview(); });

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
    activate_entry(item->text(0), item->data(0, Qt::UserRole).toBool(), item->data(0, kRolePakPath).toString());
  });

  connect(icon_view_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
    if (!item) {
      return;
    }
    activate_entry(item->text(), item->data(Qt::UserRole).toBool(), item->data(kRolePakPath).toString());
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
  connect(cut_sc, &QShortcut::activated, this, [this]() { cut(); });

  auto* copy_sc = new QShortcut(QKeySequence::Copy, this);
  copy_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(copy_sc, &QShortcut::activated, this, [this]() { copy(); });

  auto* paste_sc = new QShortcut(QKeySequence::Paste, this);
  paste_sc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(paste_sc, &QShortcut::activated, this, [this]() { paste(); });

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

  add_files_action_ = toolbar_->addAction(UiIcons::icon(UiIcons::Id::AddFiles, style()), "Add Files...");
  add_files_action_->setToolTip("Add files to the current folder");
  connect(add_files_action_, &QAction::triggered, this, &PakTab::add_files);

  add_folder_action_ = toolbar_->addAction(UiIcons::icon(UiIcons::Id::AddFolder, style()), "Add Folder...");
  add_folder_action_->setToolTip("Add a folder (recursively) to the current folder");
  connect(add_folder_action_, &QAction::triggered, this, &PakTab::add_folder);

  new_folder_action_ = toolbar_->addAction(UiIcons::icon(UiIcons::Id::NewFolder, style()), "New Folder...");
  new_folder_action_->setToolTip("Create a new folder in the current folder");
  connect(new_folder_action_, &QAction::triggered, this, &PakTab::new_folder);

  delete_action_ = toolbar_->addAction(UiIcons::icon(UiIcons::Id::DeleteItem, style()), "Delete");
  delete_action_->setToolTip("Delete selected item (Del). Shift+Del skips confirmation.");
  connect(delete_action_, &QAction::triggered, this, [this]() {
    const bool force = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    delete_selected(force);
  });

  toolbar_->addSeparator();

  view_button_ = new QToolButton(toolbar_);
  view_button_->setIcon(UiIcons::icon(UiIcons::Id::ViewDetails, style()));
  view_button_->setToolTip("Change view mode");
  view_button_->setPopupMode(QToolButton::InstantPopup);

  auto* view_menu = new QMenu(view_button_);
  view_group_ = new QActionGroup(view_menu);
  view_group_->setExclusive(true);

  view_auto_action_ = view_menu->addAction("Auto");
  view_auto_action_->setCheckable(true);
  view_auto_action_->setIcon(UiIcons::icon(UiIcons::Id::ViewAuto, style()));
  view_group_->addAction(view_auto_action_);
  connect(view_auto_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::Auto); });

  view_menu->addSeparator();

  view_details_action_ = view_menu->addAction("Details");
  view_details_action_->setCheckable(true);
  view_details_action_->setIcon(UiIcons::icon(UiIcons::Id::ViewDetails, style()));
  view_group_->addAction(view_details_action_);
  connect(view_details_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::Details); });

  view_list_action_ = view_menu->addAction("List");
  view_list_action_->setCheckable(true);
  view_list_action_->setIcon(UiIcons::icon(UiIcons::Id::ViewList, style()));
  view_group_->addAction(view_list_action_);
  connect(view_list_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::List); });

  view_small_icons_action_ = view_menu->addAction("Small Icons");
  view_small_icons_action_->setCheckable(true);
  view_small_icons_action_->setIcon(UiIcons::icon(UiIcons::Id::ViewSmallIcons, style()));
  view_group_->addAction(view_small_icons_action_);
  connect(view_small_icons_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::SmallIcons); });

  view_large_icons_action_ = view_menu->addAction("Large Icons");
  view_large_icons_action_->setCheckable(true);
  view_large_icons_action_->setIcon(UiIcons::icon(UiIcons::Id::ViewLargeIcons, style()));
  view_group_->addAction(view_large_icons_action_);
  connect(view_large_icons_action_, &QAction::triggered, this, [this]() { set_view_mode(ViewMode::LargeIcons); });

  view_gallery_action_ = view_menu->addAction("Gallery");
  view_gallery_action_->setCheckable(true);
  view_gallery_action_->setIcon(UiIcons::icon(UiIcons::Id::ViewGallery, style()));
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
  cut_action->setIcon(UiIcons::icon(UiIcons::Id::Cut, style()));
  cut_action->setShortcut(QKeySequence::Cut);
  connect(cut_action, &QAction::triggered, this, [this]() { cut(); });

  auto* copy_action = menu.addAction("Copy");
  copy_action->setIcon(UiIcons::icon(UiIcons::Id::Copy, style()));
  copy_action->setShortcut(QKeySequence::Copy);
  connect(copy_action, &QAction::triggered, this, [this]() { copy(); });

  auto* paste_action = menu.addAction("Paste");
  paste_action->setIcon(UiIcons::icon(UiIcons::Id::Paste, style()));
  paste_action->setShortcut(QKeySequence::Paste);
  connect(paste_action, &QAction::triggered, this, [this]() { paste(); });

  auto* rename_action = menu.addAction("Rename");
  rename_action->setIcon(UiIcons::icon(UiIcons::Id::Rename, style()));
  rename_action->setShortcut(QKeySequence(Qt::Key_F2));
  connect(rename_action, &QAction::triggered, this, [this]() { rename_selected(); });

  menu.addSeparator();

  auto* extract_selected_action = menu.addAction("Extract Selected...");
  extract_selected_action->setIcon(UiIcons::icon(UiIcons::Id::OpenFolder, style()));
  connect(extract_selected_action, &QAction::triggered, this, [this]() { extract_selected(); });

  auto* extract_all_action = menu.addAction("Extract All...");
  extract_all_action->setIcon(UiIcons::icon(UiIcons::Id::OpenFolder, style()));
  extract_all_action->setEnabled(can_extract_all());
  connect(extract_all_action, &QAction::triggered, this, [this]() { extract_all(); });

  auto* convert_action = menu.addAction("Convert Selected Assets...");
  convert_action->setIcon(UiIcons::icon(UiIcons::Id::Configure, style()));
  connect(convert_action, &QAction::triggered, this, [this]() { convert_selected_assets(); });

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

void PakTab::apply_auto_view(int file_count, int image_count, int video_count, int model_count, int bsp_count) {
  // Auto: prefer Gallery when there's a meaningful amount of visual assets.
  const int visual_count = image_count + video_count + model_count + bsp_count;
  const bool show_gallery = (bsp_count > 0) || ((file_count > 0) && (visual_count * 100 >= file_count * 10));
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
    QIcon icon = UiIcons::icon(UiIcons::Id::ViewDetails, style());
    if (view_mode_ == ViewMode::Auto) {
      icon = UiIcons::icon(UiIcons::Id::ViewAuto, style());
    } else {
      switch (effective_view_) {
        case ViewMode::Details:
          icon = UiIcons::icon(UiIcons::Id::ViewDetails, style());
          break;
        case ViewMode::List:
          icon = UiIcons::icon(UiIcons::Id::ViewList, style());
          break;
        case ViewMode::SmallIcons:
          icon = UiIcons::icon(UiIcons::Id::ViewSmallIcons, style());
          break;
        case ViewMode::LargeIcons:
          icon = UiIcons::icon(UiIcons::Id::ViewLargeIcons, style());
          break;
        case ViewMode::Gallery:
          icon = UiIcons::icon(UiIcons::Id::ViewGallery, style());
          break;
        case ViewMode::Auto:
          break;
      }
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
      spacing = 0;
      {
        const QFontMetrics fm(icon_view_->font());
        const int text_lines = 2;
        const int text_h = fm.lineSpacing() * text_lines;
        grid = QSize(icon.width() + 2, icon.height() + text_h + 2);
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
  detail_items_by_path_.clear();
  clear_sprite_icon_animations();
  thumbnail_pool_.clear();
}

void PakTab::register_sprite_icon_animation(const QString& pak_path,
                                            const QVector<QImage>& frames,
                                            const QVector<int>& frame_durations_ms,
                                            const QSize& icon_size) {
  if (pak_path.isEmpty() || frames.isEmpty() || !icon_size.isValid()) {
    return;
  }

  const QSize details_icon_size = (details_view_ && details_view_->iconSize().isValid())
                                  ? details_view_->iconSize()
                                  : QSize(24, 24);

  SpriteIconAnimation anim;
  anim.icon_frames.reserve(frames.size());
  anim.detail_frames.reserve(frames.size());
  anim.frame_durations_ms.reserve(frames.size());
  for (int i = 0; i < frames.size(); ++i) {
    const QImage& frame = frames[i];
    const QImage icon_frame = make_centered_icon_frame(frame, icon_size, image_texture_smoothing_);
    const QImage detail_frame = make_centered_icon_frame(frame, details_icon_size, image_texture_smoothing_);
    if (!icon_frame.isNull()) {
      anim.icon_frames.push_back(QIcon(QPixmap::fromImage(icon_frame)));
    }
    if (!detail_frame.isNull()) {
      anim.detail_frames.push_back(QIcon(QPixmap::fromImage(detail_frame)));
    }
    const int ms = (i >= 0 && i < frame_durations_ms.size()) ? frame_durations_ms[i] : 100;
    anim.frame_durations_ms.push_back(std::clamp(ms, 30, 2000));
  }

  if (anim.icon_frames.isEmpty() && anim.detail_frames.isEmpty()) {
    return;
  }

  anim.frame_index = 0;
  anim.elapsed_ms = 0;
  sprite_icon_animations_.insert(pak_path, std::move(anim));

  if (QListWidgetItem* icon_item = icon_items_by_path_.value(pak_path, nullptr)) {
    const SpriteIconAnimation& a = sprite_icon_animations_.value(pak_path);
    if (!a.icon_frames.isEmpty()) {
      icon_item->setIcon(a.icon_frames.first());
    }
  }
  if (QTreeWidgetItem* detail_item = detail_items_by_path_.value(pak_path, nullptr)) {
    const SpriteIconAnimation& a = sprite_icon_animations_.value(pak_path);
    if (!a.detail_frames.isEmpty()) {
      detail_item->setIcon(0, a.detail_frames.first());
    }
  }

  const SpriteIconAnimation& a = sprite_icon_animations_.value(pak_path);
  const int max_frames = std::max(a.icon_frames.size(), a.detail_frames.size());
  if (sprite_icon_timer_ && max_frames > 1 && !sprite_icon_timer_->isActive()) {
    sprite_icon_timer_->start();
  }
}

void PakTab::clear_sprite_icon_animations() {
  sprite_icon_animations_.clear();
  if (sprite_icon_timer_) {
    sprite_icon_timer_->stop();
  }
}

void PakTab::advance_sprite_icon_animations() {
  if (sprite_icon_animations_.isEmpty()) {
    if (sprite_icon_timer_) {
      sprite_icon_timer_->stop();
    }
    return;
  }

  const int dt_ms = (sprite_icon_timer_ && sprite_icon_timer_->interval() > 0) ? sprite_icon_timer_->interval() : 60;
  QVector<QString> to_remove;
  to_remove.reserve(sprite_icon_animations_.size());

  bool has_multi_frame = false;
  for (auto it = sprite_icon_animations_.begin(); it != sprite_icon_animations_.end(); ++it) {
    const QString& pak_path = it.key();
    SpriteIconAnimation& anim = it.value();

    QListWidgetItem* icon_item = icon_items_by_path_.value(pak_path, nullptr);
    QTreeWidgetItem* detail_item = detail_items_by_path_.value(pak_path, nullptr);
    if (!icon_item && !detail_item) {
      to_remove.push_back(pak_path);
      continue;
    }

    const int frame_count = std::max(anim.icon_frames.size(), anim.detail_frames.size());
    if (frame_count <= 0) {
      to_remove.push_back(pak_path);
      continue;
    }
    if (frame_count <= 1) {
      continue;
    }
    has_multi_frame = true;

    anim.elapsed_ms += dt_ms;
    const int delay_ms = (anim.frame_index >= 0 && anim.frame_index < anim.frame_durations_ms.size())
                         ? std::clamp(anim.frame_durations_ms[anim.frame_index], 30, 2000)
                         : 100;
    if (anim.elapsed_ms < delay_ms) {
      continue;
    }

    const int steps = std::max(1, anim.elapsed_ms / delay_ms);
    anim.elapsed_ms %= delay_ms;
    anim.frame_index = (anim.frame_index + steps) % frame_count;

    if (icon_item && !anim.icon_frames.isEmpty()) {
      icon_item->setIcon(anim.icon_frames[anim.frame_index % anim.icon_frames.size()]);
    }
    if (detail_item && !anim.detail_frames.isEmpty()) {
      detail_item->setIcon(0, anim.detail_frames[anim.frame_index % anim.detail_frames.size()]);
    }
  }

  for (const QString& key : to_remove) {
    sprite_icon_animations_.remove(key);
  }

  if (sprite_icon_timer_) {
    if (sprite_icon_animations_.isEmpty() || !has_multi_frame) {
      sprite_icon_timer_->stop();
    }
  }
}

void PakTab::queue_thumbnail(const QString& pak_path,
                             const QString& leaf,
                             const QString& source_path,
                             qint64 size,
                             const QSize& icon_size) {
  if (!icon_view_ && !details_view_) {
    return;
  }
  if (pak_path.isEmpty() || leaf.isEmpty() || !icon_size.isValid()) {
    return;
  }

  const QString ext = file_ext_lower(leaf);
  const bool is_image = is_image_file_name(leaf);
  const bool is_cinematic = (ext == "cin" || ext == "roq");
  const bool is_model = is_model_file_name(leaf);
  const bool is_bsp = is_bsp_file_name(leaf);
  const bool is_sprite = is_sprite_file_ext(ext);
  if (!is_image && !is_cinematic && !is_model && !is_bsp && !is_sprite) {
    return;
  }

  if ((ext == "lmp" || ext == "mip" || ext == "spr") && !quake1_palette_loaded_) {
    ensure_quake1_palette(nullptr);
  }
  if ((ext == "wal" || ext == "sp2" || ext == "spr2") && !quake2_palette_loaded_) {
    ensure_quake2_palette(nullptr);
  }

  // Capture state for this thumbnail generation.
  const quint64 gen = thumbnail_generation_;
  PakTab* self = this;
  const QVector<QRgb> quake1_palette = quake1_palette_;
  const QVector<QRgb> quake2_palette = quake2_palette_;
  const bool texture_smoothing = image_texture_smoothing_;

  auto* task = QRunnable::create([self, gen, pak_path, leaf, source_path, size, icon_size, quake1_palette, quake2_palette, texture_smoothing]() {
    QImage image;
    QVector<QImage> sprite_frames;
    QVector<int> sprite_frame_durations_ms;

    const QString ext = file_ext_lower(leaf);
    const auto decode_options_for = [&](const QString& name) -> ImageDecodeOptions {
      ImageDecodeOptions options;
      const QString e = file_ext_lower(name);
      if ((e == "lmp" || e == "mip") && quake1_palette.size() == 256) {
        options.palette = &quake1_palette;
      } else if (e == "wal" && quake2_palette.size() == 256) {
        options.palette = &quake2_palette;
      }
      return options;
    };
    if (is_image_file_name(leaf)) {
      ImageDecodeOptions options = decode_options_for(leaf);

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
    } else if (is_sprite_file_ext(ext)) {
      QByteArray sprite_bytes;
      QString err;
      if (!source_path.isEmpty()) {
        QFile f(source_path);
        if (f.open(QIODevice::ReadOnly)) {
          constexpr qint64 kMaxSpriteBytes = 64LL * 1024 * 1024;
          sprite_bytes = f.read(kMaxSpriteBytes);
        }
      } else {
        constexpr qint64 kMaxSpriteBytes = 64LL * 1024 * 1024;
        const qint64 max_bytes = (size > 0) ? std::min(size, kMaxSpriteBytes) : kMaxSpriteBytes;
        (void)self->view_archive().read_entry_bytes(pak_path, &sprite_bytes, &err, max_bytes);
      }

      if (!sprite_bytes.isEmpty()) {
        if (ext == "spr") {
          const SpriteDecodeResult decoded = decode_spr_sprite(sprite_bytes, quake1_palette.size() == 256 ? &quake1_palette : nullptr);
          if (decoded.ok()) {
            sprite_frames.reserve(decoded.frames.size());
            sprite_frame_durations_ms.reserve(decoded.frames.size());
            for (const SpriteFrame& frame : decoded.frames) {
              if (frame.image.isNull()) {
                continue;
              }
              sprite_frames.push_back(frame.image);
              sprite_frame_durations_ms.push_back(std::clamp(frame.duration_ms, 30, 2000));
            }
          }
        } else {
          QString normalized_pak = normalize_pak_path(pak_path);
          const int slash = normalized_pak.lastIndexOf('/');
          const QString sprite_dir_prefix = (slash >= 0) ? normalized_pak.left(slash + 1) : QString();
          QHash<QString, QString> by_lower;
          by_lower.reserve(self->view_archive().entries().size());
          for (const ArchiveEntry& e : self->view_archive().entries()) {
            const QString n = normalize_pak_path(e.name);
            if (!n.isEmpty()) {
              by_lower.insert(n.toLower(), e.name);
            }
          }

          const auto decode_bytes = [&](const QByteArray& bytes, const QString& name) -> ImageDecodeResult {
            const ImageDecodeOptions options = decode_options_for(name);
            ImageDecodeResult decoded = decode_image_bytes(bytes, name, options);
            if (!decoded.ok() && options.palette) {
              decoded = decode_image_bytes(bytes, name, {});
            }
            return decoded;
          };

          const auto decode_file_path = [&](const QString& path) -> ImageDecodeResult {
            if (path.isEmpty() || !QFileInfo::exists(path)) {
              return ImageDecodeResult{QImage(), "SP2 frame file was not found."};
            }
            const ImageDecodeOptions options = decode_options_for(path);
            ImageDecodeResult decoded = decode_image_file(path, options);
            if (!decoded.ok() && options.palette) {
              decoded = decode_image_file(path, {});
            }
            return decoded;
          };

          const Sp2FrameLoader load_frame = [&](const QString& frame_name) -> ImageDecodeResult {
            QString ref = frame_name;
            ref.replace('\\', '/');
            while (ref.startsWith('/')) {
              ref.remove(0, 1);
            }
            const QString leaf_name = QFileInfo(ref).fileName();

            if (!source_path.isEmpty()) {
              const QString base_dir = QFileInfo(source_path).absolutePath();
              QVector<QString> file_candidates;
              file_candidates.reserve(4);
              if (QFileInfo(ref).isAbsolute()) {
                file_candidates.push_back(ref);
              }
              if (!base_dir.isEmpty()) {
                file_candidates.push_back(QDir(base_dir).filePath(ref));
                if (!leaf_name.isEmpty()) {
                  file_candidates.push_back(QDir(base_dir).filePath(leaf_name));
                }
              }

              for (const QString& cand : file_candidates) {
                const ImageDecodeResult decoded = decode_file_path(cand);
                if (decoded.ok()) {
                  return decoded;
                }
              }
            }

            QVector<QString> candidates;
            candidates.reserve(6);
            auto add_candidate = [&](const QString& c) {
              const QString normalized = normalize_pak_path(c);
              if (!normalized.isEmpty()) {
                candidates.push_back(normalized);
              }
            };

            add_candidate(ref);
            if (!sprite_dir_prefix.isEmpty() && !ref.startsWith(sprite_dir_prefix)) {
              add_candidate(sprite_dir_prefix + ref);
            }
            if (!leaf_name.isEmpty()) {
              add_candidate(leaf_name);
              if (!sprite_dir_prefix.isEmpty()) {
                add_candidate(sprite_dir_prefix + leaf_name);
              }
            }

            constexpr qint64 kMaxFrameBytes = 16LL * 1024 * 1024;
            for (const QString& want : candidates) {
              const QString found = by_lower.value(want.toLower());
              if (found.isEmpty()) {
                continue;
              }
              QByteArray frame_bytes;
              QString read_err;
              if (!self->view_archive().read_entry_bytes(found, &frame_bytes, &read_err, kMaxFrameBytes)) {
                continue;
              }
              const ImageDecodeResult decoded = decode_bytes(frame_bytes, QFileInfo(found).fileName());
              if (decoded.ok()) {
                return decoded;
              }
            }

            return ImageDecodeResult{QImage(), "Unable to resolve SP2 frame image."};
          };

          const SpriteDecodeResult decoded = decode_sp2_sprite(sprite_bytes, load_frame);
          if (decoded.ok()) {
            sprite_frames.reserve(decoded.frames.size());
            sprite_frame_durations_ms.reserve(decoded.frames.size());
            for (const SpriteFrame& frame : decoded.frames) {
              if (frame.image.isNull()) {
                continue;
              }
              sprite_frames.push_back(frame.image);
              sprite_frame_durations_ms.push_back(std::clamp(frame.duration_ms, 30, 2000));
            }
          }
        }
      }

      if (!sprite_frames.isEmpty()) {
        image = sprite_frames.first();
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
    } else if (ext == "bsp") {
      BspPreviewResult preview;
      if (!source_path.isEmpty()) {
        preview = render_bsp_preview_file(source_path, BspPreviewStyle::Silhouette, qMax(icon_size.width(), icon_size.height()));
      } else {
        constexpr qint64 kMaxBspBytes = 128LL * 1024 * 1024;
        const qint64 max_bytes = (size > 0) ? std::min(size, kMaxBspBytes) : kMaxBspBytes;
        QByteArray bytes;
        QString err;
        if (self->view_archive().read_entry_bytes(pak_path, &bytes, &err, max_bytes)) {
          preview = render_bsp_preview_bytes(bytes, leaf, BspPreviewStyle::Silhouette, qMax(icon_size.width(), icon_size.height()));
        }
      }
      if (preview.ok()) {
        image = preview.image;
      }
    }

    if (image.isNull()) {
      return;
    }

    image = make_centered_icon_frame(image, icon_size, texture_smoothing);
    if (image.isNull()) {
      return;
    }

    QMetaObject::invokeMethod(self,
                              [self,
                               gen,
                               pak_path,
                               icon_size,
                               image = std::move(image),
                               sprite_frames = std::move(sprite_frames),
                               sprite_frame_durations_ms = std::move(sprite_frame_durations_ms)]() mutable {
      if (!self || self->thumbnail_generation_ != gen) {
        return;
      }
      const QIcon icon(QPixmap::fromImage(image));
      if (QListWidgetItem* item = self->icon_items_by_path_.value(pak_path, nullptr)) {
        item->setIcon(icon);
      }
      if (QTreeWidgetItem* item = self->detail_items_by_path_.value(pak_path, nullptr)) {
        item->setIcon(0, icon);
      }
      if (!sprite_frames.isEmpty()) {
        self->register_sprite_icon_animation(pak_path, sprite_frames, sprite_frame_durations_ms, icon_size);
      }
    },
                              Qt::QueuedConnection);
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

void PakTab::restore_workspace(const QString& dir_prefix, const QString& selected_path) {
  if (!loaded_) {
    return;
  }

  QString prefix = normalize_pak_path(dir_prefix);
  if (prefix.isEmpty() && !selected_path.isEmpty()) {
    const QString sel = normalize_pak_path(selected_path);
    const int slash = sel.lastIndexOf('/');
    if (slash >= 0) {
      prefix = sel.left(slash + 1);
    }
  }
  if (prefix.endsWith('/')) {
    prefix.chop(1);
  }

  QStringList parts;
  if (!prefix.isEmpty()) {
    parts = prefix.split('/', Qt::SkipEmptyParts);
  }
  set_current_dir(parts);

  const QString sel = normalize_pak_path(selected_path);
  if (!sel.isEmpty()) {
    QTimer::singleShot(0, this, [this, sel]() { select_path(sel); });
  }
}

void PakTab::select_path(const QString& pak_path) {
  if (!loaded_) {
    return;
  }
  const QString want = normalize_pak_path(pak_path);
  if (want.isEmpty()) {
    return;
  }

  auto select_in_details = [&]() -> bool {
    if (!details_view_) {
      return false;
    }
    for (int i = 0; i < details_view_->topLevelItemCount(); ++i) {
      QTreeWidgetItem* item = details_view_->topLevelItem(i);
      if (!item) {
        continue;
      }
      const QString stored = item->data(0, kRolePakPath).toString();
      if (!stored.isEmpty() && normalize_pak_path(stored) == want) {
        details_view_->setCurrentItem(item);
        item->setSelected(true);
        details_view_->scrollToItem(item);
        return true;
      }
    }
    return false;
  };

  auto select_in_icons = [&]() -> bool {
    if (!icon_view_) {
      return false;
    }
    for (int i = 0; i < icon_view_->count(); ++i) {
      QListWidgetItem* item = icon_view_->item(i);
      if (!item) {
        continue;
      }
      const QString stored = item->data(kRolePakPath).toString();
      if (!stored.isEmpty() && normalize_pak_path(stored) == want) {
        icon_view_->setCurrentItem(item);
        item->setSelected(true);
        icon_view_->scrollToItem(item);
        return true;
      }
    }
    return false;
  };

  if (view_stack_ && view_stack_->currentWidget() == details_view_) {
    if (!select_in_details()) {
      (void)select_in_icons();
    }
    return;
  }
  if (view_stack_ && view_stack_->currentWidget() == icon_view_) {
    if (!select_in_icons()) {
      (void)select_in_details();
    }
    return;
  }

  if (!select_in_details()) {
    (void)select_in_icons();
  }
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
  const bool filter_by_prefix = !prefix.isEmpty();
  if (filter_by_prefix && !prefix.endsWith('/')) {
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
    if ((filter_by_prefix && !vdir.startsWith(prefix)) || is_deleted_path(vdir)) {
      continue;
    }
    const QString rel = filter_by_prefix ? vdir.mid(prefix.size()) : vdir;
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
      if ((filter_by_prefix && !name.startsWith(prefix)) || is_deleted_path(name)) {
        continue;
      }
      if (added_index_by_name_.contains(name)) {
        continue;  // overridden by added file
      }
      const QString rel = filter_by_prefix ? name.mid(prefix.size()) : name;
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
    if ((filter_by_prefix && !name.startsWith(prefix)) || is_deleted_path(name)) {
      continue;
    }
    const QString rel = filter_by_prefix ? name.mid(prefix.size()) : name;
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
    if (is_wad_mounted()) {
      if (error) {
        *error = "Folders are not available inside a mounted container.";
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
  if (!is_wad_mounted() && added_idx >= 0 && added_idx < added_files_.size()) {
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

bool PakTab::open_entry_with_associated_app(const QString& pak_path_in, const QString& display_name) {
  const QString pak_path = normalize_pak_path(pak_path_in);
  const QString title = display_name.isEmpty() ? QString("File") : display_name;
  if (pak_path.isEmpty()) {
    if (preview_) {
      preview_->show_message(title, "Invalid file path.");
    }
    return false;
  }

  QString exported_path;
  QString err;
  if (!export_path_to_temp(pak_path, false, &exported_path, &err)) {
    const QString msg = err.isEmpty() ? "Unable to export file for external opening." : err;
    if (preview_) {
      preview_->show_message(title, msg);
    } else {
      QMessageBox::warning(this, "Open File", msg);
    }
    return false;
  }

  if (exported_path.isEmpty() || !QFileInfo::exists(exported_path)) {
    const QString msg = "Unable to locate exported file.";
    if (preview_) {
      preview_->show_message(title, msg);
    } else {
      QMessageBox::warning(this, "Open File", msg);
    }
    return false;
  }

  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(exported_path))) {
    const QString msg = "No associated application is available for this file type.";
    if (preview_) {
      preview_->show_message(title, msg);
    } else {
      QMessageBox::warning(this, "Open File", msg);
    }
    return false;
  }

  return true;
}

void PakTab::activate_entry(const QString& item_name, bool is_dir, const QString& pak_path_in) {
  QString path = normalize_pak_path(pak_path_in);
  if (path.isEmpty() && !item_name.isEmpty()) {
    path = normalize_pak_path(current_prefix() + item_name);
  }

  if (is_dir) {
    enter_directory(item_name);
    return;
  }

  const QString leaf = pak_leaf_name(path.isEmpty() ? item_name : path);
  if (is_mountable_archive_file_name(leaf)) {
    QString err;
    if (!mount_wad_from_selected_file(path, &err) && !err.isEmpty()) {
      if (preview_) {
        preview_->show_message(leaf.isEmpty() ? "Archive" : leaf, err);
      } else {
        QMessageBox::warning(this, "Open Container", err);
      }
    }
    return;
  }

  (void)open_entry_with_associated_app(path, leaf);
}

void PakTab::delete_selected(bool skip_confirmation) {
  if (!ensure_editable("Delete")) {
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
    if (auto* del = qobject_cast<QPushButton*>(del_btn)) {
      del->setIcon(UiIcons::icon(UiIcons::Id::DeleteItem, del->style()));
    }
    if (QAbstractButton* cancel_button = box.button(QMessageBox::Cancel)) {
      cancel_button->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, cancel_button->style()));
    }
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

void PakTab::import_urls_with_undo(const QList<QUrl>& urls,
                                   const QString& dest_prefix,
                                   const QString& label,
                                   const QVector<QPair<QString, bool>>& cut_items,
                                   bool is_cut) {
  if (!ensure_editable(label)) {
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

bool PakTab::can_accept_mime(const QMimeData* mime) const {
  if (!mime) {
    return false;
  }
  PakFuMimePayload payload;
  if (parse_pakfu_mime(mime, &payload) && !payload.items.isEmpty()) {
    return true;
  }
  const QList<QUrl> urls = local_urls_from_mime(mime);
  return !urls.isEmpty();
}

bool PakTab::handle_drop_event(QDropEvent* event, const QString& dest_prefix_in) {
  if (!event || !event->mimeData()) {
    return false;
  }

  const QMimeData* mime = event->mimeData();
  PakFuMimePayload payload;
  const bool has_payload = parse_pakfu_mime(mime, &payload);

  QList<QUrl> urls = local_urls_from_mime(mime);
  if (urls.isEmpty() && !has_payload) {
    return false;
  }

  QString dest_prefix = normalize_pak_path(dest_prefix_in);
  if (!dest_prefix.isEmpty() && !dest_prefix.endsWith('/')) {
    dest_prefix += '/';
  }

  const Qt::DropAction requested_action = resolve_requested_drop_action(
    event->dropAction(),
    event->proposedAction(),
    event->possibleActions(),
    event->modifiers());

  const bool source_is_this_tab = has_payload && payload.source_uid == drag_source_uid_;
  const bool source_is_same_archive = has_payload &&
    !payload.source_archive.isEmpty() &&
    !pak_path_.isEmpty() &&
    fs_paths_equal(payload.source_archive, pak_path_);
  bool wants_move = (requested_action == Qt::MoveAction) && (source_is_this_tab || source_is_same_archive);

  QList<QUrl> import_urls = urls;
  QVector<QPair<QString, bool>> move_items;

  if (wants_move && !payload.items.isEmpty() && payload.items.size() == urls.size()) {
    QList<QUrl> filtered_urls;
    QVector<QPair<QString, bool>> filtered_items;
    filtered_urls.reserve(urls.size());
    filtered_items.reserve(payload.items.size());

    bool move_blocked = false;
    for (int i = 0; i < payload.items.size() && i < urls.size(); ++i) {
      const auto& item = payload.items[i];
      const QString leaf = pak_leaf_name(item.first);
      QString new_path = normalize_pak_path(dest_prefix + leaf + (item.second ? "/" : ""));
      if (pak_paths_equal(item.first, new_path)) {
        continue;  // No-op move.
      }
      if (item.second && pak_path_is_under(dest_prefix, item.first)) {
        move_blocked = true;
        break;
      }
      filtered_urls.push_back(urls[i]);
      filtered_items.push_back(item);
    }

    if (!move_blocked) {
      import_urls = filtered_urls;
      move_items = filtered_items;
      if (import_urls.isEmpty()) {
        const Qt::DropAction accepted = resolve_requested_drop_action(
          requested_action,
          Qt::IgnoreAction,
          event->possibleActions(),
          event->modifiers());
        event->setDropAction(accepted);
        event->accept();
        return true;
      }
    } else {
      wants_move = false;
    }
  } else if (wants_move) {
    wants_move = false;
  }

  if (!wants_move && event->dropAction() == Qt::MoveAction) {
    event->setDropAction(Qt::CopyAction);
  }

  if (import_urls.isEmpty()) {
    return false;
  }

  import_urls_with_undo(import_urls, dest_prefix, wants_move ? "Move" : "Drop", move_items, wants_move);

  const Qt::DropAction accepted_action = resolve_requested_drop_action(
    wants_move ? Qt::MoveAction : Qt::CopyAction,
    Qt::IgnoreAction,
    event->possibleActions(),
    event->modifiers());
  event->setDropAction(accepted_action);
  event->accept();
  return true;
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
  root.insert("source_uid", drag_source_uid_);
  root.insert("source_archive", pak_path_.isEmpty() ? QString() : QFileInfo(pak_path_).absoluteFilePath());
  root.insert("items", json_items);

  QStringList local_paths;
  local_paths.reserve(urls.size());
  for (const QUrl& url : urls) {
    if (url.isLocalFile()) {
      local_paths.push_back(url.toLocalFile());
    }
  }

  auto* mime = new QMimeData();
  mime->setUrls(urls);
  if (!local_paths.isEmpty()) {
    mime->setText(local_paths.join('\n'));
  }
  mime->setData(kPakFuMimeType, QJsonDocument(root).toJson(QJsonDocument::Compact));
  return mime;
}

bool PakTab::try_copy_shader_selection_to_clipboard() {
  if (!preview_ || !preview_->is_shader_view_active()) {
    return false;
  }
  const QString text = preview_->selected_shader_blocks_text();
  if (text.trimmed().isEmpty()) {
    return false;
  }
  auto* mime = new QMimeData();
  mime->setText(text);
  QApplication::clipboard()->setMimeData(mime);
  return true;
}

bool PakTab::try_paste_shader_blocks_from_clipboard() {
  if (!preview_ || !preview_->is_shader_view_active()) {
    return false;
  }
  if (!loaded_) {
    return true;
  }

  const QVector<QPair<QString, bool>> items = selected_items();
  if (items.size() != 1 || items.first().second) {
    return false;
  }

  const QString pak_path = normalize_pak_path(items.first().first);
  const QString leaf = pak_leaf_name(pak_path);
  if (file_ext_lower(leaf) != "shader") {
    return false;
  }

  const QMimeData* mime = QApplication::clipboard()->mimeData();
  if (!mime || !mime->hasText()) {
    return false;
  }
  const QString clipboard_text = mime->text();
  if (clipboard_text.trimmed().isEmpty()) {
    return false;
  }

  Quake3ShaderDocument pasted_doc;
  QString parse_error;
  if (!parse_quake3_shader_text(clipboard_text, &pasted_doc, &parse_error) || pasted_doc.shaders.isEmpty()) {
    return false;
  }

  if (!ensure_editable("Paste Shader")) {
    return true;
  }

  constexpr qint64 kMaxShaderBytes = 4LL * 1024 * 1024;
  QByteArray bytes;
  QString err;

  const int added_idx = added_index_by_name_.value(pak_path, -1);
  if (added_idx >= 0 && added_idx < added_files_.size()) {
    QFile f(added_files_[added_idx].source_path);
    if (!f.open(QIODevice::ReadOnly)) {
      QMessageBox::warning(this, "Paste Shader", "Unable to open the current .shader source file.");
      return true;
    }
    if (f.size() > kMaxShaderBytes) {
      QMessageBox::warning(this, "Paste Shader", ".shader file is too large to edit inline.");
      return true;
    }
    bytes = f.readAll();
  } else {
    if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, kMaxShaderBytes)) {
      QMessageBox::warning(this, "Paste Shader", err.isEmpty() ? "Unable to read the current .shader file." : err);
      return true;
    }
  }

  const QString current_text = QString::fromUtf8(bytes);
  const QString updated_text = append_quake3_shader_blocks_text(current_text, pasted_doc);
  if (updated_text == current_text) {
    return true;
  }

  const QString temp_root = ensure_export_root();
  if (temp_root.isEmpty()) {
    QMessageBox::warning(this, "Paste Shader", "Unable to create temporary workspace for shader edits.");
    return true;
  }

  const QString op_dir = QDir(temp_root).filePath(QString("shader-edit-%1").arg(export_seq_++));
  if (!QDir().mkpath(op_dir)) {
    QMessageBox::warning(this, "Paste Shader", "Unable to create temporary workspace for shader edits.");
    return true;
  }

  const QString out_name = leaf.isEmpty() ? QString("shader.shader") : leaf;
  const QString out_path = QDir(op_dir).filePath(out_name);
  QSaveFile out(out_path);
  if (!out.open(QIODevice::WriteOnly) || out.write(updated_text.toUtf8()) < 0 || !out.commit()) {
    QMessageBox::warning(this, "Paste Shader", "Unable to write updated shader content.");
    return true;
  }

  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QString add_err;
  if (!add_file_mapping(pak_path, out_path, &add_err)) {
    QMessageBox::warning(this, "Paste Shader", add_err.isEmpty() ? "Unable to update shader file in archive." : add_err);
    return true;
  }

  if (undo_stack_) {
    undo_stack_->push(new PakTabStateCommand(this,
                                             "Paste Shader Blocks",
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
  select_path(pak_path);
  update_preview();
  return true;
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
  if (!ensure_editable("Paste")) {
    return;
  }

  const QMimeData* mime = QApplication::clipboard()->mimeData();
  if (!mime) {
    return;
  }

  QList<QUrl> urls = local_urls_from_mime(mime);
  if (urls.isEmpty()) {
    return;
  }

  bool is_cut = false;
  QVector<QPair<QString, bool>> cut_items;
  PakFuMimePayload payload;
  if (parse_pakfu_mime(mime, &payload)) {
    const bool source_is_this_tab = payload.source_uid == drag_source_uid_;
    const bool source_is_same_archive = !payload.source_archive.isEmpty() &&
      !pak_path_.isEmpty() &&
      fs_paths_equal(payload.source_archive, pak_path_);
    is_cut = payload.cut && (source_is_this_tab || source_is_same_archive);
    cut_items = payload.items;
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
  if (!ensure_editable("Rename")) {
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

  if (archive_.is_loaded() && archive_.format() == Archive::Format::Wad && !is_wad_mounted()) {
    QString lump_name;
    QString wad_err;
    if (!derive_wad2_lump_name(pak_name, &lump_name, &wad_err)) {
      if (error) {
        *error = wad_err.isEmpty() ? QString("Invalid WAD entry name: %1").arg(pak_name) : wad_err;
      }
      return false;
    }
  } else {
    const bool current_is_sin =
      is_sin_archive_path(pak_path_) || (archive_.is_loaded() && archive_.format() == Archive::Format::Pak && is_sin_archive_path(archive_.path()));
    const int max_name_bytes = current_is_sin ? kSinNameBytes : kPakNameBytes;
    const QByteArray name_bytes = pak_name.toLatin1();
    if (name_bytes.isEmpty() || name_bytes.size() > max_name_bytes) {
      if (error) {
        *error = QString("Archive path is too long for %1 format: %2").arg(current_is_sin ? "SiN" : "PAK", pak_name);
      }
      return false;
    }
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
      const bool current_is_sin =
        is_sin_archive_path(pak_path_) || (archive_.is_loaded() && archive_.format() == Archive::Format::Pak && is_sin_archive_path(archive_.path()));
      *error = QString("File is too large for %1 format: %2").arg(current_is_sin ? "SiN" : "PAK", info.fileName());
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
  if (!ensure_editable("Add Files")) {
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
  if (!ensure_editable("Add Folder")) {
    return;
  }
  if (archive_.is_loaded() && archive_.format() == Archive::Format::Wad) {
    QMessageBox::information(this, "Add Folder", "WAD2 archives are flat. Use Add Files for individual lumps.");
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
  if (!ensure_editable("New Folder")) {
    return;
  }
  if (archive_.is_loaded() && archive_.format() == Archive::Format::Wad) {
    QMessageBox::information(this, "New Folder", "WAD2 archives do not support folders.");
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

  const QString dest_ext = file_ext_lower(abs);
  const bool source_is_sin =
    archive_.is_loaded() && archive_.format() == Archive::Format::Pak && is_sin_archive_path(archive_.path());
  const bool write_sin = is_sin_archive_ext(dest_ext) || (source_is_sin && dest_ext != "pak");
  const int name_bytes_limit = write_sin ? kSinNameBytes : kPakNameBytes;
  const int dir_entry_size = write_sin ? kSinDirEntrySize : kPakDirEntrySize;
  const char archive_sig0 = write_sin ? 'S' : 'P';
  const char archive_sig1 = write_sin ? 'P' : 'A';
  const char archive_sig2 = write_sin ? 'A' : 'C';
  const char archive_sig3 = write_sin ? 'K' : 'K';
  const QString archive_label = write_sin ? "SiN archive" : "PAK";

  if (mode_ == Mode::ExistingPak && archive_.is_loaded() &&
      archive_.format() != Archive::Format::Pak &&
      archive_.format() != Archive::Format::Directory) {
    if (error) {
      *error = "Saving as PAK/SiN archive is only supported when the source is a PAK/SiN archive or a folder.";
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
  const bool have_src_pak = archive_.is_loaded() &&
    archive_.format() == Archive::Format::Pak &&
    !archive_.path().isEmpty();
  const bool have_src_dir = archive_.is_loaded() &&
    archive_.format() == Archive::Format::Directory &&
    !archive_.path().isEmpty();
  if (have_src_pak) {
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
      *error = QString("Unable to create destination %1.").arg(archive_label);
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  header[0] = archive_sig0;
  header[1] = archive_sig1;
  header[2] = archive_sig2;
  header[3] = archive_sig3;
  if (out.write(header) != header.size()) {
    if (error) {
      *error = QString("Unable to write %1 header.").arg(archive_label);
    }
    return false;
  }

  QVector<ArchiveEntry> new_entries;
  new_entries.reserve(((have_src_pak || have_src_dir) ? archive_.entries().size() : 0) + added_files_.size());

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

  if (have_src_pak) {
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
      if (name_bytes.isEmpty() || name_bytes.size() > name_bytes_limit) {
        if (error) {
          *error = QString("%1 entry name is too long: %2").arg(archive_label, name);
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
      if (!ensure_u32_pos(out_offset64, "Archive output exceeds format limits.")) {
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

  if (have_src_dir) {
    const QString root = archive_.path();
    const QDir root_dir(root);

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
      if (name_bytes.isEmpty() || name_bytes.size() > name_bytes_limit) {
        if (error) {
          *error = QString("%1 entry name is too long: %2").arg(archive_label, name);
        }
        return false;
      }

      QFile in(root_dir.filePath(QString(name).replace('/', QDir::separator())));
      if (!in.open(QIODevice::ReadOnly)) {
        if (error) {
          *error = QString("Unable to open file: %1").arg(in.fileName());
        }
        return false;
      }

      const qint64 in_size64 = in.size();
      if (in_size64 < 0 || in_size64 > std::numeric_limits<quint32>::max()) {
        if (error) {
          *error = QString("File is too large for %1 format: %2").arg(archive_label, in.fileName());
        }
        return false;
      }
      const quint32 in_size = static_cast<quint32>(in_size64);

      const qint64 out_offset64 = out.pos();
      if (!ensure_u32_pos(out_offset64, "Archive output exceeds format limits.")) {
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
            *error = QString("Unable to read file: %1").arg(in.fileName());
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
    if (name_bytes.isEmpty() || name_bytes.size() > name_bytes_limit) {
      if (error) {
        *error = QString("%1 entry name is too long: %2").arg(archive_label, name);
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
        *error = QString("File is too large for %1 format: %2").arg(archive_label, f.source_path);
      }
      return false;
    }
    const quint32 in_size = static_cast<quint32>(in_size64);

    const qint64 out_offset64 = out.pos();
    if (!ensure_u32_pos(out_offset64, "Archive output exceeds format limits.")) {
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
  if (!ensure_u32_pos(dir_offset64, "Archive output exceeds format limits.")) {
    return false;
  }
  const quint32 dir_offset = static_cast<quint32>(dir_offset64);

  const qint64 dir_length64 = static_cast<qint64>(new_entries.size()) * dir_entry_size;
  if (dir_length64 < 0 || dir_length64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = QString("%1 directory exceeds format limits.").arg(archive_label);
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
    if (name_bytes.isEmpty() || name_bytes.size() > name_bytes_limit) {
      if (error) {
        *error = QString("%1 entry name is too long: %2").arg(archive_label, e.name);
      }
      return false;
    }
    const int base = i * dir_entry_size;
    std::memcpy(dir.data() + base, name_bytes.constData(), static_cast<size_t>(name_bytes.size()));
    write_u32_le(&dir, base + name_bytes_limit, e.offset);
    write_u32_le(&dir, base + name_bytes_limit + 4, e.size);
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = QString("Unable to write %1 directory.").arg(archive_label);
    }
    return false;
  }

  // Close the source PAK before committing in case we're overwriting in-place.
  src.close();

  write_u32_le(&header, 4, dir_offset);
  write_u32_le(&header, 8, dir_length);
  if (!out.seek(0) || out.write(header) != header.size()) {
    if (error) {
      *error = QString("Unable to update %1 header.").arg(archive_label);
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize destination %1.").arg(archive_label);
    }
    return false;
  }

  return true;
}

bool PakTab::write_wad2_file(const QString& dest_path, QString* error) {
  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }

  if (mode_ == Mode::ExistingPak && archive_.is_loaded() &&
      archive_.format() != Archive::Format::Wad &&
      archive_.format() != Archive::Format::Directory) {
    if (error) {
      *error = "Saving as WAD2 is only supported when the source is a WAD archive or a folder.";
    }
    return false;
  }

  // Ensure we have a source archive loaded if we are repacking an existing WAD.
  if (mode_ == Mode::ExistingPak && !archive_.is_loaded() && !pak_path_.isEmpty()) {
    QString load_err;
    if (!archive_.load(pak_path_, &load_err)) {
      if (error) {
        *error = load_err.isEmpty() ? "Unable to load archive." : load_err;
      }
      return false;
    }
  }

  for (const QString& vdir_in : virtual_dirs_) {
    const QString vdir = normalize_pak_path(vdir_in);
    if (vdir.isEmpty() || is_deleted_path(vdir)) {
      continue;
    }
    if (error) {
      *error = "WAD2 archives do not support folders. Remove pending folders before saving.";
    }
    return false;
  }

  struct WadWriteItem {
    QString entry_name;
    QString source_path;
    bool from_archive = false;
    QString lump_name;
  };

  QVector<WadWriteItem> items;
  items.reserve((archive_.is_loaded() ? archive_.entries().size() : 0) + added_files_.size());

  QHash<QString, QString> lump_owner_by_key;
  lump_owner_by_key.reserve(items.capacity());

  const auto add_item = [&](const QString& name_in, const QString& source_path, bool from_archive) -> bool {
    const QString entry_name = normalize_pak_path(name_in);
    if (entry_name.isEmpty() || is_deleted_path(entry_name)) {
      return true;
    }
    if (!is_safe_entry_name(entry_name)) {
      if (error) {
        *error = QString("Refusing to save unsafe entry: %1").arg(entry_name);
      }
      return false;
    }

    QString lump_name;
    QString lump_err;
    if (!derive_wad2_lump_name(entry_name, &lump_name, &lump_err)) {
      if (error) {
        *error = lump_err.isEmpty() ? QString("Invalid WAD entry name: %1").arg(entry_name) : lump_err;
      }
      return false;
    }

    const QString key = lump_name.toLower();
    const QString existing = lump_owner_by_key.value(key);
    if (!existing.isEmpty()) {
      if (error) {
        *error = QString("Duplicate WAD lump name after normalization: %1 (from %2 and %3)")
                   .arg(lump_name, existing, entry_name);
      }
      return false;
    }
    lump_owner_by_key.insert(key, entry_name);

    WadWriteItem item;
    item.entry_name = entry_name;
    item.source_path = source_path;
    item.from_archive = from_archive;
    item.lump_name = lump_name;
    items.push_back(std::move(item));
    return true;
  };

  const bool have_src_dir = archive_.is_loaded() &&
    archive_.format() == Archive::Format::Directory &&
    !archive_.path().isEmpty();

  if (archive_.is_loaded()) {
    const QDir root_dir(archive_.path());
    for (const ArchiveEntry& e : archive_.entries()) {
      const QString name = normalize_pak_path(e.name);
      if (name.isEmpty() || is_deleted_path(name) || added_index_by_name_.contains(name)) {
        continue;
      }
      if (have_src_dir) {
        const QString source_path = root_dir.filePath(QString(name).replace('/', QDir::separator()));
        if (!add_item(name, source_path, false)) {
          return false;
        }
      } else {
        if (!add_item(name, QString(), true)) {
          return false;
        }
      }
    }
  }

  for (const AddedFile& f : added_files_) {
    if (!add_item(f.pak_name, f.source_path, false)) {
      return false;
    }
  }

  QSaveFile out(abs);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create destination WAD.";
    }
    return false;
  }

  QByteArray header(kWadHeaderSize, '\0');
  std::memcpy(header.data(), "WAD2", 4);
  if (out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to write WAD2 header.";
    }
    return false;
  }

  struct WadDirEntry {
    quint32 file_pos = 0;
    quint32 disk_size = 0;
    quint32 size = 0;
    quint8 type = 0;
    QByteArray lump_name_latin1;
  };

  QVector<WadDirEntry> dir_entries;
  dir_entries.reserve(items.size());

  QByteArray buffer;
  buffer.resize(1 << 16);

  for (const WadWriteItem& item : items) {
    const qint64 out_pos = out.pos();
    if (out_pos < 0 || out_pos > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = "WAD2 output exceeds format limits.";
      }
      return false;
    }

    quint32 size = 0;
    quint8 type = kWadTypeNone;

    if (item.from_archive) {
      QByteArray bytes;
      QString read_err;
      if (!archive_.read_entry_bytes(item.entry_name, &bytes, &read_err)) {
        if (error) {
          *error = read_err.isEmpty() ? QString("Unable to read source entry: %1").arg(item.entry_name) : read_err;
        }
        return false;
      }
      if (bytes.size() < 0 || static_cast<quint64>(bytes.size()) > std::numeric_limits<quint32>::max()) {
        if (error) {
          *error = QString("Entry is too large for WAD2 format: %1").arg(item.entry_name);
        }
        return false;
      }
      if (out.write(bytes) != bytes.size()) {
        if (error) {
          *error = QString("Unable to write destination entry: %1").arg(item.entry_name);
        }
        return false;
      }
      size = static_cast<quint32>(bytes.size());
      type = derive_wad2_lump_type(item.entry_name, item.lump_name, &bytes);
    } else {
      QFile in(item.source_path);
      if (!in.open(QIODevice::ReadOnly)) {
        if (error) {
          *error = QString("Unable to open file: %1").arg(item.source_path);
        }
        return false;
      }

      const qint64 in_size64 = in.size();
      if (in_size64 < 0 || in_size64 > std::numeric_limits<quint32>::max()) {
        if (error) {
          *error = QString("File is too large for WAD2 format: %1").arg(item.source_path);
        }
        return false;
      }
      size = static_cast<quint32>(in_size64);
      type = derive_wad2_lump_type(item.entry_name, item.lump_name, nullptr);

      quint32 remaining = size;
      while (remaining > 0) {
        const int want =
          static_cast<int>(std::min<quint32>(remaining, static_cast<quint32>(buffer.size())));
        const qint64 got = in.read(buffer.data(), want);
        if (got <= 0) {
          if (error) {
            *error = QString("Unable to read file: %1").arg(item.source_path);
          }
          return false;
        }
        if (out.write(buffer.constData(), got) != got) {
          if (error) {
            *error = QString("Unable to write destination entry: %1").arg(item.entry_name);
          }
          return false;
        }
        remaining -= static_cast<quint32>(got);
      }
    }

    WadDirEntry d;
    d.file_pos = static_cast<quint32>(out_pos);
    d.disk_size = size;
    d.size = size;
    d.type = type;
    d.lump_name_latin1 = item.lump_name.toLatin1();
    dir_entries.push_back(std::move(d));
  }

  const qint64 dir_offset64 = out.pos();
  if (dir_offset64 < 0 || dir_offset64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = "WAD2 output exceeds format limits.";
    }
    return false;
  }
  const quint32 dir_offset = static_cast<quint32>(dir_offset64);

  const qint64 dir_bytes64 = static_cast<qint64>(dir_entries.size()) * kWadDirEntrySize;
  if (dir_bytes64 < 0 || dir_bytes64 > std::numeric_limits<int>::max()) {
    if (error) {
      *error = "WAD2 directory exceeds format limits.";
    }
    return false;
  }

  QByteArray dir;
  dir.resize(static_cast<int>(dir_bytes64));
  dir.fill('\0');
  for (int i = 0; i < dir_entries.size(); ++i) {
    const WadDirEntry& d = dir_entries[i];
    const int base = i * kWadDirEntrySize;
    write_u32_le(&dir, base + 0, d.file_pos);
    write_u32_le(&dir, base + 4, d.disk_size);
    write_u32_le(&dir, base + 8, d.size);
    dir[base + 12] = static_cast<char>(d.type);
    dir[base + 13] = 0;
    dir[base + 14] = 0;
    dir[base + 15] = 0;
    const QByteArray lump = d.lump_name_latin1.left(kWadNameBytes);
    if (!lump.isEmpty()) {
      std::memcpy(dir.data() + base + 16, lump.constData(), static_cast<size_t>(lump.size()));
    }
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = "Unable to write WAD2 directory.";
    }
    return false;
  }

  write_u32_le(&header, 4, static_cast<quint32>(dir_entries.size()));
  write_u32_le(&header, 8, dir_offset);
  if (!out.seek(0) || out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to update WAD2 header.";
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize destination WAD2.";
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

  if (mode_ == Mode::ExistingPak && archive_.is_loaded() &&
      archive_.format() != Archive::Format::Zip &&
      archive_.format() != Archive::Format::Directory) {
    if (error) {
      *error = "Saving as ZIP-based formats is only supported when the source is a ZIP-based archive or a folder.";
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

  const bool have_src_dir = archive_.is_loaded() &&
    archive_.format() == Archive::Format::Directory &&
    !archive_.path().isEmpty();

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

  QVector<AddedFile> dir_files;
  if (have_src_dir) {
    const QDir root_dir(archive_.path());
    dir_files.reserve(archive_.entries().size());
    for (const ArchiveEntry& e : archive_.entries()) {
      const QString name = normalize_pak_path(e.name);
      if (name.isEmpty()) {
        continue;
      }
      if (added_index_by_name_.contains(name)) {
        continue;
      }
      AddedFile f;
      f.pak_name = name;
      f.source_path = root_dir.filePath(QString(name).replace('/', QDir::separator()));
      f.mtime_utc_secs = e.mtime_utc_secs;
      dir_files.push_back(std::move(f));
    }
  }

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

  auto add_disk_file = [&](const AddedFile& f) -> bool {
    const QString name = normalize_pak_path(f.pak_name);
    if (name.isEmpty()) {
      return true;
    }
    if (is_deleted_path(name)) {
      return true;
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

    return true;
  };

  for (const AddedFile& f : dir_files) {
    if (!add_disk_file(f)) {
      return false;
    }
  }
  for (const AddedFile& f : added_files_) {
    if (!add_disk_file(f)) {
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
  // Leaving any mounted container view when (re)loading the outer archive.
  mounted_archives_.clear();

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
    if (archive_.is_loaded() && archive_.format() == Archive::Format::Directory) {
      root = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
    } else {
      root = info.fileName().isEmpty() ? "PAK" : info.fileName();
    }
  } else if (!pak_path_.isEmpty()) {
    const QFileInfo info(pak_path_);
    root = info.fileName().isEmpty() ? "PAK" : info.fileName();
  } else {
    root = "New PAK";
  }

  QStringList crumbs;
  crumbs.push_back(root);
  for (const MountedArchiveLayer& layer : mounted_archives_) {
    crumbs.push_back(layer.mount_name.isEmpty() ? "Archive" : layer.mount_name);
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

  const bool can_edit = is_editable();
  const bool wad_flat = archive_.is_loaded() && archive_.format() == Archive::Format::Wad && !is_wad_mounted();
  if (add_files_action_) {
    add_files_action_->setEnabled(can_edit);
  }
  if (add_folder_action_) {
    add_folder_action_->setEnabled(can_edit && !wad_flat);
  }
  if (new_folder_action_) {
    new_folder_action_->setEnabled(can_edit && !wad_flat);
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
  if (!is_wad_mounted()) {
    added_sizes.reserve(added_files_.size());
    added_sources.reserve(added_files_.size());
    added_mtimes.reserve(added_files_.size());
    for (const AddedFile& f : added_files_) {
      added_sizes.insert(f.pak_name, f.size);
      added_sources.insert(f.pak_name, f.source_path);
      added_mtimes.insert(f.pak_name, f.mtime_utc_secs);
    }
  }

  qint64 fallback_mtime_utc_secs = -1;
  {
    const QString archive_path = view_archive().path();
    if (!archive_path.isEmpty()) {
      const QFileInfo info(archive_path);
      if (info.exists()) {
        fallback_mtime_utc_secs = info.lastModified().toUTC().toSecsSinceEpoch();
      }
    }
  }

  const QVector<ArchiveEntry> empty_entries;
  const QVector<ChildListing> children =
    list_children(view_archive().is_loaded() ? view_archive().entries() : empty_entries,
                  added_sizes,
                  added_sources,
                  added_mtimes,
                  is_wad_mounted() ? QSet<QString>{} : virtual_dirs_,
                  is_wad_mounted() ? QSet<QString>{} : deleted_files_,
                  is_wad_mounted() ? QSet<QString>{} : deleted_dir_prefixes_,
                  fallback_mtime_utc_secs,
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
  int bsp_count = 0;
  for (const ChildListing& child : children) {
    if (child.is_dir) {
      continue;
    }
    ++file_count;
    if (is_image_file_name(child.name) || is_sprite_file_name(child.name)) {
      ++image_count;
    }
    if (is_video_file_name(child.name)) {
      ++video_count;
    }
    if (is_model_file_name(child.name)) {
      ++model_count;
    }
    if (is_bsp_file_name(child.name)) {
      ++bsp_count;
    }
  }

  if (view_mode_ == ViewMode::Auto) {
    apply_auto_view(file_count, image_count, video_count, model_count, bsp_count);
  } else {
    effective_view_ = view_mode_;
  }

  update_view_controls();

  const QIcon dir_icon = style()->standardIcon(QStyle::SP_DirIcon);
  const QIcon file_icon = style()->standardIcon(QStyle::SP_FileIcon);
  const QIcon audio_icon = style()->standardIcon(QStyle::SP_MediaVolume);
  const QSize details_icon_size = (details_view_ && details_view_->iconSize().isValid()) ? details_view_->iconSize() : QSize(24, 24);
  const QIcon bik_icon = make_badged_icon(file_icon, QSize(32, 32), "BIK", palette());
  const QIcon cfg_icon = make_badged_icon(file_icon, QSize(32, 32), "{}", palette());
  const QIcon wad_base = make_archive_icon(file_icon, QSize(32, 32), palette());
  const QIcon wad_icon = make_badged_icon(wad_base, QSize(32, 32), "WAD", palette());
  const QIcon archive_icon = make_badged_icon(wad_base, QSize(32, 32), "ARC", palette());
  const QIcon model_icon = make_badged_icon(file_icon, QSize(32, 32), "3D", palette());
  const QIcon sprite_icon = make_badged_icon(file_icon, QSize(32, 32), "SPR", palette());

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
      detail_items_by_path_.insert(full_path, item);
      if (child.is_dir) {
        item->setIcon(0, dir_icon);
      } else {
        const QString leaf = child.name;
        const QString ext = file_ext_lower(leaf);
        QIcon assoc_icon;
        if (try_file_association_icon(leaf, details_icon_size, &assoc_icon)) {
          item->setIcon(0, assoc_icon);
        } else if (ext == "bik") {
          item->setIcon(0, bik_icon);
        } else if (is_supported_audio_file(leaf)) {
          item->setIcon(0, audio_icon);
        } else if (is_mountable_archive_ext(ext)) {
          item->setIcon(0, is_wad_archive_ext(ext) ? wad_icon : archive_icon);
        } else if (is_model_file_name(leaf)) {
          item->setIcon(0, model_icon);
        } else if (is_sprite_file_name(leaf)) {
          item->setIcon(0, sprite_icon);
        } else if (is_cfg_like_text_ext(ext)) {
          item->setIcon(0, cfg_icon);
        } else {
          item->setIcon(0, file_icon);
        }
        if (is_sprite_file_name(leaf)) {
          queue_thumbnail(full_path, leaf, child.source_path, static_cast<qint64>(child.size), details_icon_size);
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

      Qt::ItemFlags flags = item->flags() | Qt::ItemIsDragEnabled;
      if (child.is_dir) {
        flags |= Qt::ItemIsDropEnabled;
      }
      item->setFlags(flags);

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
    const QIcon bik_icon = make_badged_icon(file_icon, icon_size, "BIK", palette());
    const QIcon cfg_icon = make_badged_icon(file_icon, icon_size, "{}", palette());
    const QIcon wad_base = make_archive_icon(file_icon, icon_size, palette());
    const QIcon wad_icon = make_badged_icon(wad_base, icon_size, "WAD", palette());
    const QIcon archive_icon = make_badged_icon(wad_base, icon_size, "ARC", palette());
    const QIcon model_icon = make_badged_icon(file_icon, icon_size, "3D", palette());
    const QIcon sprite_icon = make_badged_icon(file_icon, icon_size, "SPR", palette());

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
        QIcon assoc_icon;
        if (try_file_association_icon(leaf, icon_size, &assoc_icon)) {
          icon = assoc_icon;
        } else if (ext == "bik") {
          icon = bik_icon;
        } else if (is_supported_audio_file(leaf)) {
          icon = audio_icon;
        } else if (is_mountable_archive_ext(ext)) {
          icon = is_wad_archive_ext(ext) ? wad_icon : archive_icon;
        } else if (is_model_file_name(leaf)) {
          icon = model_icon;
        } else if (is_sprite_file_name(leaf)) {
          icon = sprite_icon;
        } else if (is_cfg_like_text_ext(ext)) {
          icon = cfg_icon;
        }

        if (is_model_file_name(leaf) && want_thumbs) {
          // Thumbnail will be set asynchronously.
          queue_thumbnail(full_path, leaf, child.source_path, static_cast<qint64>(child.size), icon_size);
        } else if (is_sprite_file_name(leaf)) {
          queue_thumbnail(full_path, leaf, child.source_path, static_cast<qint64>(child.size), icon_size);
        } else if (is_bsp_file_name(leaf) && want_thumbs) {
          // Thumbnail will be set asynchronously.
          queue_thumbnail(full_path, leaf, child.source_path, static_cast<qint64>(child.size), icon_size);
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

      Qt::ItemFlags flags = item->flags() | Qt::ItemIsDragEnabled;
      if (child.is_dir) {
        flags |= Qt::ItemIsDropEnabled;
      }
      item->setFlags(flags);

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

  const QString pak_path = normalize_pak_path(pak_path_in);
  if (pak_path.isEmpty()) {
    if (error) {
      *error = "Invalid container path.";
    }
    return false;
  }

  const QString leaf = pak_leaf_name(pak_path);
  if (!is_mountable_archive_file_name(leaf)) {
    if (error) {
      *error = "Not a supported container file.";
    }
    return false;
  }

  QString mounted_fs_path;

  // Prefer an overridden/added source file when present.
  const int added_idx = added_index_by_name_.value(pak_path, -1);
  if (!is_wad_mounted() && added_idx >= 0 && added_idx < added_files_.size()) {
    mounted_fs_path = added_files_[added_idx].source_path;
  } else {
    QString err;
    if (!export_path_to_temp(pak_path, false, &mounted_fs_path, &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to export container for viewing." : err;
      }
      return false;
    }
  }

  if (mounted_fs_path.isEmpty() || !QFileInfo::exists(mounted_fs_path)) {
    if (error) {
      *error = "Unable to locate container file on disk.";
    }
    return false;
  }

  auto inner = std::make_unique<Archive>();
  QString load_err;
  if (!inner->load(mounted_fs_path, &load_err) || !inner->is_loaded() || inner->format() == Archive::Format::Unknown ||
      inner->format() == Archive::Format::Directory) {
    if (error) {
      *error = load_err.isEmpty() ? "Unable to open container." : load_err;
    }
    return false;
  }

  MountedArchiveLayer layer;
  layer.archive = std::move(inner);
  layer.mount_name = leaf;
  layer.mount_fs_path = mounted_fs_path;
  layer.outer_dir_before_mount = current_dir_;
  mounted_archives_.push_back(std::move(layer));

  set_current_dir({});
  return true;
}

void PakTab::unmount_wad() {
  if (!is_wad_mounted()) {
    return;
  }

  const QStringList restore = mounted_archives_.back().outer_dir_before_mount;
  mounted_archives_.pop_back();
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
  // If we're mounted into a container, prefer outer archives for palette lookup.
  if (is_wad_mounted()) {
    for (int i = static_cast<int>(mounted_archives_.size()) - 2; i >= 0; --i) {
      if (try_archive(*mounted_archives_[i].archive, "Outer Mounted Archive")) {
        return true;
      }
    }
    if (try_archive(archive_, "Outer Archive")) {
      return true;
    }
  }
  if (try_archive(view_archive(), is_wad_mounted() ? "Mounted Archive" : "Current Archive")) {
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
  // If we're mounted into a container, prefer outer archives for palette lookup.
  if (is_wad_mounted()) {
    for (int i = static_cast<int>(mounted_archives_.size()) - 2; i >= 0; --i) {
      if (try_archive_palette(*mounted_archives_[i].archive, "Outer Mounted Archive")) {
        return true;
      }
    }
    if (try_archive_palette(archive_, "Outer Archive")) {
      return true;
    }
  }
  if (try_archive_palette(view_archive(), is_wad_mounted() ? "Mounted Archive" : "Current Archive")) {
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
    preview_->set_current_file_info({}, -1, -1);
    preview_->show_message("Insights", load_error_.isEmpty() ? "PAK is not loaded." : load_error_);
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
      preview_->set_current_file_info({}, -1, -1);
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
      preview_->set_current_file_info({}, -1, -1);
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

  preview_->set_current_file_info(pak_path, size, mtime);

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
  if (is_mountable_archive_ext(ext)) {
    QString type = "Archive container";
    if (is_quake_wad_archive_ext(ext)) {
      type = "Quake WAD archive";
    } else if (ext == "wad") {
      if (view_archive().format() == Archive::Format::Wad) {
        type = view_archive().is_doom_wad() ? "Doom IWAD/PWAD archive" : "WAD archive container";
      } else {
        type = "WAD archive container";
      }
    } else if (ext == "resources") {
      type = "Doom 3 BFG resources container";
    }
    preview_->show_message(leaf.isEmpty() ? "Archive" : leaf,
                           type + ". Double-click to open.");
    return;
  }

  if (view_archive().format() == Archive::Format::Wad && view_archive().is_doom_wad()) {
    const QString wanted = normalize_pak_path(pak_path);
    const QVector<ArchiveEntry>& entries = view_archive().entries();
    int selected_index = -1;
    for (int i = 0; i < entries.size(); ++i) {
      if (normalize_pak_path(entries[i].name) == wanted) {
        selected_index = i;
        break;
      }
    }
    const int marker_index = find_doom_map_marker_index_for_lump(entries, selected_index);
    if (marker_index >= 0) {
      QString map_err;
      const QString summary = build_doom_map_summary(entries, marker_index, &map_err);
      if (!summary.isEmpty()) {
        preview_->show_text(leaf, subtitle, summary);
        return;
      }
    }
  }

  const bool is_audio = is_supported_audio_file(leaf);
  const bool is_video = is_video_file_name(leaf);
  const bool is_model = is_model_file_name(leaf);
  const bool is_bsp = is_bsp_file_name(leaf);

  QString source_path;
  const int added_idx = added_index_by_name_.value(normalize_pak_path(pak_path), -1);
  if (added_idx >= 0 && added_idx < added_files_.size()) {
    source_path = added_files_[added_idx].source_path;
  }

  if (is_image_file_name(leaf)) {
    ImageDecodeOptions decode_options;
    const bool supports_mips = (ext == "wal" || ext == "swl" || ext == "mip");
    if (preview_) {
      preview_->set_image_mip_controls(supports_mips, preview_->image_mip_level());
    }
    decode_options.mip_level = supports_mips && preview_ ? preview_->image_mip_level() : 0;
    if (ext == "wal") {
      QString pal_err;
      if (!ensure_quake2_palette(&pal_err)) {
        preview_->show_message(leaf, pal_err.isEmpty() ? "Unable to locate Quake II palette required for WAL preview." : pal_err);
        return;
      }
      decode_options.palette = &quake2_palette_;
    }
    if (ext == "lmp") {
      QString pal_err;
      if (ensure_quake1_palette(&pal_err)) {
        decode_options.palette = &quake1_palette_;
      }
    }
    if (ext == "mip") {
      QString pal_err;
      if (ensure_quake1_palette(&pal_err)) {
        decode_options.palette = &quake1_palette_;
      }
    }

    const bool allow_glow = is_quake2_game(game_id_);
    const auto apply_glow_from_file = [&](const QString& path, const QImage& base_image) -> QImage {
      if (!allow_glow) {
        return base_image;
      }
      const QString glow_path = glow_path_for_fs(path);
      if (glow_path.isEmpty() || !QFileInfo::exists(glow_path)) {
        return base_image;
      }
      const ImageDecodeResult glow_decoded = decode_image_file(glow_path, ImageDecodeOptions{});
      if (!glow_decoded.ok()) {
        return base_image;
      }
      return apply_glow_overlay(base_image, glow_decoded.image);
    };

    if (!source_path.isEmpty()) {
      const ImageDecodeResult decoded = decode_image_file(source_path, decode_options);
      if (!decoded.ok()) {
        preview_->show_message(leaf, decoded.error.isEmpty() ? "Unable to load this image file." : decoded.error);
        return;
      }
      const QImage out = apply_glow_from_file(source_path, decoded.image);
      preview_->show_image(leaf, subtitle, out);
      return;
    }

    QByteArray bytes;
    QString err;
    constexpr qint64 kMaxImageBytes = 32LL * 1024 * 1024;
    if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, kMaxImageBytes)) {
      preview_->show_message(leaf, err.isEmpty() ? "Unable to read image from PAK." : err);
      return;
    }
    ImageDecodeResult decoded = decode_image_bytes(bytes, leaf, decode_options);
    if (!decoded.ok()) {
      preview_->show_message(leaf, decoded.error.isEmpty() ? "Unable to decode this image format." : decoded.error);
      return;
    }

    QImage image = decoded.image;
    if (allow_glow) {
      const QString glow_pak = glow_path_for_pak(pak_path);
      if (!glow_pak.isEmpty()) {
        QHash<QString, QString> by_lower;
        by_lower.reserve(view_archive().entries().size() + (is_wad_mounted() ? 0 : added_files_.size()));
        for (const ArchiveEntry& e : view_archive().entries()) {
          const QString n = normalize_pak_path(e.name);
          if (!n.isEmpty()) {
            by_lower.insert(n.toLower(), e.name);
          }
        }
        if (!is_wad_mounted()) {
          for (const AddedFile& f : added_files_) {
            const QString n = normalize_pak_path(f.pak_name);
            if (!n.isEmpty()) {
              by_lower.insert(n.toLower(), f.pak_name);
            }
          }
        }

        const QString key = normalize_pak_path(glow_pak).toLower();
        const QString found = key.isEmpty() ? QString() : by_lower.value(key);
        if (!found.isEmpty()) {
          const int glow_added_idx = added_index_by_name_.value(normalize_pak_path(found), -1);
          if (glow_added_idx >= 0 && glow_added_idx < added_files_.size()) {
            const ImageDecodeResult glow_decoded =
              decode_image_file(added_files_[glow_added_idx].source_path, ImageDecodeOptions{});
            if (glow_decoded.ok()) {
              image = apply_glow_overlay(image, glow_decoded.image);
            }
          } else {
            QByteArray glow_bytes;
            QString glow_err;
            constexpr qint64 kMaxGlowBytes = 32LL * 1024 * 1024;
            if (view_archive().read_entry_bytes(found, &glow_bytes, &glow_err, kMaxGlowBytes)) {
              const ImageDecodeResult glow_decoded = decode_image_bytes(glow_bytes, QFileInfo(found).fileName(), ImageDecodeOptions{});
              if (glow_decoded.ok()) {
                image = apply_glow_overlay(image, glow_decoded.image);
              }
            }
          }
        }
      }
    }

    preview_->show_image(leaf, subtitle, image);
    return;
  }

  if (is_video) {
    QString video_path = source_path;
    if (video_path.isEmpty()) {
      QString err;
      if (!export_path_to_temp(pak_path, false, &video_path, &err)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to export video for preview." : err);
        return;
      }
    }
    if (video_path.isEmpty()) {
      preview_->show_message(leaf, "Unable to export video for preview.");
      return;
    }

    const bool is_cinematic = (ext == "cin" || ext == "roq");
    if (is_cinematic) {
      preview_->show_cinematic_from_file(leaf, subtitle, video_path);
    } else {
      preview_->show_video_from_file(leaf, subtitle, video_path);
    }
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

  if (is_model) {
    QString model_path = source_path;
    QString skin_path;

    const auto file_base_name = [](const QString& name) -> QString {
      const int dot = name.lastIndexOf('.');
      return dot >= 0 ? name.left(dot) : name;
    };

    const QString model_base = file_base_name(leaf);
    const QString model_ext = ext;

    const auto score_skin = [&](const QString& skin_leaf) -> int {
      const QString skin_ext = file_ext_lower(skin_leaf);
      const QString base = file_base_name(skin_leaf);
      const QString base_lower = base.toLower();
      const QString model_base_lower = model_base.toLower();

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
      if (base.contains("default", Qt::CaseInsensitive)) {
        score += 30;
      }
      if (base.endsWith("_glow", Qt::CaseInsensitive)) {
        score -= 200;
      }

      // Prefer Quake III-family .skin files for MD3/MDC/MDR models.
      if ((model_ext == "md3" || model_ext == "mdc" || model_ext == "mdr") && skin_ext == "skin") {
        score += 160;
      }

      // Quake MDL skins in rerelease/community packs often use model_XX_YY naming.
      if (model_ext == "mdl" && !model_base_lower.isEmpty()) {
        const QString mdl_prefix = model_base_lower + "_";
        if (base_lower == model_base_lower + "_00_00") {
          score += 220;
        } else if (base_lower.startsWith(mdl_prefix)) {
          const QString suffix = base_lower.mid(mdl_prefix.size());
          const bool two_by_two_numeric = (suffix.size() == 5 && suffix[2] == '_' && suffix[0].isDigit() &&
                                           suffix[1].isDigit() && suffix[3].isDigit() && suffix[4].isDigit());
          score += two_by_two_numeric ? 180 : 120;
        }
      }

      if (skin_ext == "png") {
        score += 20;
      } else if (skin_ext == "tga") {
        score += 18;
      } else if (skin_ext == "jpg" || skin_ext == "jpeg") {
        score += 16;
      } else if (skin_ext == "ftx") {
        score += 21;
      } else if (skin_ext == "lmp") {
        score += (model_ext == "mdl") ? 26 : 12;
      } else if (skin_ext == "mip") {
        score += (model_ext == "mdl") ? 24 : 11;
      } else if (skin_ext == "pcx") {
        score += 14;
      } else if (skin_ext == "wal") {
        score += 12;
      } else if (skin_ext == "swl") {
        score += 12;
      } else if (skin_ext == "dds") {
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

      QStringList filters = {"*.png", "*.tga", "*.jpg", "*.jpeg", "*.pcx", "*.wal", "*.swl", "*.dds", "*.lmp", "*.mip", "*.ftx"};
      if (model_ext == "md3" || model_ext == "mdc" || model_ext == "mdr") {
        filters.push_back("*.skin");
      }
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
      if (best_score < 40) {
        return {};
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
        const QString leaf_ext = file_ext_lower(leaf_name);
        const bool is_q3_skin = ((model_ext == "md3" || model_ext == "mdc" || model_ext == "mdr") && leaf_ext == "skin");
        if (!is_image_file_name(leaf_name) && !is_q3_skin) {
          return;
        }
        candidates.push_back(Candidate{p, leaf_name, score_skin(leaf_name)});
      };

      for (const ArchiveEntry& e : view_archive().entries()) {
        consider(e.name);
      }
      if (!is_wad_mounted()) {
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

      if (candidates.first().score < 40) {
        return {};
      }
      return candidates.first().pak_path;
    };

    if (model_path.isEmpty()) {
      QString err;
      if (!export_path_to_temp(pak_path, false, &model_path, &err)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to export model for preview." : err);
        return;
      }

      const QString op_dir = QFileInfo(model_path).absolutePath();

      auto extract_entry_to_model_dir = [&](const QString& found_entry) -> QString {
        const QString entry_leaf = pak_leaf_name(found_entry);
        if (entry_leaf.isEmpty()) {
          return {};
        }
        const QString dest = QDir(op_dir).filePath(entry_leaf);
        if (QFileInfo::exists(dest)) {
          return dest;
        }

        QString tex_err;
        const int tex_added_idx = added_index_by_name_.value(normalize_pak_path(found_entry), -1);
        if (tex_added_idx >= 0 && tex_added_idx < added_files_.size()) {
          if (copy_file_stream(added_files_[tex_added_idx].source_path, dest, &tex_err)) {
            return dest;
          }
          return {};
        }
        if (view_archive().extract_entry_to_file(found_entry, dest, &tex_err)) {
          return dest;
        }
        return QFileInfo::exists(dest) ? dest : QString();
      };

      auto find_entry_ci_slow = [&](const QString& want) -> QString {
        const QString key = normalize_pak_path(want).toLower();
        if (key.isEmpty()) {
          return {};
        }
        for (const ArchiveEntry& e : view_archive().entries()) {
          const QString n = normalize_pak_path(e.name);
          if (!n.isEmpty() && n.toLower() == key) {
            return e.name;
          }
        }
        if (!is_wad_mounted()) {
          for (const AddedFile& f : added_files_) {
            const QString n = normalize_pak_path(f.pak_name);
            if (!n.isEmpty() && n.toLower() == key) {
              return f.pak_name;
            }
          }
        }
        return {};
      };

      // Try to find and export a skin from the same folder in the archive.
      const QString skin_pak = find_skin_in_archive(pak_path);
      if (!skin_pak.isEmpty()) {
        skin_path = extract_entry_to_model_dir(skin_pak);

        if (!skin_path.isEmpty() && is_quake2_game(game_id_)) {
          const QString skin_ext = file_ext_lower(QFileInfo(skin_path).fileName());
          if (skin_ext != "skin") {
            const QString glow_candidate = glow_path_for_pak(skin_pak);
            const QString glow_found = glow_candidate.isEmpty() ? QString() : find_entry_ci_slow(glow_candidate);
            if (!glow_found.isEmpty()) {
              (void)extract_entry_to_model_dir(glow_found);
            }
          }
        }
      }

      // For multi-surface formats, try to extract per-surface textures referenced by the model so the model viewer can
      // auto-load them from the exported temp directory.
      if (ext == "md3" || ext == "mdc" || ext == "md4" || ext == "mdr" || ext == "skb" || ext == "skd" || ext == "mdm" || ext == "glm" || ext == "md5mesh" || ext == "iqm" || ext == "tan" || ext == "obj" || ext == "lwo") {
        const QString normalized_model = normalize_pak_path(pak_path);
        const int slash = normalized_model.lastIndexOf('/');
        const QString model_dir_prefix = (slash >= 0) ? normalized_model.left(slash + 1) : QString();

        const QStringList img_exts = {"png", "tga", "jpg", "jpeg", "pcx", "wal", "swl", "dds", "lmp", "mip", "ftx"};

        // Build a quick case-insensitive lookup across the currently-viewed archive + added files.
        QHash<QString, QString> by_lower;
        by_lower.reserve(view_archive().entries().size() + (is_wad_mounted() ? 0 : added_files_.size()));
        for (const ArchiveEntry& e : view_archive().entries()) {
          const QString n = normalize_pak_path(e.name);
          if (!n.isEmpty()) {
            by_lower.insert(n.toLower(), e.name);
          }
        }
        if (!is_wad_mounted()) {
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

        auto extract_first_existing = [&](const QVector<QString>& wants) -> QString {
          for (const QString& want : wants) {
            const QString found = find_entry_ci(want);
            if (found.isEmpty()) {
              continue;
            }
            const QString extracted = extract_entry_to_model_dir(found);
            if (!extracted.isEmpty()) {
              return extracted;
            }
          }
          return {};
        };

        if (ext == "mdm") {
          const QString model_leaf = pak_leaf_name(normalized_model);
          const QString base = QFileInfo(model_leaf).completeBaseName();
          QVector<QString> wants;
          wants.reserve(4);
          if (!base.isEmpty()) {
            wants.push_back(model_dir_prefix + base + ".mdx");
            wants.push_back(model_dir_prefix + base + ".MDX");
            wants.push_back(base + ".mdx");
            wants.push_back(base + ".MDX");
            (void)extract_first_existing(wants);
          }
        }

        if (ext == "glm") {
          QSet<QString> wants;
          wants.reserve(8);
          const auto add_want = [&](QString p) {
            p = normalize_pak_path(p);
            if (!p.isEmpty()) {
              wants.insert(p);
            }
          };

          const QString model_leaf = pak_leaf_name(normalized_model);
          const QString model_base = QFileInfo(model_leaf).completeBaseName();
          if (!model_base.isEmpty()) {
            add_want(model_dir_prefix + model_base + ".gla");
          }

          QFile glm_file(model_path);
          if (glm_file.open(QIODevice::ReadOnly)) {
            const QByteArray hdr = glm_file.read(164);
            if (hdr.size() >= 136) {
              const QByteArray anim_raw = hdr.mid(72, 64);
              int nul = anim_raw.indexOf('\0');
              if (nul < 0) {
                nul = anim_raw.size();
              }
              QString anim_name = QString::fromLatin1(anim_raw.constData(), nul).trimmed();
              anim_name.replace('\\', '/');
              while (anim_name.startsWith('/')) {
                anim_name.remove(0, 1);
              }
              if (!anim_name.isEmpty()) {
                if (!anim_name.endsWith(".gla", Qt::CaseInsensitive)) {
                  anim_name += ".gla";
                }
                add_want(anim_name);
                add_want(model_dir_prefix + anim_name);
                add_want(model_dir_prefix + QFileInfo(anim_name).fileName());
              }
            }
          }

          for (const QString& want : wants) {
            const QString found = find_entry_ci(want);
            if (found.isEmpty()) {
              continue;
            }
            (void)extract_entry_to_model_dir(found);
          }
        }

        auto extract_glow_for_entry = [&](const QString& found_entry) -> void {
          if (!is_quake2_game(game_id_)) {
            return;
          }
          const QString lower = found_entry.toLower();
          if (lower.endsWith("_glow.png")) {
            return;
          }
          const QString glow_candidate = glow_path_for_pak(found_entry);
          if (glow_candidate.isEmpty()) {
            return;
          }
          const QString glow_found = find_entry_ci(glow_candidate);
          if (glow_found.isEmpty()) {
            return;
          }
          extract_entry_to_model_dir(glow_found);
        };

        // If we exported an OBJ, try to extract its referenced .mtl files first so the OBJ loader can resolve
        // per-surface texture paths.
        if (ext == "obj") {
          QFile obj_file(model_path);
          if (obj_file.open(QIODevice::ReadOnly)) {
            QSet<QString> extracted_mtl_lower;
            extracted_mtl_lower.reserve(8);
            while (!obj_file.atEnd()) {
              QString line = QString::fromLatin1(obj_file.readLine());
              const int hash = line.indexOf('#');
              if (hash >= 0) {
                line = line.left(hash);
              }
              line = line.trimmed();
              if (line.isEmpty()) {
                continue;
              }
              if (!line.startsWith("mtllib", Qt::CaseInsensitive)) {
                continue;
              }
              QString rest = line.mid(6).trimmed().simplified();
              if (rest.isEmpty()) {
                continue;
              }
              const QStringList refs = rest.split(' ', Qt::SkipEmptyParts);
              for (const QString& ref0 : refs) {
                QString ref = ref0.trimmed();
                if (ref.isEmpty()) {
                  continue;
                }
                ref.replace('\\', '/');
                while (ref.startsWith('/')) {
                  ref.remove(0, 1);
                }

                const QString leaf = QFileInfo(ref).fileName();
                const QString ext_name = file_ext_lower(leaf);

                QVector<QString> candidates;
                candidates.reserve(8);
                auto add = [&](const QString& c) {
                  if (!c.isEmpty()) {
                    candidates.push_back(c);
                  }
                };

                add(ref);
                add(model_dir_prefix + ref);
                add(leaf);
                add(model_dir_prefix + leaf);

                if (ext_name.isEmpty()) {
                  add(ref + ".mtl");
                  add(model_dir_prefix + ref + ".mtl");
                  add(leaf + ".mtl");
                  add(model_dir_prefix + leaf + ".mtl");
                }

                for (const QString& want : candidates) {
                  const QString found = find_entry_ci(want);
                  if (found.isEmpty()) {
                    continue;
                  }
                  const QString leaf_lower = pak_leaf_name(found).toLower();
                  if (leaf_lower.isEmpty() || extracted_mtl_lower.contains(leaf_lower)) {
                    continue;
                  }
                  extracted_mtl_lower.insert(leaf_lower);
                  extract_entry_to_model_dir(found);
                  break;
                }
              }
            }
          }
        }

        QString model_err;
        const std::optional<LoadedModel> loaded_model = load_model_file(model_path, &model_err);
        if (loaded_model && !loaded_model->surfaces.isEmpty()) {
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
            const QString dir_name = QDir::cleanPath(sfi.path()).replace('\\', '/');

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
              if (!model_dir_prefix.isEmpty() && !sh.startsWith(model_dir_prefix)) {
                add_candidate(model_dir_prefix + sh);
              }

              // Some assets ship as .jpg/.png even when the shader reference uses .tga.
              if (!base_name.isEmpty()) {
                if (!dir_name.isEmpty() && dir_name != ".") {
                  for (const QString& e : img_exts) {
                    add_candidate(QString("%1/%2.%3").arg(dir_name, base_name, e));
                    if (!model_dir_prefix.isEmpty()) {
                      add_candidate(QString("%1%2/%3.%4").arg(model_dir_prefix, dir_name, base_name, e));
                    }
                  }
                }
                for (const QString& e : img_exts) {
                  add_candidate(QString("%1.%2").arg(base_name, e));
                    add_candidate(QString("%1%2.%3").arg(model_dir_prefix, base_name, e));
                }
              }
            } else {
              for (const QString& e : img_exts) {
                const QString cand = QString("%1.%2").arg(sh, e);
                add_candidate(cand);
                if (!model_dir_prefix.isEmpty() && !cand.startsWith(model_dir_prefix)) {
                  add_candidate(model_dir_prefix + cand);
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
            extract_glow_for_entry(found);
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

          // If we exported a Quake III-family .skin file, also extract textures referenced by it.
          if ((ext == "md3" || ext == "mdc" || ext == "mdr") && !skin_path.isEmpty() && file_ext_lower(QFileInfo(skin_path).fileName()) == "skin") {
            Quake3SkinMapping mapping;
            QString skin_err;
            if (parse_quake3_skin_file(skin_path, &mapping, &skin_err) && !mapping.surface_to_shader.isEmpty()) {
              for (auto it = mapping.surface_to_shader.cbegin(); it != mapping.surface_to_shader.cend(); ++it) {
                const QString shader = it.value();
                if (shader.isEmpty()) {
                  continue;
                }
                consider_shader(shader);
                if (extracted_lower.size() >= 32) {
                  break;
                }
              }
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
    (void)ensure_quake1_palette(nullptr);
    (void)ensure_quake2_palette(nullptr);
    preview_->set_model_palettes(quake1_palette_, quake2_palette_);
    preview_->show_model_from_file(leaf, subtitle, model_path, skin_path);
    return;
  }

  if (is_bsp) {
    BspMesh mesh;
    QString err;
    bool ok = false;
    QByteArray bsp_bytes;
    if (!source_path.isEmpty()) {
      QFile f(source_path);
      if (!f.open(QIODevice::ReadOnly)) {
        preview_->show_message(leaf, "Unable to open BSP file.");
        return;
      }
      constexpr qint64 kMaxBspBytes = 128LL * 1024 * 1024;
      const qint64 size_on_disk = f.size();
      if (size_on_disk > kMaxBspBytes) {
        preview_->show_message(leaf, "BSP file is too large to preview.");
        return;
      }
      bsp_bytes = f.readAll();
      ok = load_bsp_mesh_bytes(bsp_bytes, leaf, &mesh, &err, true);
    } else {
      constexpr qint64 kMaxBspBytes = 128LL * 1024 * 1024;
      const qint64 max_bytes = (size > 0) ? std::min(size, kMaxBspBytes) : kMaxBspBytes;
      if (!view_archive().read_entry_bytes(pak_path, &bsp_bytes, &err, max_bytes)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to read BSP from archive." : err);
        return;
      }
      ok = load_bsp_mesh_bytes(bsp_bytes, leaf, &mesh, &err, true);
    }

    if (!ok) {
      preview_->show_message(leaf, err.isEmpty() ? "Unable to render BSP preview." : err);
      return;
    }
    QHash<QString, QImage> textures;
    if (view_archive().is_loaded()) {
      (void)ensure_quake1_palette(nullptr);
      (void)ensure_quake2_palette(nullptr);

      if (!bsp_bytes.isEmpty()) {
        QHash<QString, QImage> embedded = extract_bsp_embedded_textures_bytes(bsp_bytes, quake1_palette_.size() == 256 ? &quake1_palette_ : nullptr);
        for (auto it = embedded.begin(); it != embedded.end(); ++it) {
          textures.insert(it.key().toLower(), it.value());
        }
      }

      QSet<QString> wanted;
      for (const BspMeshSurface& s : mesh.surfaces) {
        if (!s.texture.isEmpty()) {
          wanted.insert(s.texture);
        }
      }

      if (!wanted.isEmpty()) {
        const BspFamily bsp_family = bsp_family_bytes(bsp_bytes);
        const QStringList exts_q3 = {"ftx", "tga", "jpg", "jpeg", "png", "dds"};
        const QStringList exts_q2 = {"wal", "swl", "png", "tga", "jpg", "jpeg", "dds"};
        const QStringList exts_q1 = {"mip", "lmp", "pcx", "png", "tga", "jpg", "jpeg"};

        QHash<QString, QString> by_lower;
        by_lower.reserve(view_archive().entries().size() + (is_wad_mounted() ? 0 : added_files_.size()));
        for (const ArchiveEntry& e : view_archive().entries()) {
          const QString n = normalize_pak_path(e.name);
          if (!n.isEmpty()) {
            by_lower.insert(n.toLower(), e.name);
          }
        }
        if (!is_wad_mounted()) {
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

        auto decode_texture = [&](const QByteArray& bytes, const QString& name) -> ImageDecodeResult {
          const QString ext = file_ext_lower(name);
          if (ext == "wal") {
            if (quake2_palette_.size() != 256) {
              return ImageDecodeResult{QImage(), "Missing Quake II palette for WAL."};
            }
            QString wal_err;
            QImage img = decode_wal_image(bytes, quake2_palette_, 0, name, &wal_err);
            return ImageDecodeResult{std::move(img), wal_err};
          }
          if (ext == "mip") {
            QString mip_err;
            QImage img = decode_miptex_image(bytes, quake1_palette_.size() == 256 ? &quake1_palette_ : nullptr, 0, name, &mip_err);
            return ImageDecodeResult{std::move(img), mip_err};
          }
          ImageDecodeOptions opts;
          if (ext == "lmp" && quake1_palette_.size() == 256) {
            opts.palette = &quake1_palette_;
          }
          ImageDecodeResult decoded = decode_image_bytes(bytes, name, opts);
          if (!decoded.ok() && opts.palette) {
            decoded = decode_image_bytes(bytes, name, {});
          }
          return decoded;
        };

        constexpr qint64 kMaxTexBytes = 64LL * 1024 * 1024;

        QSet<QString> attempted;
        attempted.reserve(wanted.size());

        for (const QString& tex : wanted) {
          const QString tex_key = tex.toLower();
          if (attempted.contains(tex_key)) {
            continue;
          }
          attempted.insert(tex_key);
          QString name = tex;
          name.replace('\\', '/');
          while (name.startsWith('/')) {
            name.remove(0, 1);
          }

          const QString lower = name.toLower();
          const QFileInfo info(name);
          const QString ext = info.suffix().toLower();
          const QString base = info.completeBaseName();
          const bool is_q3 = [&]() {
            for (const BspMeshSurface& s : mesh.surfaces) {
              if (s.texture == tex && s.uv_normalized) {
                return true;
              }
            }
            return false;
          }();
          const QStringList img_exts = is_q3
                                       ? exts_q3
                                       : ((bsp_family == BspFamily::Quake2) ? exts_q2 : exts_q1);
          const bool has_ext = img_exts.contains(ext);
          const bool has_textures_prefix = lower.startsWith("textures/");

          QVector<QString> candidates;
          candidates.reserve(32);
          auto add_candidate = [&](const QString& cand) {
            const QString c = normalize_pak_path(cand);
            if (!c.isEmpty()) {
              candidates.push_back(c);
            }
          };

          if (has_ext) {
            add_candidate(name);
            if (!has_textures_prefix) {
              add_candidate(QString("textures/%1").arg(name));
            }
            if (!base.isEmpty() && ext != "tga") {
              add_candidate(QString("%1.%2").arg(base, ext));
            }
          } else {
            for (const QString& e : img_exts) {
              add_candidate(QString("%1.%2").arg(name, e));
              if (!has_textures_prefix) {
                add_candidate(QString("textures/%1.%2").arg(name, e));
              }
            }
          }

          for (const QString& cand : candidates) {
            const QString found = find_entry_ci(cand);
            if (found.isEmpty()) {
              continue;
            }
            QByteArray bytes;
            QString tex_err;
            if (!view_archive().read_entry_bytes(found, &bytes, &tex_err, kMaxTexBytes)) {
              continue;
            }
            const ImageDecodeResult decoded = decode_texture(bytes, QFileInfo(found).fileName());
            if (!decoded.ok()) {
              continue;
            }
            textures.insert(tex_key, decoded.image);
            break;
          }
        }
      }
    }

    preview_->show_bsp(leaf, subtitle, std::move(mesh), std::move(textures));
    return;
  }

  if (is_supported_idtech_asset_file(leaf)) {
    constexpr qint64 kMaxAssetBytes = 128LL * 1024 * 1024;
    QByteArray bytes;
    QString err;

    if (!source_path.isEmpty()) {
      QFile f(source_path);
      if (!f.open(QIODevice::ReadOnly)) {
        preview_->show_message(leaf, "Unable to open source file for preview.");
        return;
      }
      const qint64 size_on_disk = f.size();
      if (size_on_disk > kMaxAssetBytes) {
        preview_->show_message(leaf, "Asset file is too large to inspect.");
        return;
      }
      bytes = f.readAll();
    } else {
      const qint64 max_bytes = (size > 0) ? std::min(size, kMaxAssetBytes) : kMaxAssetBytes;
      if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, max_bytes)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to read asset from archive." : err);
        return;
      }
    }

    if (is_sprite_file_name(leaf)) {
      const auto decode_options_for = [&](const QString& name) -> ImageDecodeOptions {
        ImageDecodeOptions opt;
        const QString frame_ext = file_ext_lower(name);
        if ((frame_ext == "lmp" || frame_ext == "mip") && quake1_palette_.size() == 256) {
          opt.palette = &quake1_palette_;
        } else if (frame_ext == "wal" && quake2_palette_.size() == 256) {
          opt.palette = &quake2_palette_;
        }
        return opt;
      };

      const auto decode_image_from_file_path = [&](const QString& frame_path) -> ImageDecodeResult {
        if (frame_path.isEmpty() || !QFileInfo::exists(frame_path)) {
          return ImageDecodeResult{QImage(), "Frame image file was not found."};
        }
        ImageDecodeOptions opts = decode_options_for(frame_path);
        ImageDecodeResult decoded = decode_image_file(frame_path, opts);
        if (!decoded.ok() && opts.palette) {
          decoded = decode_image_file(frame_path, {});
        }
        return decoded;
      };

      const auto decode_image_from_bytes = [&](const QByteArray& frame_bytes, const QString& frame_name) -> ImageDecodeResult {
        ImageDecodeOptions opts = decode_options_for(frame_name);
        ImageDecodeResult decoded = decode_image_bytes(frame_bytes, frame_name, opts);
        if (!decoded.ok() && opts.palette) {
          decoded = decode_image_bytes(frame_bytes, frame_name, {});
        }
        return decoded;
      };

      QVector<QImage> sprite_frames;
      QVector<int> sprite_frame_durations_ms;

      if (ext == "spr") {
        // Quake SPR needs an external palette; Half-Life SPR v2 carries an embedded palette.
        (void)ensure_quake1_palette(nullptr);
        const QVector<QRgb>* sprite_palette = (quake1_palette_.size() == 256) ? &quake1_palette_ : nullptr;
        const SpriteDecodeResult sprite = decode_spr_sprite(bytes, sprite_palette);
        if (!sprite.ok()) {
          preview_->show_message(leaf, sprite.error.isEmpty() ? "Unable to decode SPR sprite." : sprite.error);
          return;
        }
        sprite_frames.reserve(sprite.frames.size());
        sprite_frame_durations_ms.reserve(sprite.frames.size());
        for (const SpriteFrame& frame : sprite.frames) {
          if (frame.image.isNull()) {
            continue;
          }
          sprite_frames.push_back(frame.image);
          sprite_frame_durations_ms.push_back(std::clamp(frame.duration_ms, 30, 2000));
        }
      } else {
        (void)ensure_quake1_palette(nullptr);
        (void)ensure_quake2_palette(nullptr);

        const QString normalized_sprite = normalize_pak_path(pak_path);
        const int slash = normalized_sprite.lastIndexOf('/');
        const QString sprite_dir_prefix = (slash >= 0) ? normalized_sprite.left(slash + 1) : QString();

        QHash<QString, QString> by_lower;
        by_lower.reserve(view_archive().entries().size() + (is_wad_mounted() ? 0 : added_files_.size()));
        for (const ArchiveEntry& e : view_archive().entries()) {
          const QString n = normalize_pak_path(e.name);
          if (!n.isEmpty()) {
            by_lower.insert(n.toLower(), e.name);
          }
        }
        if (!is_wad_mounted()) {
          for (const AddedFile& f : added_files_) {
            const QString n = normalize_pak_path(f.pak_name);
            if (!n.isEmpty()) {
              by_lower.insert(n.toLower(), f.pak_name);
            }
          }
        }

        const Sp2FrameLoader frame_loader = [&](const QString& frame_name) -> ImageDecodeResult {
          QString ref = frame_name;
          ref.replace('\\', '/');
          while (ref.startsWith('/')) {
            ref.remove(0, 1);
          }
          const QString frame_leaf = QFileInfo(ref).fileName();

          if (!source_path.isEmpty()) {
            const QString base_dir = QFileInfo(source_path).absolutePath();
            QVector<QString> file_candidates;
            file_candidates.reserve(4);
            if (QFileInfo(ref).isAbsolute()) {
              file_candidates.push_back(ref);
            }
            if (!base_dir.isEmpty()) {
              file_candidates.push_back(QDir(base_dir).filePath(ref));
              if (!frame_leaf.isEmpty()) {
                file_candidates.push_back(QDir(base_dir).filePath(frame_leaf));
              }
            }
            for (const QString& cand : file_candidates) {
              const ImageDecodeResult decoded = decode_image_from_file_path(cand);
              if (decoded.ok()) {
                return decoded;
              }
            }
          }

          QVector<QString> candidates;
          candidates.reserve(6);
          auto add_candidate = [&](const QString& candidate) {
            const QString c = normalize_pak_path(candidate);
            if (!c.isEmpty()) {
              candidates.push_back(c);
            }
          };

          add_candidate(ref);
          if (!sprite_dir_prefix.isEmpty() && !ref.startsWith(sprite_dir_prefix)) {
            add_candidate(sprite_dir_prefix + ref);
          }
          if (!frame_leaf.isEmpty()) {
            add_candidate(frame_leaf);
            if (!sprite_dir_prefix.isEmpty()) {
              add_candidate(sprite_dir_prefix + frame_leaf);
            }
          }

          constexpr qint64 kMaxFrameBytes = 16LL * 1024 * 1024;
          for (const QString& cand : candidates) {
            const QString found = by_lower.value(cand.toLower());
            if (found.isEmpty()) {
              continue;
            }
            const int frame_added_idx = added_index_by_name_.value(normalize_pak_path(found), -1);
            if (frame_added_idx >= 0 && frame_added_idx < added_files_.size()) {
              const ImageDecodeResult decoded = decode_image_from_file_path(added_files_[frame_added_idx].source_path);
              if (decoded.ok()) {
                return decoded;
              }
              continue;
            }

            QByteArray frame_bytes;
            QString frame_err;
            if (!view_archive().read_entry_bytes(found, &frame_bytes, &frame_err, kMaxFrameBytes)) {
              continue;
            }
            const ImageDecodeResult decoded = decode_image_from_bytes(frame_bytes, QFileInfo(found).fileName());
            if (decoded.ok()) {
              return decoded;
            }
          }

          return ImageDecodeResult{QImage(), "Unable to resolve SP2 frame image."};
        };

        const SpriteDecodeResult sprite = decode_sp2_sprite(bytes, frame_loader);
        if (!sprite.ok()) {
          preview_->show_message(leaf, sprite.error.isEmpty() ? "Unable to decode SP2 sprite." : sprite.error);
          return;
        }
        sprite_frames.reserve(sprite.frames.size());
        sprite_frame_durations_ms.reserve(sprite.frames.size());
        for (const SpriteFrame& frame : sprite.frames) {
          if (frame.image.isNull()) {
            continue;
          }
          sprite_frames.push_back(frame.image);
          sprite_frame_durations_ms.push_back(std::clamp(frame.duration_ms, 30, 2000));
        }
      }

      if (sprite_frames.isEmpty()) {
        preview_->show_message(leaf, "Sprite has no decodable frames.");
        return;
      }

      const IdTechAssetDecodeResult decoded = decode_idtech_asset_bytes(bytes, leaf);
      const QString details_text = decoded.ok()
                                   ? decoded.summary
                                   : (decoded.error.isEmpty() ? "Unable to decode sprite metadata." : decoded.error);
      preview_->show_sprite(leaf, subtitle, sprite_frames, sprite_frame_durations_ms, details_text);
      return;
    }

    const IdTechAssetDecodeResult decoded = decode_idtech_asset_bytes(bytes, leaf);
    if (!decoded.ok()) {
      preview_->show_message(leaf, decoded.error.isEmpty() ? "Unable to decode idTech asset." : decoded.error);
      return;
    }
    preview_->show_text(leaf, subtitle, decoded.summary);
    return;
  }

  if (is_font_file_name(leaf)) {
    constexpr qint64 kMaxFontBytes = 64LL * 1024 * 1024;
    QByteArray bytes;
    QString err;

    if (!source_path.isEmpty()) {
      QFile f(source_path);
      if (!f.open(QIODevice::ReadOnly)) {
        preview_->show_message(leaf, "Unable to open source file for preview.");
        return;
      }
      const qint64 size_on_disk = f.size();
      if (size_on_disk > kMaxFontBytes) {
        preview_->show_message(leaf, "Font file is too large to inspect.");
        return;
      }
      bytes = f.readAll();
    } else {
      const qint64 max_bytes = (size > 0) ? std::min(size, kMaxFontBytes) : kMaxFontBytes;
      if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, max_bytes)) {
        preview_->show_message(leaf, err.isEmpty() ? "Unable to read font from archive." : err);
        return;
      }
    }

    preview_->show_font_from_bytes(leaf, subtitle, bytes);
    return;
  }

  // Text preview (best-effort).
  if (is_text_file_name(leaf)) {
    constexpr qint64 kMaxTextBytes = 512LL * 1024;
    constexpr qint64 kMaxShaderTextBytes = 4LL * 1024 * 1024;
    const qint64 text_limit = (ext == "shader") ? kMaxShaderTextBytes : kMaxTextBytes;
    QByteArray bytes;
    bool truncated = (size >= 0 && size > text_limit);
    QString err;
    if (!source_path.isEmpty()) {
      QFile f(source_path);
      if (f.open(QIODevice::ReadOnly)) {
        bytes = f.read(text_limit);
      } else {
        err = "Unable to open source file for preview.";
      }
    } else {
      if (!view_archive().read_entry_bytes(pak_path, &bytes, &err, text_limit)) {
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
    const QString sub = truncated ? (subtitle + "  (Content truncated)") : subtitle;
    if (ext == "shader") {
      preview_->show_text(leaf, sub, text);
      return;
    }
    if (is_cfg_like_text_ext(ext)) {
      preview_->show_cfg(leaf, sub, text);
    } else if (ext == "json") {
      preview_->show_json(leaf, sub, text);
    } else if (ext == "c" || ext == "h" || ext == "qc") {
      preview_->show_c(leaf, sub, text);
    } else if (is_plain_text_script_ext(ext)) {
      preview_->show_txt(leaf, sub, text);
    } else if (ext == "menu") {
      preview_->show_menu(leaf, sub, text);
    } else if (ext == "shader") {
      Quake3ShaderDocument shader_doc;
      QString shader_parse_error;
      if (!parse_quake3_shader_text(text, &shader_doc, &shader_parse_error)) {
        shader_doc.shaders.clear();
      }

      QHash<QString, QImage> shader_textures;
      if (!shader_doc.shaders.isEmpty()) {
        (void)ensure_quake1_palette(nullptr);
        (void)ensure_quake2_palette(nullptr);

        QSet<QString> refs;
        for (const Quake3ShaderBlock& shader : shader_doc.shaders) {
          refs.unite(collect_quake3_shader_texture_refs(shader));
        }

        auto insert_texture_aliases = [&](const QString& name, const QImage& image) {
          if (image.isNull()) {
            return;
          }
          auto add = [&](QString key) {
            key = key.trimmed().toLower();
            key.replace('\\', '/');
            while (key.startsWith('/')) {
              key.remove(0, 1);
            }
            if (!key.isEmpty()) {
              shader_textures.insert(key, image);
            }
          };

          add(name);
          const QFileInfo fi(name);
          const QString leaf_name = fi.fileName();
          const QString base_name = fi.completeBaseName();
          if (!leaf_name.isEmpty()) {
            add(leaf_name);
          }
          if (!base_name.isEmpty()) {
            add(base_name);
          }
        };

        const auto decode_texture_from_bytes = [&](const QByteArray& tex_bytes, const QString& tex_name) -> ImageDecodeResult {
          const QString tex_ext = file_ext_lower(tex_name);
          if (tex_ext == "wal") {
            if (quake2_palette_.size() != 256) {
              return ImageDecodeResult{QImage(), "Missing Quake II palette for WAL."};
            }
            QString wal_err;
            QImage img = decode_wal_image(tex_bytes, quake2_palette_, 0, tex_name, &wal_err);
            return ImageDecodeResult{std::move(img), wal_err};
          }
          if (tex_ext == "mip") {
            QString mip_err;
            QImage img = decode_miptex_image(tex_bytes, quake1_palette_.size() == 256 ? &quake1_palette_ : nullptr, 0, tex_name, &mip_err);
            return ImageDecodeResult{std::move(img), mip_err};
          }
          ImageDecodeOptions opts;
          if (tex_ext == "lmp" && quake1_palette_.size() == 256) {
            opts.palette = &quake1_palette_;
          }
          ImageDecodeResult decoded = decode_image_bytes(tex_bytes, tex_name, opts);
          if (!decoded.ok() && opts.palette) {
            decoded = decode_image_bytes(tex_bytes, tex_name, {});
          }
          return decoded;
        };

        const auto decode_texture_from_file = [&](const QString& tex_path) -> ImageDecodeResult {
          const QString tex_ext = file_ext_lower(tex_path);
          if (tex_ext == "wal") {
            if (quake2_palette_.size() != 256) {
              return ImageDecodeResult{QImage(), "Missing Quake II palette for WAL."};
            }
            QFile f(tex_path);
            if (!f.open(QIODevice::ReadOnly)) {
              return ImageDecodeResult{QImage(), "Unable to open WAL image."};
            }
            QString wal_err;
            QImage img = decode_wal_image(f.readAll(), quake2_palette_, 0, QFileInfo(tex_path).fileName(), &wal_err);
            return ImageDecodeResult{std::move(img), wal_err};
          }
          if (tex_ext == "mip") {
            QFile f(tex_path);
            if (!f.open(QIODevice::ReadOnly)) {
              return ImageDecodeResult{QImage(), "Unable to open MIP image."};
            }
            QString mip_err;
            QImage img = decode_miptex_image(f.readAll(),
                                             quake1_palette_.size() == 256 ? &quake1_palette_ : nullptr,
                                             0,
                                             QFileInfo(tex_path).fileName(),
                                             &mip_err);
            return ImageDecodeResult{std::move(img), mip_err};
          }
          ImageDecodeOptions opts;
          if (tex_ext == "lmp" && quake1_palette_.size() == 256) {
            opts.palette = &quake1_palette_;
          }
          ImageDecodeResult decoded = decode_image_file(tex_path, opts);
          if (!decoded.ok() && opts.palette) {
            decoded = decode_image_file(tex_path, {});
          }
          return decoded;
        };

        QHash<QString, QString> by_lower;
        by_lower.reserve(view_archive().entries().size() + (is_wad_mounted() ? 0 : added_files_.size()));
        for (const ArchiveEntry& e : view_archive().entries()) {
          const QString n = normalize_pak_path(e.name);
          if (!n.isEmpty()) {
            by_lower.insert(n.toLower(), e.name);
          }
        }
        if (!is_wad_mounted()) {
          for (const AddedFile& f : added_files_) {
            const QString n = normalize_pak_path(f.pak_name);
            if (!n.isEmpty()) {
              by_lower.insert(n.toLower(), f.pak_name);
            }
          }
        }

        const auto find_entry_ci = [&](const QString& want) -> QString {
          const QString key = normalize_pak_path(want).toLower();
          return key.isEmpty() ? QString() : by_lower.value(key);
        };

        const QStringList tex_exts = {"tga", "jpg", "jpeg", "png", "dds", "wal", "swl", "pcx", "lmp", "mip"};
        const QString shader_dir = source_path.isEmpty() ? QString() : QFileInfo(source_path).absolutePath();
        QStringList local_roots;
        if (!shader_dir.isEmpty()) {
          QSet<QString> root_seen;
          auto add_root = [&](const QString& root_in) {
            QString root = QDir(root_in).absolutePath();
            root.replace('\\', '/');
            if (!root.isEmpty() && !root_seen.contains(root)) {
              root_seen.insert(root);
              local_roots.push_back(root);
            }
          };
          add_root(shader_dir);
          QDir d(shader_dir);
          add_root(d.absoluteFilePath(".."));
          add_root(d.absoluteFilePath("../.."));
        }

        for (const QString& ref_in : refs) {
          QString ref = ref_in.trimmed();
          ref.replace('\\', '/');
          while (ref.startsWith('/')) {
            ref.remove(0, 1);
          }
          if (ref.isEmpty()) {
            continue;
          }

          const QFileInfo ref_info(ref);
          const QString ref_ext = ref_info.suffix().toLower();
          const bool has_ext = !ref_ext.isEmpty();
          const bool has_textures_prefix = ref.startsWith("textures/", Qt::CaseInsensitive);

          QVector<QString> candidates;
          candidates.reserve(32);
          QSet<QString> candidate_seen;
          auto add_candidate = [&](const QString& c) {
            const QString normalized = normalize_pak_path(c);
            if (!normalized.isEmpty() && !candidate_seen.contains(normalized)) {
              candidate_seen.insert(normalized);
              candidates.push_back(normalized);
            }
          };
          auto add_candidate_with_optional_prefix = [&](const QString& c) {
            add_candidate(c);
            if (!has_textures_prefix) {
              add_candidate(QString("textures/%1").arg(c));
            }
          };

          if (has_ext) {
            add_candidate_with_optional_prefix(ref);

            const QString ext = ref_ext;
            const QString base_ref = ref.left(ref.size() - ext.size() - 1);
            if (ext == "tga") {
              // Quake III tries JPG when TGA is requested but missing.
              add_candidate_with_optional_prefix(QString("%1.jpg").arg(base_ref));
            } else if (ext == "jpeg") {
              add_candidate_with_optional_prefix(QString("%1.jpg").arg(base_ref));
            } else if (ext == "jpg") {
              // Pragmatic fallback for mixed content packs.
              add_candidate_with_optional_prefix(QString("%1.tga").arg(base_ref));
            }
          } else {
            for (const QString& e : tex_exts) {
              add_candidate_with_optional_prefix(QString("%1.%2").arg(ref, e));
            }
          }

          bool loaded = false;
          if (!local_roots.isEmpty()) {
            for (const QString& root : local_roots) {
              for (const QString& cand : candidates) {
                if (loaded) {
                  break;
                }
                QString local = QDir(root).filePath(cand);
                local.replace('/', QDir::separator());
                if (!QFileInfo::exists(local)) {
                  continue;
                }
                const ImageDecodeResult decoded = decode_texture_from_file(local);
                if (!decoded.ok()) {
                  continue;
                }
                insert_texture_aliases(ref, decoded.image);
                insert_texture_aliases(cand, decoded.image);
                loaded = true;
              }
              if (loaded) {
                break;
              }
            }
          }

          if (loaded) {
            continue;
          }

          constexpr qint64 kMaxShaderTexBytes = 64LL * 1024 * 1024;
          for (const QString& cand : candidates) {
            const QString found = find_entry_ci(cand);
            if (found.isEmpty()) {
              continue;
            }
            const int tex_added_idx = added_index_by_name_.value(normalize_pak_path(found), -1);
            if (tex_added_idx >= 0 && tex_added_idx < added_files_.size()) {
              const ImageDecodeResult decoded = decode_texture_from_file(added_files_[tex_added_idx].source_path);
              if (!decoded.ok()) {
                continue;
              }
              insert_texture_aliases(ref, decoded.image);
              insert_texture_aliases(found, decoded.image);
              loaded = true;
              break;
            }

            QByteArray tex_bytes;
            QString tex_err;
            if (!view_archive().read_entry_bytes(found, &tex_bytes, &tex_err, kMaxShaderTexBytes)) {
              continue;
            }
            const ImageDecodeResult decoded = decode_texture_from_bytes(tex_bytes, QFileInfo(found).fileName());
            if (!decoded.ok()) {
              continue;
            }
            insert_texture_aliases(ref, decoded.image);
            insert_texture_aliases(found, decoded.image);
            loaded = true;
            break;
          }
        }
      }

      preview_->show_shader(leaf, sub, text, shader_doc, std::move(shader_textures));
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
  if (looks_like_text(bytes)) {
    const QString sub = truncated ? (subtitle + "  (Content truncated)") : subtitle;
    preview_->show_text(leaf, sub, QString::fromUtf8(bytes));
    return;
  }
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

  const int mounted_depth = static_cast<int>(mounted_archives_.size());

  // Index 0 is always the outer archive "Root" crumb.
  if (index <= 0) {
    if (is_wad_mounted()) {
      unmount_wad();
      return;
    }
    set_current_dir({});
    return;
  }

  if (index <= mounted_depth) {
    while (static_cast<int>(mounted_archives_.size()) > index) {
      mounted_archives_.pop_back();
    }
    set_current_dir({});
    return;
  }

  const QStringList crumbs = breadcrumbs_->crumbs();

  // Keep crumbs[mounted_depth + 1..index] as the current directory within the active archive.
  QStringList next;
  for (int i = mounted_depth + 1; i <= index && i < crumbs.size(); ++i) {
    next.push_back(crumbs[i]);
  }
  set_current_dir(next);
}

