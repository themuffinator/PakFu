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
#include <QPushButton>
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
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "pak/pak_archive.h"
#include "ui/breadcrumb_bar.h"
#include "ui/preview_pane.h"

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

QVector<ChildListing> list_children(const QVector<PakEntry>& entries,
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

  for (const PakEntry& e : entries) {
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
    item.mtime_utc_secs = -1;
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
  const bool want_trailing_slash = path.endsWith('/') || path.endsWith('\\');
  path.replace('\\', '/');
  while (path.startsWith('/')) {
    path.remove(0, 1);
  }
  path = QDir::cleanPath(path);
  path.replace('\\', '/');
  if (path == ".") {
    path.clear();
  }
  if (want_trailing_slash && !path.isEmpty() && !path.endsWith('/')) {
    path += '/';
  }
  return path;
}

bool is_safe_entry_name(const QString& name) {
  if (name.isEmpty()) {
    return false;
  }
  if (name.contains('\\') || name.contains(':')) {
    return false;
  }
  if (name.startsWith('/') || name.startsWith("./") || name.startsWith("../")) {
    return false;
  }
  const QStringList parts = name.split('/', Qt::SkipEmptyParts);
  for (const QString& p : parts) {
    if (p == "." || p == "..") {
      return false;
    }
  }
  return true;
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

    const bool a_dir = data(0, kRoleIsDir).toBool();
    const bool b_dir = other.data(0, kRoleIsDir).toBool();
    if (a_dir != b_dir) {
      return a_dir > b_dir;
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

    return text(col).compare(other.text(col), Qt::CaseInsensitive) < 0;
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
    "png", "jpg", "jpeg", "bmp", "gif", "tga", "pcx", "wal", "tif", "tiff"
  };
  return kImageExts.contains(ext);
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

bool is_text_file_name(const QString& name) {
  const QString ext = file_ext_lower(name);
  static const QSet<QString> kTextExts = {
    "cfg", "txt", "log", "md", "ini", "json", "xml", "shader", "script"
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
  build_ui();
  if (mode_ == Mode::ExistingPak) {
    load_archive();
  } else {
    loaded_ = true;
    set_dirty(false);
    refresh_listing();
  }
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
      *error = "PAK is not loaded.";
    }
    return false;
  }
  if (!dirty_) {
    return true;
  }
  if (pak_path_.isEmpty()) {
    if (error) {
      *error = "This PAK has not been saved yet. Use Save As...";
    }
    return false;
  }
  return save_as(pak_path_, error);
}

bool PakTab::save_as(const QString& dest_path, QString* error) {
  if (!loaded_) {
    if (error) {
      *error = "PAK is not loaded.";
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

  if (!write_pak_file(abs, error)) {
    return false;
  }

  QString reload_err;
  if (!archive_.load(abs, &reload_err)) {
    if (error) {
      *error = reload_err.isEmpty() ? "Saved, but failed to reload the new PAK." : reload_err;
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
    // Placeholder: later this will open a preview panel for the selected file.
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
    // Placeholder: later this will open a preview panel for the selected file.
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

void PakTab::apply_auto_view(int file_count, int image_count) {
  // Auto: prefer large icons when the folder is predominantly images.
  const bool mostly_images =
    (file_count > 0) && (image_count * 100 >= file_count * 60);
  effective_view_ = mostly_images ? ViewMode::LargeIcons : ViewMode::Details;
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
      grid = QSize(220, 210);
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
  icon_view_->setSpacing(10);
  icon_view_->setGridSize(grid);
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
    for (const PakEntry& e : archive_.entries()) {
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
  if (added_idx >= 0 && added_idx < added_files_.size()) {
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

  if (!archive_.is_loaded()) {
    if (error) {
      *error = "Unable to export from an unloaded PAK.";
    }
    return false;
  }

  QString err;
  if (!archive_.extract_entry_to_file(pak_path, dest_file, &err)) {
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
    for (const PakEntry& e : archive_.entries()) {
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

bool PakTab::import_urls(const QList<QUrl>& urls, const QString& dest_prefix, QStringList* failures) {
  bool changed = false;
  for (const QUrl& url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QString local = url.toLocalFile();
    const QFileInfo info(local);
    if (!info.exists()) {
      continue;
    }
    if (info.isDir()) {
      QStringList folder_failures;
      const bool did = add_folder_from_path(info.absoluteFilePath(), dest_prefix, QString(), &folder_failures);
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
  const bool changed = import_urls(urls, dest_prefix, &failures);

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

QMimeData* PakTab::make_mime_data_for_items(const QVector<QPair<QString, bool>>& items, bool cut, QStringList* failures) {
  QList<QUrl> urls;
  QJsonArray json_items;

  for (const auto& it : items) {
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

  QStringList failures;
  QMimeData* mime = make_mime_data_for_items(items, cut, &failures);

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
  changed = import_urls(urls, dest_prefix, &failures);

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

  const QVector<AddedFile> before_added = added_files_;
  const QSet<QString> before_virtual = virtual_dirs_;
  const QSet<QString> before_deleted_files = deleted_files_;
  const QSet<QString> before_deleted_dirs = deleted_dir_prefixes_;

  QFileDialog dialog(this);
  dialog.setWindowTitle("Add Files");
  dialog.setFileMode(QFileDialog::ExistingFiles);
  dialog.setNameFilters({"All files (*.*)"});
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

  QStringList failures;
  bool changed = false;
  for (const QString& path : selected) {
    const QString pak_name = current_prefix() + QFileInfo(path).fileName();
    QString err;
    if (!add_file_mapping(pak_name, path, &err)) {
      failures.push_back(err.isEmpty() ? QString("Failed to add: %1").arg(path) : err);
    } else {
      changed = true;
    }
  }

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
                                  QStringList* failures) {
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

  QDirIterator it(folder_path, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
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
  }

  return changed;
}

void PakTab::add_folder() {
  if (!loaded_) {
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

  QStringList failures;
  const bool changed = add_folder_from_path(selected.first(), current_prefix(), QString(), &failures);

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

  QVector<PakEntry> new_entries;
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
    for (const PakEntry& e : archive_.entries()) {
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

      PakEntry out_entry;
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

    PakEntry out_entry;
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
    const PakEntry& e = new_entries[i];
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

void PakTab::load_archive() {
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
  for (const QString& p : parts) {
    crumbs.push_back(p);
  }
  if (breadcrumbs_) {
    breadcrumbs_->set_crumbs(crumbs);
  }

  refresh_listing();
}

void PakTab::refresh_listing() {
  if (details_view_) {
    details_view_->clear();
  }
  if (icon_view_) {
    icon_view_->clear();
  }

  if (add_files_action_) {
    add_files_action_->setEnabled(loaded_);
  }
  if (add_folder_action_) {
    add_folder_action_->setEnabled(loaded_);
  }
  if (new_folder_action_) {
    new_folder_action_->setEnabled(loaded_);
  }
  if (delete_action_) {
    delete_action_->setEnabled(loaded_);
  }

  if (!loaded_) {
    const QString msg = load_error_.isEmpty() ? "Failed to load PAK." : load_error_;
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
  added_sizes.reserve(added_files_.size());
  added_sources.reserve(added_files_.size());
  added_mtimes.reserve(added_files_.size());
  for (const AddedFile& f : added_files_) {
    added_sizes.insert(f.pak_name, f.size);
    added_sources.insert(f.pak_name, f.source_path);
    added_mtimes.insert(f.pak_name, f.mtime_utc_secs);
  }

  const QVector<PakEntry> empty_entries;
  const QVector<ChildListing> children =
    list_children(archive_.is_loaded() ? archive_.entries() : empty_entries,
                  added_sizes,
                  added_sources,
                  added_mtimes,
                  virtual_dirs_,
                  deleted_files_,
                  deleted_dir_prefixes_,
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
  for (const ChildListing& child : children) {
    if (child.is_dir) {
      continue;
    }
    ++file_count;
    if (is_image_file_name(child.name)) {
      ++image_count;
    }
  }

  if (view_mode_ == ViewMode::Auto) {
    apply_auto_view(file_count, image_count);
  } else {
    effective_view_ = view_mode_;
  }

  update_view_controls();

  const QIcon dir_icon = style()->standardIcon(QStyle::SP_DirIcon);
  const QIcon file_icon = style()->standardIcon(QStyle::SP_FileIcon);

  const bool show_details = (effective_view_ == ViewMode::Details);

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
      item->setIcon(0, child.is_dir ? dir_icon : file_icon);

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

    for (const ChildListing& child : children) {
      const QString full_path =
        normalize_pak_path(current_prefix() + child.name + (child.is_dir ? "/" : ""));

      const QString label = child.is_dir ? (child.name + "/") : child.name;
      auto* item = new QListWidgetItem(label);
      item->setData(kRoleIsDir, child.is_dir);
      item->setData(kRolePakPath, full_path);
      item->setData(kRoleSize, static_cast<qint64>(child.size));
      item->setData(kRoleMtime, child.mtime_utc_secs);
      item->setData(kRoleIsAdded, child.is_added);
      item->setData(kRoleIsOverridden, child.is_overridden);

      QIcon icon = child.is_dir ? dir_icon : file_icon;
      if (!child.is_dir && is_image_file_name(child.name) && !child.source_path.isEmpty()) {
        QPixmap pm(child.source_path);
        if (!pm.isNull()) {
          icon = QIcon(pm.scaled(icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
		return;
	}
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
  const bool is_video = (ext == "roq" || ext == "cin" || ext == "mp4" || ext == "mkv");

  QString source_path;
  const int added_idx = added_index_by_name_.value(normalize_pak_path(pak_path), -1);
  if (added_idx >= 0 && added_idx < added_files_.size()) {
    source_path = added_files_[added_idx].source_path;
  }

  if (is_image_file_name(leaf)) {
    if (!source_path.isEmpty()) {
      preview_->show_image_from_file(leaf, subtitle, source_path);
      return;
    }
    QByteArray bytes;
    QString err;
    constexpr qint64 kMaxImageBytes = 32LL * 1024 * 1024;
    if (!archive_.read_entry_bytes(pak_path, &bytes, &err, kMaxImageBytes)) {
      preview_->show_message(leaf, err.isEmpty() ? "Unable to read image from PAK." : err);
      return;
    }
    preview_->show_image_from_bytes(leaf, subtitle, bytes);
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
    preview_->show_message(leaf, "Video preview is not implemented yet.");
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
      if (!archive_.read_entry_bytes(pak_path, &bytes, &err, kMaxTextBytes)) {
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
    if (ext == "cfg") {
      preview_->show_cfg(leaf, truncated ? (subtitle + "  (Preview truncated)") : subtitle, text);
    } else {
      preview_->show_text(leaf, truncated ? (subtitle + "  (Preview truncated)") : subtitle, text);
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
    archive_.read_entry_bytes(pak_path, &bytes, &err, kMaxBinBytes);
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
  // Index 0 is always the "Root" crumb.
  if (index <= 0) {
    set_current_dir({});
    return;
  }

  // Keep crumbs[1..index] as the current directory.
  QStringList next;
  const QStringList crumbs = breadcrumbs_->crumbs();
  for (int i = 1; i <= index && i < crumbs.size(); ++i) {
    next.push_back(crumbs[i]);
  }
  set_current_dir(next);
}
