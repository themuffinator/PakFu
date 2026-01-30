#include "pak_tab.h"

#include <algorithm>

#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QSet>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "pak/pak_archive.h"
#include "ui/breadcrumb_bar.h"

namespace {
struct ChildListing {
  QString name;
  bool is_dir = false;
  quint32 size = 0;
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

QVector<ChildListing> list_children(const QVector<PakEntry>& entries, const QStringList& dir) {
  const QString prefix = join_prefix(dir);
  QSet<QString> dirs;
  QHash<QString, quint32> files;

  for (const PakEntry& e : entries) {
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
    files.insert(rest, e.size);
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
    ChildListing item;
    item.name = it.key();
    item.is_dir = false;
    item.size = it.value();
    out.push_back(item);
  }

  std::sort(out.begin(), out.end(), [](const ChildListing& a, const ChildListing& b) {
    if (a.is_dir != b.is_dir) {
      return a.is_dir > b.is_dir;
    }
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });

  return out;
}
}  // namespace

PakTab::PakTab(Mode mode, const QString& pak_path, QWidget* parent)
    : QWidget(parent), mode_(mode), pak_path_(pak_path) {
  build_ui();
  if (mode_ == Mode::ExistingPak) {
    load_archive();
  } else {
    loaded_ = true;
    refresh_listing();
  }
}

void PakTab::build_ui() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 18, 22, 18);
  layout->setSpacing(12);

  breadcrumbs_ = new BreadcrumbBar(this);
  breadcrumbs_->set_crumbs({"Root"});
  connect(breadcrumbs_, &BreadcrumbBar::crumb_activated, this, &PakTab::activate_crumb);
  layout->addWidget(breadcrumbs_);

  listing_ = new QTreeWidget(this);
  listing_->setHeaderLabels({"Name", "Size"});
  listing_->setRootIsDecorated(false);
  listing_->setUniformRowHeights(true);
  listing_->setAlternatingRowColors(true);
  listing_->setSelectionMode(QAbstractItemView::SingleSelection);
  listing_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  listing_->setExpandsOnDoubleClick(false);
  listing_->header()->setStretchLastSection(false);
  listing_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  listing_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  layout->addWidget(listing_, 1);

  connect(listing_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
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
}

void PakTab::load_archive() {
  PakArchive archive;
  QString err;
  if (!archive.load(pak_path_, &err)) {
    loaded_ = false;
    load_error_ = err;
    refresh_listing();
    return;
  }

  loaded_ = true;
  load_error_.clear();

  // Cache entries locally to keep this widget independent from the loader.
  const QVector<PakEntry>& loaded_entries = archive.entries();
  QVector<PakEntry> entries;
  entries.reserve(loaded_entries.size());
  for (const PakEntry& e : loaded_entries) {
    entries.push_back(e);
  }
  // Store in-place via swap to avoid extra allocations.
  entries_.swap(entries);

  // Root listing.
  set_current_dir({});
}

void PakTab::set_current_dir(const QStringList& parts) {
  current_dir_ = parts;

  QString root = "Root";
  if (mode_ == Mode::ExistingPak) {
    const QFileInfo info(pak_path_);
    root = info.fileName().isEmpty() ? "PAK" : info.fileName();
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
  if (!listing_) {
    return;
  }

  listing_->clear();

  if (!loaded_) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, load_error_.isEmpty() ? "Failed to load PAK." : load_error_);
    item->setFlags(Qt::NoItemFlags);
    listing_->addTopLevelItem(item);
    return;
  }

  if (mode_ == Mode::NewPak) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, "Empty archive (creation workflow not implemented yet).");
    item->setFlags(Qt::NoItemFlags);
    listing_->addTopLevelItem(item);
    return;
  }

  const QVector<ChildListing> children = list_children(entries_, current_dir_);
  if (children.isEmpty()) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, "No entries in this folder.");
    item->setFlags(Qt::NoItemFlags);
    listing_->addTopLevelItem(item);
    return;
  }

  const QIcon dir_icon = style()->standardIcon(QStyle::SP_DirIcon);
  const QIcon file_icon = style()->standardIcon(QStyle::SP_FileIcon);

  for (const ChildListing& child : children) {
    auto* item = new QTreeWidgetItem();
    item->setText(0, child.is_dir ? (child.name + "/") : child.name);
    item->setData(0, Qt::UserRole, child.is_dir);
    item->setIcon(0, child.is_dir ? dir_icon : file_icon);
    if (!child.is_dir) {
      item->setText(1, format_size(child.size));
    }
    listing_->addTopLevelItem(item);
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
