#include "ui/workspace_tab.h"

#include <utility>

#include <QAbstractItemView>
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QModelIndex>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QToolBar>
#include <QVariant>
#include <QVBoxLayout>

#include "ui/ui_icons.h"

namespace {
QString wt(const char* text) {
	return QCoreApplication::translate("WorkspaceTab", text);
}

QStandardItem* make_item(const QString& text) {
	auto* item = new QStandardItem(text);
	item->setEditable(false);
	return item;
}

QStandardItem* make_number_item(qint64 value) {
	auto* item = make_item(QString::number(value));
	item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	item->setData(value, Qt::UserRole);
	return item;
}

QStandardItem* make_size_item(quint32 bytes) {
	QString text = QString::number(bytes);
	if (bytes >= 1024 * 1024) {
		text = QString("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
	} else if (bytes >= 1024) {
		text = QString("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
	}
	auto* item = make_item(text);
	item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	item->setData(static_cast<qulonglong>(bytes), Qt::UserRole);
	return item;
}

void tune_table(QTableView* table, const QString& accessible_name) {
	if (!table) {
		return;
	}
	table->setAccessibleName(accessible_name);
	table->setAlternatingRowColors(true);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSortingEnabled(true);
	table->setWordWrap(false);
	table->setCornerButtonEnabled(false);
	table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	table->verticalHeader()->setVisible(false);
	table->horizontalHeader()->setHighlightSections(false);
}

QStandardItemModel* make_model(QObject* parent, const QStringList& headers) {
	auto* model = new QStandardItemModel(0, headers.size(), parent);
	model->setHorizontalHeaderLabels(headers);
	return model;
}

void reset_model(QStandardItemModel* model, int rows) {
	if (!model) {
		return;
	}
	model->removeRows(0, model->rowCount());
	model->setRowCount(rows);
}

QString model_text(const QStandardItemModel* model, int row, int column) {
	if (!model || row < 0 || row >= model->rowCount() || column < 0 || column >= model->columnCount()) {
		return {};
	}
	const QStandardItem* item = model->item(row, column);
	return item ? item->text() : QString();
}

QVariant model_data(const QStandardItemModel* model, int row, int column, int role) {
	if (!model || row < 0 || row >= model->rowCount() || column < 0 || column >= model->columnCount()) {
		return {};
	}
	const QStandardItem* item = model->item(row, column);
	return item ? item->data(role) : QVariant();
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
	bits.push_back(summary.loaded ? wt("Loaded") : wt("Not loaded"));
	if (summary.dirty) {
		bits.push_back(wt("Modified"));
	}
	if (summary.protected_archive) {
		bits.push_back(wt("Protected"));
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
	setAccessibleName(wt("Workspace"));
	build_ui();
	set_state(State{});
}

QString WorkspaceTab::search_query() const {
	return search_edit_ ? search_edit_->text() : QString();
}

void WorkspaceTab::focus_search(const QString& query) {
	if (section_nav_) {
		for (int row = 0; row < section_nav_->count(); ++row) {
			const QListWidgetItem* item = section_nav_->item(row);
			if (item && item->text() == wt("Search")) {
				section_nav_->setCurrentRow(row);
				break;
			}
		}
	}
	if (!search_edit_) {
		return;
	}
	if (!query.isNull()) {
		search_edit_->setText(query);
		run_search();
	}
	search_edit_->setFocus(Qt::ShortcutFocusReason);
	search_edit_->selectAll();
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

	auto* action_bar = new QToolBar(this);
	action_bar->setAccessibleName(wt("Workspace actions"));
	action_bar->setIconSize(QSize(18, 18));
	action_bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	action_bar->setMovable(false);
	action_bar->setFloatable(false);

	auto* new_action = action_bar->addAction(UiIcons::icon(UiIcons::Id::NewPak, style()), wt("New"));
	new_action->setToolTip(wt("Create a new archive"));
	connect(new_action, &QAction::triggered, this, [this]() {
		if (callbacks_.new_archive) {
			callbacks_.new_archive();
		}
	});

	auto* open_file_action = action_bar->addAction(UiIcons::icon(UiIcons::Id::OpenArchive, style()), wt("Open File"));
	open_file_action->setToolTip(wt("Open any supported file"));
	connect(open_file_action, &QAction::triggered, this, [this]() {
		if (callbacks_.open_file) {
			callbacks_.open_file();
		}
	});

	auto* open_archive_action =
	    action_bar->addAction(UiIcons::icon(UiIcons::Id::OpenArchive, style()), wt("Quick Inspect Archive"));
	open_archive_action->setToolTip(wt("Open an archive in place for immediate inspection"));
	connect(open_archive_action, &QAction::triggered, this, [this]() {
		if (callbacks_.open_archive) {
			callbacks_.open_archive();
		}
	});

	auto* open_folder_action = action_bar->addAction(UiIcons::icon(UiIcons::Id::OpenFolder, style()), wt("Open Folder"));
	open_folder_action->setToolTip(wt("Open a folder as an editable archive view"));
	connect(open_folder_action, &QAction::triggered, this, [this]() {
		if (callbacks_.open_folder) {
			callbacks_.open_folder();
		}
	});

	auto* spacer = new QWidget(action_bar);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	action_bar->addWidget(spacer);

	auto* installs_action = action_bar->addAction(UiIcons::icon(UiIcons::Id::Configure, style()), wt("Installations"));
	installs_action->setToolTip(wt("Manage installation profiles"));
	connect(installs_action, &QAction::triggered, this, [this]() {
		if (callbacks_.manage_installations) {
			callbacks_.manage_installations();
		}
	});

	auto* refresh_action = action_bar->addAction(UiIcons::icon(UiIcons::Id::AutoDetect, style()), wt("Refresh"));
	refresh_action->setToolTip(wt("Refresh workspace status"));
	connect(refresh_action, &QAction::triggered, this, [this]() {
		if (callbacks_.refresh) {
			callbacks_.refresh();
		}
	});

	root->addWidget(action_bar);

	auto* summary = new QWidget(this);
	auto* summary_layout = new QGridLayout(summary);
	summary_layout->setContentsMargins(0, 0, 0, 0);
	summary_layout->setHorizontalSpacing(18);
	summary_layout->setVerticalSpacing(6);
	summary_layout->addWidget(make_section_label(wt("Installation"), summary), 0, 0);
	summary_layout->addWidget(make_section_label(wt("Open Archives"), summary), 0, 1);
	summary_layout->addWidget(make_section_label(wt("Recent Files"), summary), 0, 2);
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

	auto* body = new QSplitter(Qt::Horizontal, this);
	body->setChildrenCollapsible(false);
	section_nav_ = new QListWidget(body);
	section_nav_->setAccessibleName(wt("Workspace lenses"));
	section_nav_->setSelectionMode(QAbstractItemView::SingleSelection);
	section_nav_->setUniformItemSizes(true);
	section_nav_->setMinimumWidth(150);
	section_nav_->setMaximumWidth(230);
	body->addWidget(section_nav_);

	sections_ = new QStackedWidget(body);
	sections_->setAccessibleName(wt("Workspace lens content"));
	body->addWidget(sections_);
	body->setStretchFactor(0, 0);
	body->setStretchFactor(1, 1);
	root->addWidget(body, 1);

	connect(section_nav_, &QListWidget::currentRowChanged, sections_, &QStackedWidget::setCurrentIndex);

	auto* overview = new QWidget(sections_);
	auto* overview_layout = new QVBoxLayout(overview);
	overview_layout->setContentsMargins(0, 0, 0, 0);
	overview_layout->setSpacing(10);
	overview_layout->addWidget(make_section_label(wt("Open archives"), overview));
	archives_table_ = new QTableView(overview);
	archives_model_ = make_model(archives_table_,
	                              {wt("Archive"), wt("Format"), wt("Entries"), wt("Added"), wt("Deleted"),
	                               wt("Folder"), wt("State"), wt("Path")});
	archives_table_->setModel(archives_model_);
	tune_table(archives_table_, wt("Open archives table"));
	archives_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	archives_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	archives_table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
	overview_layout->addWidget(archives_table_, 3);
	connect(archives_table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
		if (!callbacks_.focus_archive || !index.isValid()) {
			return;
		}
		const QString path = model_text(archives_model_, index.row(), 7);
		callbacks_.focus_archive(QDir::fromNativeSeparators(path));
	});

	overview_layout->addWidget(make_section_label(wt("Recent files"), overview));
	recent_table_ = new QTableView(overview);
	recent_model_ = make_model(recent_table_, {wt("File"), wt("Path")});
	recent_table_->setModel(recent_model_);
	tune_table(recent_table_, wt("Recent files table"));
	recent_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	recent_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	overview_layout->addWidget(recent_table_, 2);
	connect(recent_table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
		if (!callbacks_.focus_archive || !index.isValid()) {
			return;
		}
		const QString path = model_text(recent_model_, index.row(), 1);
		callbacks_.focus_archive(QDir::fromNativeSeparators(path));
	});
	add_section(wt("Overview"), overview);

	auto* install_page = new QWidget(sections_);
	auto* install_layout = new QVBoxLayout(install_page);
	install_layout->setContentsMargins(0, 0, 0, 0);
	installations_table_ = new QTableView(install_page);
	installations_model_ = make_model(installations_table_,
	                                   {wt("Installation"), wt("Game"), wt("Root"), wt("Default Folder"), wt("State")});
	installations_table_->setModel(installations_model_);
	tune_table(installations_table_, wt("Installations table"));
	installations_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	installations_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	installations_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	install_layout->addWidget(installations_table_);
	add_section(wt("Installations"), install_page);

	auto* search_page = new QWidget(sections_);
	auto* search_layout = new QVBoxLayout(search_page);
	search_layout->setContentsMargins(0, 0, 0, 0);
	search_layout->setSpacing(8);
	auto* search_row = new QHBoxLayout();
	search_edit_ = new QLineEdit(search_page);
	search_edit_->setAccessibleName(wt("Search open archives"));
	search_edit_->setPlaceholderText(wt("Search open archives"));
	search_edit_->setClearButtonEnabled(true);
	search_row->addWidget(search_edit_, 1);
	auto* search_button = new QPushButton(wt("Search"), search_page);
	search_button->setIcon(UiIcons::icon(UiIcons::Id::Details, style()));
	search_button->setAccessibleName(wt("Run workspace search"));
	search_row->addWidget(search_button);
	search_layout->addLayout(search_row);
	connect(search_edit_, &QLineEdit::returnPressed, this, &WorkspaceTab::run_search);
	connect(search_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
		if (text.trimmed().isEmpty()) {
			run_search();
		}
	});
	connect(search_button, &QPushButton::clicked, this, &WorkspaceTab::run_search);

	search_results_table_ = new QTableView(search_page);
	search_results_model_ = make_model(search_results_table_,
	                                   {wt("Archive"), wt("Path"), wt("Type"), wt("Size"), wt("State"), wt("Scope")});
	search_results_table_->setModel(search_results_model_);
	tune_table(search_results_table_, wt("Workspace search results table"));
	search_results_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	search_results_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	search_results_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	search_layout->addWidget(search_results_table_, 1);
	connect(search_results_table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
		if (!callbacks_.focus_archive || !index.isValid()) {
			return;
		}
		callbacks_.focus_archive(model_data(search_results_model_, index.row(), 0, Qt::UserRole).toString());
	});
	add_section(wt("Search"), search_page);

	auto* changes_page = new QWidget(sections_);
	auto* changes_layout = new QVBoxLayout(changes_page);
	changes_layout->setContentsMargins(0, 0, 0, 0);
	changes_table_ = new QTableView(changes_page);
	changes_model_ = make_model(changes_table_, {wt("Archive"), wt("Added"), wt("Deleted"), wt("State"), wt("Path")});
	changes_table_->setModel(changes_model_);
	tune_table(changes_table_, wt("Changed archives table"));
	changes_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	changes_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	changes_layout->addWidget(changes_table_);
	add_section(wt("Changes"), changes_page);

	auto* deps_page = new QWidget(sections_);
	auto* deps_layout = new QVBoxLayout(deps_page);
	deps_layout->setContentsMargins(0, 0, 0, 0);
	dependencies_table_ = new QTableView(deps_page);
	dependencies_model_ = make_model(dependencies_table_, {wt("Dependency Hints")});
	dependencies_table_->setModel(dependencies_model_);
	tune_table(dependencies_table_, wt("Dependency hints table"));
	dependencies_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	deps_layout->addWidget(dependencies_table_);
	add_section(wt("Dependencies"), deps_page);

	auto* validation_page = new QWidget(sections_);
	auto* validation_layout = new QVBoxLayout(validation_page);
	validation_layout->setContentsMargins(0, 0, 0, 0);
	validation_table_ = new QTableView(validation_page);
	validation_model_ = make_model(validation_table_, {wt("Workspace Checks")});
	validation_table_->setModel(validation_model_);
	tune_table(validation_table_, wt("Workspace checks table"));
	validation_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	validation_layout->addWidget(validation_table_);
	add_section(wt("Validation"), validation_page);

	auto* capabilities_page = new QWidget(sections_);
	auto* capabilities_layout = new QVBoxLayout(capabilities_page);
	capabilities_layout->setContentsMargins(0, 0, 0, 0);
	capabilities_table_ = new QTableView(capabilities_page);
	capabilities_model_ = make_model(capabilities_table_, {wt("Area"), wt("Status"), wt("Details")});
	capabilities_table_->setModel(capabilities_model_);
	tune_table(capabilities_table_, wt("Runtime capabilities table"));
	capabilities_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	capabilities_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	capabilities_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	capabilities_layout->addWidget(capabilities_table_);
	add_section(wt("Capabilities"), capabilities_page);

	section_nav_->setCurrentRow(0);
}

void WorkspaceTab::add_section(const QString& label, QWidget* page) {
	if (!section_nav_ || !sections_ || !page) {
		return;
	}
	auto* item = new QListWidgetItem(label, section_nav_);
	item->setToolTip(label);
	sections_->addWidget(page);
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
			details.push_back(QString("%1: %2").arg(wt("Root"), native_or_empty(state_.active_install.root_dir)));
		}
		if (!state_.active_install.default_dir.isEmpty()) {
			details.push_back(QString("%1: %2").arg(wt("Default"), native_or_empty(state_.active_install.default_dir)));
		}
		install_detail_label_->setText(details.join("   "));
	}
	if (archive_count_label_) {
		archive_count_label_->setText(QString::number(state_.archives.size()));
	}
	if (recent_count_label_) {
		recent_count_label_->setText(QString::number(state_.recent_files.size()));
	}

	if (archives_table_ && archives_model_) {
		archives_table_->setSortingEnabled(false);
		reset_model(archives_model_, state_.archives.size());
		for (int row = 0; row < state_.archives.size(); ++row) {
			const ArchiveSummary& archive = state_.archives[row];
			archives_model_->setItem(row, 0, make_item(archive.title));
			archives_model_->setItem(row, 1, make_item(archive.format));
			archives_model_->setItem(row, 2, make_number_item(archive.entry_count));
			archives_model_->setItem(row, 3, make_number_item(archive.added_count));
			archives_model_->setItem(row, 4, make_number_item(archive.deleted_count));
			archives_model_->setItem(row, 5, make_item(archive.current_prefix.isEmpty() ? QStringLiteral("/") : archive.current_prefix));
			archives_model_->setItem(row, 6, make_item(archive_state_text(archive)));
			archives_model_->setItem(row, 7, make_item(native_or_empty(archive.archive_path)));
		}
		archives_table_->setSortingEnabled(true);
	}

	if (recent_table_ && recent_model_) {
		recent_table_->setSortingEnabled(false);
		reset_model(recent_model_, state_.recent_files.size());
		for (int row = 0; row < state_.recent_files.size(); ++row) {
			const QFileInfo info(state_.recent_files[row]);
			recent_model_->setItem(row, 0, make_item(info.fileName().isEmpty() ? state_.recent_files[row] : info.fileName()));
			recent_model_->setItem(row, 1, make_item(native_or_empty(state_.recent_files[row])));
		}
		recent_table_->setSortingEnabled(true);
	}
}

void WorkspaceTab::populate_installations() {
	if (!installations_table_ || !installations_model_) {
		return;
	}
	installations_table_->setSortingEnabled(false);
	reset_model(installations_model_, state_.installations.size());
	for (int row = 0; row < state_.installations.size(); ++row) {
		const GameSet& set = state_.installations[row];
		QStringList status;
		if (set.uid == state_.active_install.uid) {
			status.push_back(wt("Active"));
		}
		if (!set.root_dir.isEmpty() && !QFileInfo::exists(set.root_dir)) {
			status.push_back(wt("Root missing"));
		}
		if (!set.default_dir.isEmpty() && !QFileInfo::exists(set.default_dir)) {
			status.push_back(wt("Default missing"));
		}
		if (status.isEmpty()) {
			status.push_back(wt("Ready"));
		}
		installations_model_->setItem(row, 0, make_item(installation_name(set)));
		installations_model_->setItem(row, 1, make_item(game_display_name(set.game)));
		installations_model_->setItem(row, 2, make_item(native_or_empty(set.root_dir)));
		installations_model_->setItem(row, 3, make_item(native_or_empty(set.default_dir)));
		installations_model_->setItem(row, 4, make_item(status.join(", ")));
	}
	installations_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_search_results() {
	if (!search_results_table_ || !search_results_model_) {
		return;
	}
	search_results_table_->setSortingEnabled(false);
	reset_model(search_results_model_, search_results_.size());
	for (int row = 0; row < search_results_.size(); ++row) {
		const SearchResult& result = search_results_[row];
		QStringList state;
		if (result.is_added) {
			state.push_back(wt("Added"));
		}
		if (result.is_overridden) {
			state.push_back(wt("Overrides archive"));
		}
		if (state.isEmpty()) {
			state.push_back(wt("Archive"));
		}
		auto* archive_item = make_item(result.archive_title);
		archive_item->setData(result.archive_path, Qt::UserRole);
		search_results_model_->setItem(row, 0, archive_item);
		search_results_model_->setItem(row, 1, make_item(result.item_path));
		search_results_model_->setItem(row, 2, make_item(result.is_dir ? wt("Folder") : wt("File")));
		search_results_model_->setItem(row, 3, make_size_item(result.size));
		search_results_model_->setItem(row, 4, make_item(state.join(", ")));
		search_results_model_->setItem(row, 5, make_item(result.scope));
	}
	search_results_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_changes() {
	if (!changes_table_ || !changes_model_) {
		return;
	}
	QVector<ArchiveSummary> changed;
	for (const ArchiveSummary& archive : state_.archives) {
		if (archive.dirty || archive.added_count > 0 || archive.deleted_count > 0 || archive.protected_archive) {
			changed.push_back(archive);
		}
	}
	changes_table_->setSortingEnabled(false);
	reset_model(changes_model_, changed.size());
	for (int row = 0; row < changed.size(); ++row) {
		const ArchiveSummary& archive = changed[row];
		changes_model_->setItem(row, 0, make_item(archive.title));
		changes_model_->setItem(row, 1, make_number_item(archive.added_count));
		changes_model_->setItem(row, 2, make_number_item(archive.deleted_count));
		changes_model_->setItem(row, 3, make_item(archive_state_text(archive)));
		changes_model_->setItem(row, 4, make_item(native_or_empty(archive.archive_path)));
	}
	changes_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_dependencies() {
	if (!dependencies_table_ || !dependencies_model_) {
		return;
	}
	dependencies_table_->setSortingEnabled(false);
	reset_model(dependencies_model_, state_.dependency_notes.size());
	for (int row = 0; row < state_.dependency_notes.size(); ++row) {
		dependencies_model_->setItem(row, 0, make_item(state_.dependency_notes[row]));
	}
	dependencies_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_validation() {
	if (!validation_table_ || !validation_model_) {
		return;
	}
	const QStringList issues = state_.validation_issues.isEmpty()
	                             ? QStringList{wt("Workspace checks passed.")}
	                             : state_.validation_issues;
	validation_table_->setSortingEnabled(false);
	reset_model(validation_model_, issues.size());
	for (int row = 0; row < issues.size(); ++row) {
		validation_model_->setItem(row, 0, make_item(issues[row]));
	}
	validation_table_->setSortingEnabled(true);
}

void WorkspaceTab::populate_capabilities() {
	if (!capabilities_table_ || !capabilities_model_) {
		return;
	}
	capabilities_table_->setSortingEnabled(false);
	reset_model(capabilities_model_, state_.capabilities.size());
	for (int row = 0; row < state_.capabilities.size(); ++row) {
		const Capability& capability = state_.capabilities[row];
		capabilities_model_->setItem(row, 0, make_item(capability.area));
		capabilities_model_->setItem(row, 1, make_item(capability.status));
		capabilities_model_->setItem(row, 2, make_item(capability.details));
	}
	capabilities_table_->setSortingEnabled(true);
}
