#include "pak_tab.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QAbstractScrollArea>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QKeySequence>
#include <QListView>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSet>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QTimeZone>
#include <QBrush>
#include <QSaveFile>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "pak/pak_archive.h"
#include "ui/breadcrumb_bar.h"

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
}  // namespace

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
  load_error_.clear();
  loaded_ = true;
  set_current_dir(current_dir_);
  return true;
}

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

  view_stack_ = new QStackedWidget(this);
  layout->addWidget(view_stack_, 1);

  details_view_ = new QTreeWidget(view_stack_);
  details_view_->setHeaderLabels({"Name", "Size", "Modified"});
  details_view_->setRootIsDecorated(false);
  details_view_->setUniformRowHeights(true);
  details_view_->setAlternatingRowColors(true);
  details_view_->setSelectionMode(QAbstractItemView::SingleSelection);
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

  icon_view_ = new QListWidget(view_stack_);
  icon_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  icon_view_->setContextMenuPolicy(Qt::CustomContextMenu);
  icon_view_->setSortingEnabled(true);
  view_stack_->addWidget(icon_view_);

  connect(details_view_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    show_context_menu(details_view_, pos);
  });
  connect(icon_view_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    show_context_menu(icon_view_, pos);
  });

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

void PakTab::delete_selected(bool skip_confirmation) {
  if (!loaded_) {
    return;
  }

  bool is_dir = false;
  QString sel = selected_pak_path(&is_dir);
  if (sel.isEmpty()) {
    return;
  }
  sel = normalize_pak_path(sel);
  if (is_dir && !sel.endsWith('/')) {
    sel += '/';
  }

  // Determine how many files would be removed (best-effort).
  int affected_files = 0;
  if (is_dir) {
    for (const PakEntry& e : archive_.entries()) {
      const QString name = normalize_pak_path(e.name);
      if (name.startsWith(sel) && !is_deleted_path(name)) {
        ++affected_files;
      }
    }
    for (const AddedFile& f : added_files_) {
      const QString name = normalize_pak_path(f.pak_name);
      if (name.startsWith(sel) && !is_deleted_path(name)) {
        ++affected_files;
      }
    }
  } else {
    affected_files = 1;
  }

  const bool force = skip_confirmation || (QApplication::keyboardModifiers() & Qt::ShiftModifier);
  if (!force) {
    QString display = sel;
    if (display.endsWith('/')) {
      display.chop(1);
    }
    const int slash = display.lastIndexOf('/');
    const QString leaf = (slash >= 0) ? display.mid(slash + 1) : display;
    const QString title = is_dir ? "Delete Folder" : "Delete File";
    const QString text = is_dir
      ? QString("Delete folder \"%1/\" from this PAK?").arg(leaf)
      : QString("Delete \"%1\" from this PAK?").arg(leaf);

    QString info = "This does not delete any source files on disk.";
    if (is_dir) {
      info = QString("This will remove %1 file(s) from the archive.\n\n%2")
               .arg(affected_files)
               .arg(info);
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
  if (is_dir) {
    deleted_dir_prefixes_.insert(sel);

    // Remove any added files under this folder (they're now deleted).
    bool removed_added = false;
    for (int i = added_files_.size() - 1; i >= 0; --i) {
      const QString name = normalize_pak_path(added_files_[i].pak_name);
      if (name.startsWith(sel)) {
        added_files_.removeAt(i);
        removed_added = true;
      }
    }
    if (removed_added) {
      rebuild_added_index();
    }

    // Remove virtual dirs under this folder.
    QSet<QString> kept_dirs;
    kept_dirs.reserve(virtual_dirs_.size());
    for (const QString& d : virtual_dirs_) {
      if (!normalize_pak_path(d).startsWith(sel)) {
        kept_dirs.insert(d);
      }
    }
    virtual_dirs_.swap(kept_dirs);

    // Remove exact file deletions under this folder (folder deletion supersedes them).
    QSet<QString> kept_deleted_files;
    kept_deleted_files.reserve(deleted_files_.size());
    for (const QString& f : deleted_files_) {
      if (!normalize_pak_path(f).startsWith(sel)) {
        kept_deleted_files.insert(f);
      }
    }
    deleted_files_.swap(kept_deleted_files);

    changed = true;
  } else {
    deleted_files_.insert(sel);
    remove_added_file_by_name(sel);
    changed = true;
  }

  if (changed) {
    set_dirty(true);
    refresh_listing();
  }
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
    set_dirty(true);
  }
  refresh_listing();
}

void PakTab::add_folder() {
  if (!loaded_) {
    return;
  }

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

  const QString folder_path = selected.first();
  const QFileInfo folder_info(folder_path);
  const QString folder_name = folder_info.fileName().isEmpty() ? "folder" : folder_info.fileName();
  const QString pak_root = normalize_pak_path(current_prefix() + folder_name) + "/";
  virtual_dirs_.insert(pak_root);
  clear_deletions_under(pak_root);
  bool changed = true;

  QDir base(folder_path);
  QStringList failures;

  QDirIterator it(folder_path, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString file_path = it.next();
    const QString rel = normalize_pak_path(base.relativeFilePath(file_path));
    const QString pak_name = pak_root + rel;
    QString err;
    if (!add_file_mapping(pak_name, file_path, &err)) {
      failures.push_back(err.isEmpty() ? QString("Failed to add: %1").arg(file_path) : err);
    } else {
      changed = true;
    }
  }

  if (!failures.isEmpty()) {
    QMessageBox::warning(this, "Add Folder", failures.mid(0, 12).join("\n"));
  }

  if (changed) {
    set_dirty(true);
  }
  refresh_listing();
}

void PakTab::new_folder() {
  if (!loaded_) {
    return;
  }

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
  set_dirty(true);
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
