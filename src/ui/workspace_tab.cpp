#include "ui/workspace_tab.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/ui_icons.h"

namespace {
QTableWidgetItem* make_item(const QString& text) {
  auto* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  return item;
}

QTableWidgetItem* make_number_item(qint64 value) {
  auto* item = make_item(QString::number(value));
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return item;
}

QTableWidgetItem* make_size_item(quint32 bytes) {
  QString text = QString::number(bytes);
  if (bytes >= 1024 * 1024) {
    text = QString("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
  } else if (bytes >= 1024) {
    text = QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
  }
  auto* item = make_item(text);
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  item->setData(Qt::UserRole, static_cast<qulonglong>(bytes));
  return item;
}

void tune_table(QTableWidget* table) {
  if (!table) {
    return;
  }
  table->setAlternatingRowColors(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSortingEnabled(true);
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setHighlightSections(false);
}

QLabel* make_section_label(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  QFont font = label->font();
  font.setWeight(QFont::DemiBold);
  label->setFont(font);
  return label;
}

QString native_or_empty(const QString& path) {
  return path.isEmpty() ? QStringLiteral("-") : QDir::toNativeSeparators(path);
}

QString installation_name(const GameSet& set) {
  return set.name.isEmpty() ? game_display_name(set.game) : set.name;
}

QString archive_state_text(const WorkspaceTab::ArchiveSummary& summary) {
  QStringList bits;
  bits.push_back(summary.loaded ? QStringLiteral("Loaded") : QStringLiteral("Not loaded"));
  if (summary.dirty) {
    bits.push_back(QStringLiteral("Modified"));
  }
  if (summary.protected_archive) {
    bits.push_back(QStringLiteral("Protected"));
  }
  return bits.join(", ");
}

QFrame* make_rule(QWidget* parent) {
  auto* line = new QFrame(parent);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  return line;
}
}  // namespace

WorkspaceTab::WorkspaceTab(Callbacks callbacks, QWidget* parent)
    : QWidget(parent), callbacks_(std::move(callbacks)) {
  build_ui();
  set_state(State{});
}

QString WorkspaceTab::search_query() const {
  return search_edit_ ? search_edit_->text() : QString();
}

void WorkspaceTab::set_state(const State& state) {
  state_ = state;
  populate_overview();
  populate_installations();
  populate_changes();
  populate_dependencies();
  populate_validation();
  populate_capabilities();
}

void WorkspaceTab::set_search_results(const QVector<SearchResult>& results) {
  search_results_ = results;
  populate_search_results();
}

void WorkspaceTab::build_ui() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(18, 16, 18, 16);
  root->setSpacing(12);

  auto* action_row = new QHBoxLayout();
  action_row->setSpacing(8);

  auto* new_button = new QToolButton(this);
  new_button->setText("New");
  new_button->setIcon(UiIcons::icon(UiIcons::Id::NewPak, style()));
  new_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  action_row->addWidget(new_button);
  connect(new_button, &QToolButton::clicked, this, [this]() {
    if (callbacks_.new_archive) {
      callbacks_.new_archive();
    }
  });

  auto* open_file_button = new QToolButton(this);
  open_file_button->setText("Open File");
  open_file_button->setIcon(UiIcons::icon(UiIcons::Id::OpenArchive, style()));
  open_file_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  action_row->addWidget(open_file_button);
  connect(open_file_button, &QToolButton::clicked, this, [this]() {
    if (callbacks_.open_file) {
      callbacks_.open_file();
    }
  });

  auto* open_archive_button = new QToolButton(this);
  open_archive_button->setText("Open Archive");
  open_archive_button->setIcon(UiIcons::icon(UiIcons::Id::OpenArchive, style()));
  open_archive_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  action_row->addWidget(open_archive_button);
  connect(open_archive_button, &QToolButton::clicked, this, [this]() {
    if (callbacks_.open_archive) {
      callbacks_.open_archive();
    }
  });

  auto* open_folder_button = new QToolButton(this);
  open_folder_button->setText("Open Folder");
  open_folder_button->setIcon(UiIcons::icon(UiIcons::Id::OpenFolder, style()));
  open_folder_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  action_row->addWidget(open_folder_button);
  connect(open_folder_button, &QToolButton::clicked, this, [this]() {
    if (callbacks_.open_folder) {
      callbacks_.open_folder();
    }
  });

  action_row->addStretch(1);

  auto* installs_button = new QToolButton(this);
  installs_button->setText("Installations");
  installs_button->setIcon(UiIcons::icon(UiIcons::Id::Configure, style()));
  installs_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  action_row->addWidget(installs_button);
  connect(installs_button, &QToolButton::clicked, this, [this]() {
    if (callbacks_.manage_installations) {
      callbacks_.manage_installations();
    }
  });

  auto* refresh_button = new QToolButton(this);
  refresh_button->setText("Refresh");
  refresh_button->setIcon(UiIcons::icon(UiIcons::Id::AutoDetect, style()));
  refresh_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  action_row->addWidget(refresh_button);
  connect(refresh_button, &QToolButton::clicked, this, [this]() {
    if (callbacks_.refresh) {
      callbacks_.refresh();
    }
  });

  root->addLayout(action_row);

  auto* summary = new QWidget(this);
  auto* summary_layout = new QGridLayout(summary);
  summary_layout->setContentsMargins(0, 0, 0, 0);
  summary_layout->setHorizontalSpacing(18);
  summary_layout->setVerticalSpacing(6);
  summary_layout->addWidget(make_section_label("Installation", summary), 0, 0);
  summary_layout->addWidget(make_section_label("Open Archives", summary), 0, 1);
  summary_layout->addWidget(make_section_label("Recent Files", summary), 0, 2);
  install_label_ = new QLabel(summary);
  archive_count_label_ = new QLabel(summary);
  recent_count_label_ = new QLabel(summary);
  install_detail_label_ = new QLabel(summary);
  install_detail_label_->setWordWrap(true);
  install_detail_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  summary_layout->addWidget(install_label_, 1, 0);
  summary_layout->addWidget(archive_count_label_, 1, 1);
  summary_layout->addWidget(recent_count_label_, 1, 2);
  summary_layout->addWidget(install_detail_label_, 2, 0, 1, 3);
  summary_layout->setColumnStretch(0, 3);
  summary_layout->setColumnStretch(1, 1);
  summary_layout->setColumnStretch(2, 1);
  root->addWidget(summary);
  root->addWidget(make_rule(this));

  sections_ = new QTabWidget(this);
  sections_->setDocumentMode(true);
  root->addWidget(sections_, 1);

  auto* overview = new QWidget(sections_);
  auto* overview_layout = new QVBoxLayout(overview);
  overview_layout->setContentsMargins(0, 10, 0, 0);
  overview_layout->setSpacing(10);
  overview_layout->addWidget(make_section_label("Open archives", overview));
  archives_table_ = new QTableWidget(overview);
  archives_table_->setColumnCount(8);
  archives_table_->setHorizontalHeaderLabels({"Archive", "Format", "Entries", "Added", "Deleted", "Folder", "State", "Path"});
  tune_table(archives_table_);
  archives_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  archives_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
  archives_table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
  overview_layout->addWidget(archives_table_, 3);
  connect(archives_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (!callbacks_.focus_archive || row < 0 || row >= archives_table_->rowCount()) {
      return;
    }
    const QString path = archives_table_->item(row, 7) ? archives_table_->item(row, 7)->text() : QString();
    callbacks_.focus_archive(QDir::fromNativeSeparators(path));
  });

  overview_layout->addWidget(make_section_label("Recent files", overview));
  recent_table_ = new QTableWidget(overview);
  recent_table_->setColumnCount(2);
  recent_table_->setHorizontalHeaderLabels({"File", "Path"});
  tune_table(recent_table_);
  recent_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  recent_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  overview_layout->addWidget(recent_table_, 2);
  connect(recent_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (!callbacks_.focus_archive || row < 0 || row >= recent_table_->rowCount()) {
      return;
    }
    const QString path = recent_table_->item(row, 1) ? recent_table_->item(row, 1)->text() : QString();
    callbacks_.focus_archive(QDir::fromNativeSeparators(path));
  });
  sections_->addTab(overview, "Overview");

  auto* install_page = new QWidget(sections_);
  auto* install_layout = new QVBoxLayout(install_page);
  install_layout->setContentsMargins(0, 10, 0, 0);
  installations_table_ = new QTableWidget(install_page);
  installations_table_->setColumnCount(5);
  installations_table_->setHorizontalHeaderLabels({"Installation", "Game", "Root", "Default Folder", "State"});
  tune_table(installations_table_);
  installations_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  installations_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  installations_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  install_layout->addWidget(installations_table_);
  sections_->addTab(install_page, "Installations");

  auto* search_page = new QWidget(sections_);
  auto* search_layout = new QVBoxLayout(search_page);
  search_layout->setContentsMargins(0, 10, 0, 0);
  search_layout->setSpacing(8);
  auto* search_row = new QHBoxLayout();
  search_edit_ = new QLineEdit(search_page);
  search_edit_->setPlaceholderText("Search open archives");
  search_edit_->setClearButtonEnabled(true);
  search_row->addWidget(search_edit_, 1);
  auto* search_button = new QPushButton("Search", search_page);
  search_button->setIcon(UiIcons::icon(UiIcons::Id::Details, style()));
  search_row->addWidget(search_button);
  search_layout->addLayout(search_row);
  connect(search_edit_, &QLineEdit::returnPressed, this, &WorkspaceTab::run_search);
  connect(search_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (text.trimmed().isEmpty()) {
      run_search();
    }
  });
  connect(search_button, &QPushButton::clicked, this, &WorkspaceTab::run_search);

  search_results_table_ = new QTableWidget(search_page);
  search_results_table_->setColumnCount(6);
  search_results_table_->setHorizontalHeaderLabels({"Archive", "Path", "Type", "Size", "State", "Scope"});
  tune_table(search_results_table_);
  search_results_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  search_results_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  search_results_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
  search_layout->addWidget(search_results_table_, 1);
  connect(search_results_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (!callbacks_.focus_archive || row < 0 || row >= search_results_table_->rowCount()) {
      return;
    }
    const QTableWidgetItem* item = search_results_table_->item(row, 0);
    if (!item) {
      return;
    }
    callbacks_.focus_archive(item->data(Qt::UserRole).toString());
  });
  sections_->addTab(search_page, "Search");

  auto* changes_page = new QWidget(sections_);
  auto* changes_layout = new QVBoxLayout(changes_page);
  changes_layout->setContentsMargins(0, 10, 0, 0);
  changes_table_ = new QTableWidget(changes_page);
  changes_table_->setColumnCount(5);
  changes_table_->setHorizontalHeaderLabels({"Archive", "Added", "Deleted", "State", "Path"});
  tune_table(changes_table_);
  changes_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  changes_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  changes_layout->addWidget(changes_table_);
  sections_->addTab(changes_page, "Changes");

  auto* deps_page = new QWidget(sections_);
  auto* deps_layout = new QVBoxLayout(deps_page);
  deps_layout->setContentsMargins(0, 10, 0, 0);
  dependencies_table_ = new QTableWidget(deps_page);
  dependencies_table_->setColumnCount(1);
  dependencies_table_->setHorizontalHeaderLabels({"Dependency Hints"});
  tune_table(dependencies_table_);
  dependencies_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  deps_layout->addWidget(dependencies_table_);
  sections_->addTab(deps_page, "Dependencies");

  auto* validation_page = new QWidget(sections_);
  auto* validation_layout = new QVBoxLayout(validation_page);
  validation_layout->setContentsMargins(0, 10, 0, 0);
  validation_table_ = new QTableWidget(validation_page);
  validation_table_->setColumnCount(1);
  validation_table_->setHorizontalHeaderLabels({"Workspace Checks"});
  tune_table(validation_table_);
  validation_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  validation_layout->addWidget(validation_table_);
  sections_->addTab(validation_page, "Validation");

  auto* capabilities_page = new QWidget(sections_);
  auto* capabilities_layout = new QVBoxLayout(capabilities_page);
  capabilities_layout->setContentsMargins(0, 10, 0, 0);
  capabilities_table_ = new QTableWidget(capabilities_page);
  capabilities_table_->setColumnCount(3);
  capabilities_table_->setHorizontalHeaderLabels({"Area", "Status", "Details"});
  tune_table(capabilities_table_);
  capabilities_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  capabilities_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  capabilities_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  capabilities_layout->addWidget(capabilities_table_);
  sections_->addTab(capabilities_page, "Capabilities");
}

void WorkspaceTab::run_search() {
  if (callbacks_.search) {
    callbacks_.search(search_query());
  }
}

void WorkspaceTab::populate_overview() {
  if (install_label_) {
    install_label_->setText(installation_name(state_.active_install));
  }
  if (install_detail_label_) {
    QStringList details;
    details.push_back(game_display_name(state_.active_install.game));
    if (!state_.active_install.root_dir.isEmpty()) {
      details.push_back(QString("Root: %1").arg(native_or_empty(state_.active_install.root_dir)));
    }
    if (!state_.active_install.default_dir.isEmpty()) {
      details.push_back(QString("Default: %1").arg(native_or_empty(state_.active_install.default_dir)));
    }
    install_detail_label_->setText(details.join("   "));
  }
  if (archive_count_label_) {
    archive_count_label_->setText(QString::number(state_.archives.size()));
  }
  if (recent_count_label_) {
    recent_count_label_->setText(QString::number(state_.recent_files.size()));
  }

  if (archives_table_) {
    archives_table_->setSortingEnabled(false);
    archives_table_->setRowCount(state_.archives.size());
    for (int row = 0; row < state_.archives.size(); ++row) {
      const ArchiveSummary& archive = state_.archives[row];
      archives_table_->setItem(row, 0, make_item(archive.title));
      archives_table_->setItem(row, 1, make_item(archive.format));
      archives_table_->setItem(row, 2, make_number_item(archive.entry_count));
      archives_table_->setItem(row, 3, make_number_item(archive.added_count));
      archives_table_->setItem(row, 4, make_number_item(archive.deleted_count));
      archives_table_->setItem(row, 5, make_item(archive.current_prefix.isEmpty() ? QStringLiteral("/") : archive.current_prefix));
      archives_table_->setItem(row, 6, make_item(archive_state_text(archive)));
      archives_table_->setItem(row, 7, make_item(native_or_empty(archive.archive_path)));
    }
    archives_table_->setSortingEnabled(true);
  }

  if (recent_table_) {
    recent_table_->setSortingEnabled(false);
    recent_table_->setRowCount(state_.recent_files.size());
    for (int row = 0; row < state_.recent_files.size(); ++row) {
      const QFileInfo info(state_.recent_files[row]);
      recent_table_->setItem(row, 0, make_item(info.fileName().isEmpty() ? state_.recent_files[row] : info.fileName()));
      recent_table_->setItem(row, 1, make_item(native_or_empty(state_.recent_files[row])));
    }
    recent_table_->setSortingEnabled(true);
  }
}

void WorkspaceTab::populate_installations() {
  if (!installations_table_) {
    return;
  }
  installations_table_->setSortingEnabled(false);
  installations_table_->setRowCount(state_.installations.size());
  for (int row = 0; row < state_.installations.size(); ++row) {
    const GameSet& set = state_.installations[row];
    QStringList status;
    if (set.uid == state_.active_install.uid) {
      status.push_back("Active");
    }
    if (!set.root_dir.isEmpty() && !QFileInfo::exists(set.root_dir)) {
      status.push_back("Root missing");
    }
    if (!set.default_dir.isEmpty() && !QFileInfo::exists(set.default_dir)) {
      status.push_back("Default missing");
    }
    if (status.isEmpty()) {
      status.push_back("Ready");
    }
    installations_table_->setItem(row, 0, make_item(installation_name(set)));
    installations_table_->setItem(row, 1, make_item(game_display_name(set.game)));
    installations_table_->setItem(row, 2, make_item(native_or_empty(set.root_dir)));
    installations_table_->setItem(row, 3, make_item(native_or_empty(set.default_dir)));
    installations_table_->setItem(row, 4, make_item(status.join(", ")));
  }
  installations_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_search_results() {
  if (!search_results_table_) {
    return;
  }
  search_results_table_->setSortingEnabled(false);
  search_results_table_->setRowCount(search_results_.size());
  for (int row = 0; row < search_results_.size(); ++row) {
    const SearchResult& result = search_results_[row];
    QStringList state;
    if (result.is_added) {
      state.push_back("Added");
    }
    if (result.is_overridden) {
      state.push_back("Overrides archive");
    }
    if (state.isEmpty()) {
      state.push_back("Archive");
    }
    auto* archive_item = make_item(result.archive_title);
    archive_item->setData(Qt::UserRole, result.archive_path);
    search_results_table_->setItem(row, 0, archive_item);
    search_results_table_->setItem(row, 1, make_item(result.item_path));
    search_results_table_->setItem(row, 2, make_item(result.is_dir ? "Folder" : "File"));
    search_results_table_->setItem(row, 3, make_size_item(result.size));
    search_results_table_->setItem(row, 4, make_item(state.join(", ")));
    search_results_table_->setItem(row, 5, make_item(result.scope));
  }
  search_results_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_changes() {
  if (!changes_table_) {
    return;
  }
  QVector<ArchiveSummary> changed;
  for (const ArchiveSummary& archive : state_.archives) {
    if (archive.dirty || archive.added_count > 0 || archive.deleted_count > 0 || archive.protected_archive) {
      changed.push_back(archive);
    }
  }
  changes_table_->setSortingEnabled(false);
  changes_table_->setRowCount(changed.size());
  for (int row = 0; row < changed.size(); ++row) {
    const ArchiveSummary& archive = changed[row];
    changes_table_->setItem(row, 0, make_item(archive.title));
    changes_table_->setItem(row, 1, make_number_item(archive.added_count));
    changes_table_->setItem(row, 2, make_number_item(archive.deleted_count));
    changes_table_->setItem(row, 3, make_item(archive_state_text(archive)));
    changes_table_->setItem(row, 4, make_item(native_or_empty(archive.archive_path)));
  }
  changes_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_dependencies() {
  if (!dependencies_table_) {
    return;
  }
  dependencies_table_->setSortingEnabled(false);
  dependencies_table_->setRowCount(state_.dependency_notes.size());
  for (int row = 0; row < state_.dependency_notes.size(); ++row) {
    dependencies_table_->setItem(row, 0, make_item(state_.dependency_notes[row]));
  }
  dependencies_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_validation() {
  if (!validation_table_) {
    return;
  }
  const QStringList issues = state_.validation_issues.isEmpty()
    ? QStringList{"Workspace checks passed."}
    : state_.validation_issues;
  validation_table_->setSortingEnabled(false);
  validation_table_->setRowCount(issues.size());
  for (int row = 0; row < issues.size(); ++row) {
    validation_table_->setItem(row, 0, make_item(issues[row]));
  }
  validation_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_capabilities() {
  if (!capabilities_table_) {
    return;
  }
  capabilities_table_->setSortingEnabled(false);
  capabilities_table_->setRowCount(state_.capabilities.size());
  for (int row = 0; row < state_.capabilities.size(); ++row) {
    const Capability& capability = state_.capabilities[row];
    capabilities_table_->setItem(row, 0, make_item(capability.area));
    capabilities_table_->setItem(row, 1, make_item(capability.status));
    capabilities_table_->setItem(row, 2, make_item(capability.details));
  }
  capabilities_table_->setSortingEnabled(true);
}
