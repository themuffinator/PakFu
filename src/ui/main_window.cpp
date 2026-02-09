#include "main_window.h"

#include <functional>

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QKeySequence>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QSet>
#include <QSettings>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QStyle>
#include <QUndoStack>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>

#include "game/game_auto_detect.h"
#include "game/game_set.h"
#include "pakfu_config.h"
#include "ui/game_set_dialog.h"
#include "ui/pak_tab.h"
#include "ui/preferences_tab.h"
#include "update/update_service.h"

class DropOverlay : public QWidget {
public:
  explicit DropOverlay(QWidget* parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
  }

  void set_message(const QString& message) {
    message_ = message;
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 120, 212, 80));

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(24);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter, message_.isEmpty() ? "Drop to open" : message_);
  }

private:
  QString message_ = "Drop to open archive";
};

namespace {
constexpr int kMaxRecentFiles = 12;
constexpr char kLegacyRecentFilesKey[] = "ui/recentFiles";
constexpr char kRecentFilesByInstallKeyPrefix[] = "ui/recentFilesByInstall";
constexpr char kRecentFilesByInstallMigratedKey[] = "ui/recentFilesByInstallMigrated";
constexpr char kInstallWorkspaceKeyPrefix[] = "ui/installWorkspace";
constexpr char kInstallWorkspaceVersionKey[] = "ui/installWorkspaceVersion";
constexpr char kConfigureGameSetsUid[] = "__configure_game_sets__";

bool is_supported_archive_extension(const QString& path) {
  const QString lower = path.toLower();
  return lower.endsWith(".pak") || lower.endsWith(".pk3") ||
         lower.endsWith(".pk4") || lower.endsWith(".pkz") ||
         lower.endsWith(".zip") || lower.endsWith(".wad");
}

struct OpenableDropPaths {
  QStringList archives;
  QStringList folders;
};

OpenableDropPaths collect_openable_paths(const QList<QUrl>& urls) {
  OpenableDropPaths out;
  for (const QUrl& url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
      continue;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
      continue;
    }
    if (info.isDir()) {
      out.folders.append(info.absoluteFilePath());
      continue;
    }
    if (info.isFile() && is_supported_archive_extension(path)) {
      out.archives.append(info.absoluteFilePath());
    }
  }
  return out;
}

bool has_openable_paths(const OpenableDropPaths& drop) {
  return !(drop.archives.isEmpty() && drop.folders.isEmpty());
}

QString drop_overlay_message(const OpenableDropPaths& drop) {
  const bool has_archives = !drop.archives.isEmpty();
  const bool has_folders = !drop.folders.isEmpty();
  if (has_archives && has_folders) {
    return "Drop to open archives and folders";
  }
  if (has_archives) {
    return (drop.archives.size() > 1) ? "Drop to open archives" : "Drop to open archive";
  }
  if (has_folders) {
    return (drop.folders.size() > 1) ? "Drop to open folders" : "Drop to open folder";
  }
  return "Drop to open";
}

QString normalize_recent_path(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  return QFileInfo(path).absoluteFilePath();
}

bool recent_paths_equal(const QString& a, const QString& b) {
#if defined(Q_OS_WIN)
  return a.compare(b, Qt::CaseInsensitive) == 0;
#else
  return a == b;
#endif
}

QString recent_files_key_for_game_set_uid(const QString& uid) {
  if (uid.isEmpty()) {
    return QLatin1String(kLegacyRecentFilesKey);
  }
  QString key = QLatin1String(kRecentFilesByInstallKeyPrefix);
  key += '/';
  key += uid;
  return key;
}

QString clean_path(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool path_is_under(const QString& path, const QString& root) {
  const QString p = clean_path(path);
  QString r = clean_path(root);
  if (p.isEmpty() || r.isEmpty()) {
    return false;
  }
  if (!r.endsWith('/')) {
    r += '/';
  }
#if defined(Q_OS_WIN)
  return p.startsWith(r, Qt::CaseInsensitive);
#else
  return p.startsWith(r);
#endif
}

bool is_numbered_archive_name(const QString& lower_file, const QString& ext) {
  const QString suffix = "." + ext;
  if (!lower_file.endsWith(suffix)) {
    return false;
  }
  const QString base = lower_file.left(lower_file.size() - suffix.size());
  if (!base.startsWith("pak")) {
    return false;
  }
  const QString digits = base.mid(3);
  if (digits.isEmpty()) {
    return false;
  }
  for (const QChar c : digits) {
    if (!c.isDigit()) {
      return false;
    }
  }
  return true;
}

bool is_official_archive_name(GameId game, const QString& lower_file) {
  switch (game) {
    case GameId::Quake:
    case GameId::QuakeRerelease:
    case GameId::Quake2:
    case GameId::Quake2Rerelease:
      return is_numbered_archive_name(lower_file, "pak");
    case GameId::Quake3Arena:
    case GameId::QuakeLive:
      return is_numbered_archive_name(lower_file, "pk3");
    case GameId::Quake4:
      return is_numbered_archive_name(lower_file, "pk4");
  }
  return false;
}

bool is_official_archive_for_set(const GameSet& set, const QString& path) {
  if (path.isEmpty()) {
    return false;
  }
  const QString base_dir = !set.default_dir.isEmpty() ? set.default_dir : set.root_dir;
  if (base_dir.isEmpty()) {
    return false;
  }
  const QString abs = QFileInfo(path).absoluteFilePath();
  if (!path_is_under(abs, base_dir)) {
    return false;
  }
  const QString file_lower = QFileInfo(abs).fileName().toLower();
  return is_official_archive_name(set.game, file_lower);
}

QStringList normalize_recent_paths(const QStringList& files) {
  // Normalize, drop empty, de-dupe (preserve order).
  QStringList normalized;
  normalized.reserve(files.size());
  for (const QString& f : files) {
    const QString n = normalize_recent_path(f);
    if (n.isEmpty()) {
      continue;
    }
    bool seen = false;
    for (const QString& existing : normalized) {
      if (recent_paths_equal(existing, n)) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      normalized.push_back(n);
    }
    if (normalized.size() >= kMaxRecentFiles) {
      break;
    }
  }
  return normalized;
}

struct TabWorkspace {
  QString archive_path;
  QString dir_prefix;
  QString selected_path;
};

struct InstallWorkspace {
  QVector<TabWorkspace> tabs;
  QString current_archive_path;
};

QJsonObject tab_workspace_to_json(const TabWorkspace& tab) {
  QJsonObject obj;
  obj.insert("path", tab.archive_path);
  if (!tab.dir_prefix.isEmpty()) {
    obj.insert("dir", tab.dir_prefix);
  }
  if (!tab.selected_path.isEmpty()) {
    obj.insert("selected", tab.selected_path);
  }
  return obj;
}

TabWorkspace tab_workspace_from_json(const QJsonObject& obj) {
  TabWorkspace out;
  out.archive_path = obj.value("path").toString();
  out.dir_prefix = obj.value("dir").toString();
  out.selected_path = obj.value("selected").toString();
  return out;
}

QString workspace_key_for_install_uid(const QString& uid) {
  if (uid.isEmpty()) {
    return QString();
  }
  QString key = QLatin1String(kInstallWorkspaceKeyPrefix);
  key += '/';
  key += uid;
  return key;
}

class WelcomeBackdrop : public QWidget {
public:
  explicit WelcomeBackdrop(QWidget* parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    if (qEnvironmentVariableIsSet("PAKFU_DISABLE_WELCOME_BACKDROP_TEXT")) {
      return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont f = font();
    f.setPixelSize(160);
    f.setWeight(QFont::Black);
    painter.setFont(f);

    QColor c = palette().color(QPalette::Text);
    c.setAlphaF(0.06);
    painter.setPen(c);

    painter.drawText(rect(), Qt::AlignCenter, QString::fromUtf8(u8"存档很强大"));
  }
};

QToolButton* make_tab_close_button(QWidget* parent, const std::function<void()>& on_clicked) {
  auto* btn = new QToolButton(parent);
  btn->setText("x");
  btn->setAutoRaise(true);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setToolTip("Close");
  btn->setFixedSize(18, 18);
  QObject::connect(btn, &QToolButton::clicked, parent, on_clicked);
  return btn;
}

QString random_welcome_message() {
  const QStringList messages = {
    "Welcome to PakFu. Your archives will learn respect.",
    "PakFu loaded. Prepare your folders for combat.",
    "Inner peace achieved. Outer PAKs reorganised.",
    "The way of the PAK is strong today.",
    "Unzip with honour.",
    "Files do not fear PakFu. They should.",
    "One kick. Many folders.",
    "Your assets have entered the dojo.",
    "PAK management through disciplined violence.",
    "Balance restored. Archives aligned.",
    "Kung Fu is temporary. PakFu is forever.",
    "Folders fly. Files cry. PakFu smiles.",
    "Mastery is knowing when to extract.",
    "You bring the PAKs. PakFu brings the wisdom.",
    "The archive has been opened. So has your mind.",
    "Walk softly. Carry a big PAK.",
    "Meditate. Breathe. Rebuild index.",
    "The dojo is open. Drag and drop responsibly.",
    "A true master backs up first.",
    "Today is a good day to refactor an archive.",
  };

  if (messages.isEmpty()) {
    return "Welcome";
  }
  const int idx = static_cast<int>(QRandomGenerator::global()->bounded(messages.size()));
  return messages[idx];
}

QWidget* build_welcome_tab(QWidget* parent,
                          const std::function<void()>& on_new,
                          const std::function<void()>& on_open_archive,
                          const std::function<void()>& on_open_folder,
                          const std::function<void()>& on_exit) {
  auto* root = new QWidget(parent);

  // Background decoration (kept non-interactive so it never steals clicks).
  auto* stacked = new QStackedLayout(root);
  stacked->setStackingMode(QStackedLayout::StackAll);
  stacked->setContentsMargins(0, 0, 0, 0);

  auto* backdrop = new WelcomeBackdrop(root);
  stacked->addWidget(backdrop);

  auto* content = new QWidget(root);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(44, 40, 44, 40);
  layout->setSpacing(18);
  stacked->addWidget(content);

  layout->addStretch();

  auto* title = new QLabel(random_welcome_message(), content);
  title->setAlignment(Qt::AlignCenter);
  title->setWordWrap(true);
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 8);
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* subtitle = new QLabel("Create a new PAK, open an archive or folder, or exit.", content);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setWordWrap(true);
  layout->addWidget(subtitle);

  auto* button_row = new QHBoxLayout();
  button_row->addStretch();

  auto* create_button = new QPushButton("Create PAK", content);
  auto* open_button = new QPushButton("Open…", content);
  auto* open_menu = new QMenu(open_button);
  QAction* open_archive_action = open_menu->addAction("Open Archive...");
  QAction* open_folder_action = open_menu->addAction("Open Folder...");
  open_button->setMenu(open_menu);
  auto* close_button = new QPushButton("Close", content);
  create_button->setMinimumWidth(170);
  open_button->setMinimumWidth(170);
  close_button->setMinimumWidth(170);

  button_row->addWidget(create_button);
  button_row->addSpacing(18);
  button_row->addWidget(open_button);
  button_row->addSpacing(18);
  button_row->addWidget(close_button);
  button_row->addStretch();

  layout->addSpacing(10);
  layout->addLayout(button_row);
  layout->addStretch();

  QObject::connect(create_button, &QPushButton::clicked, root, on_new);
  QObject::connect(open_archive_action, &QAction::triggered, root, on_open_archive);
  QObject::connect(open_folder_action, &QAction::triggered, root, on_open_folder);
  QObject::connect(close_button, &QPushButton::clicked, root, on_exit);

  return root;
}
}  // namespace

MainWindow::MainWindow(const GameSet& game_set, const QString& initial_pak_path, bool schedule_updates)
    : game_set_(game_set), schedule_updates_(schedule_updates) {
  setAcceptDrops(true);
  setup_central();
  load_game_sets();
  rebuild_game_combo();
  setup_menus();

  drop_overlay_ = new DropOverlay(this);

  resize(1120, 760);

  updater_ = new UpdateService(this);
  updater_->configure(PAKFU_GITHUB_REPO, PAKFU_UPDATE_CHANNEL, PAKFU_VERSION);

  if (schedule_updates_) {
    schedule_update_check();
  }

  if (!initial_pak_path.isEmpty()) {
    open_pak(initial_pak_path);
  }

  update_window_title();
  if (initial_pak_path.isEmpty()) {
    restore_workspace_for_install(game_set_.uid);
  }
}

void MainWindow::open_archives(const QStringList& paths) {
  for (const QString& path : paths) {
    if (!path.isEmpty()) {
      open_pak(path);
    }
  }
}

QString MainWindow::default_directory_for_dialogs() const {
  if (!game_set_.default_dir.isEmpty() && QFileInfo::exists(game_set_.default_dir)) {
    return game_set_.default_dir;
  }
  if (!game_set_.root_dir.isEmpty() && QFileInfo::exists(game_set_.root_dir)) {
    return game_set_.root_dir;
  }
  return QDir::homePath();
}

bool MainWindow::pure_pak_protector_enabled() const {
  QSettings settings;
  return settings.value("archive/purePakProtector", true).toBool();
}

bool MainWindow::is_official_archive_for_current_install(const QString& path) const {
  return is_official_archive_for_set(game_set_, path);
}

void MainWindow::update_pure_pak_protector_for_tabs() {
  if (!tabs_) {
    return;
  }
  const bool enabled = pure_pak_protector_enabled();
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i));
    if (!pak_tab) {
      continue;
    }
    const bool official = !pak_tab->pak_path().isEmpty()
      && is_official_archive_for_current_install(pak_tab->pak_path());
    pak_tab->set_pure_pak_protector(enabled, official);
  }
  update_action_states();
}

void MainWindow::save_workspace_for_current_install() {
  if (game_set_.uid.isEmpty()) {
    return;
  }
  if (!tabs_ || restoring_workspace_) {
    return;
  }

  InstallWorkspace ws;
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* tab = qobject_cast<PakTab*>(tabs_->widget(i));
    if (!tab || tab->pak_path().isEmpty()) {
      continue;
    }
    TabWorkspace t;
    t.archive_path = QFileInfo(tab->pak_path()).absoluteFilePath();
    t.dir_prefix = tab->current_prefix();
    t.selected_path = tab->selected_pak_path(nullptr);
    ws.tabs.push_back(std::move(t));
    if (tabs_->currentWidget() == tab) {
      ws.current_archive_path = t.archive_path;
    }
  }

  QJsonObject root;
  root.insert("v", 1);
  QJsonArray arr;
  for (const TabWorkspace& t : ws.tabs) {
    arr.append(tab_workspace_to_json(t));
  }
  root.insert("tabs", arr);
  if (!ws.current_archive_path.isEmpty()) {
    root.insert("current", ws.current_archive_path);
  }

  QSettings settings;
  settings.setValue(kInstallWorkspaceVersionKey, 1);
  const QString key = workspace_key_for_install_uid(game_set_.uid);
  if (!key.isEmpty()) {
    settings.setValue(key, QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
  }
}

void MainWindow::restore_workspace_for_install(const QString& uid) {
  if (!tabs_ || uid.isEmpty()) {
    return;
  }

  clear_archive_tabs();

  QSettings settings;
  const QString key = workspace_key_for_install_uid(uid);
  if (key.isEmpty()) {
    return;
  }
  const QString raw = settings.value(key).toString().trimmed();
  if (raw.isEmpty()) {
    return;
  }

  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    return;
  }
  const QJsonObject root = doc.object();
  const QJsonArray arr = root.value("tabs").toArray();
  if (arr.isEmpty()) {
    return;
  }

  QVector<TabWorkspace> tabs;
  tabs.reserve(arr.size());
  for (const QJsonValue& v : arr) {
    if (!v.isObject()) {
      continue;
    }
    TabWorkspace t = tab_workspace_from_json(v.toObject());
    if (t.archive_path.isEmpty()) {
      continue;
    }
    tabs.push_back(std::move(t));
  }
  if (tabs.isEmpty()) {
    return;
  }

  const QString current_path = root.value("current").toString();

  restoring_workspace_ = true;
  for (const TabWorkspace& t : tabs) {
    PakTab* tab = open_pak_internal(t.archive_path, false, false);
    if (!tab) {
      continue;
    }
    tab->restore_workspace(t.dir_prefix, t.selected_path);
  }
  restoring_workspace_ = false;

  if (!current_path.isEmpty()) {
    focus_tab_by_path(current_path);
  }
}

void MainWindow::clear_archive_tabs() {
  if (!tabs_) {
    return;
  }
  for (int i = tabs_->count() - 1; i >= 0; --i) {
    auto* tab = qobject_cast<PakTab*>(tabs_->widget(i));
    if (tab) {
      close_tab(i);
    }
  }
}

void MainWindow::setup_central() {
  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Game selector row.
  auto* game_row = new QWidget(central);
  auto* game_layout = new QHBoxLayout(game_row);
  game_layout->setContentsMargins(12, 8, 12, 8);
  game_layout->setSpacing(10);

  auto* game_label = new QLabel("Installation", game_row);
  game_combo_ = new QComboBox(game_row);
  game_combo_->setMinimumWidth(280);
  game_combo_->setToolTip("Select the active installation");

  game_layout->addWidget(game_label, 0);
  game_layout->addWidget(game_combo_, 0);
  game_layout->addStretch(1);

  layout->addWidget(game_row, 0);

  tabs_ = new QTabWidget(central);
  tabs_->setDocumentMode(true);
  tabs_->setMovable(true);
  // We'll provide our own per-tab close button so it is always on the right.
  tabs_->setTabsClosable(false);
  layout->addWidget(tabs_, 1);
  setCentralWidget(central);

  welcome_tab_ = build_welcome_tab(
    tabs_,
    [this]() { create_new_pak(); },
    [this]() { open_pak_dialog(); },
    [this]() { open_folder_dialog(); },
    [this]() { close(); });
  tabs_->addTab(welcome_tab_, "Welcome");
  if (auto* bar = tabs_->tabBar()) {
    auto* close_btn = make_tab_close_button(bar, [this]() {
      if (!tabs_ || !welcome_tab_) {
        return;
      }
      const int idx = tabs_->indexOf(welcome_tab_);
      if (idx >= 0) {
        close_tab(idx);
      }
    });
    const int idx = tabs_->indexOf(welcome_tab_);
    if (idx >= 0) {
      bar->setTabButton(idx, QTabBar::RightSide, close_btn);
    }
  }

  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
    update_window_title();
    update_action_states();
    if (const PakTab* tab = current_pak_tab()) {
      auto_select_game_for_archive_path(tab->pak_path());
    }
  });
  connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_tab);

  if (game_combo_) {
    connect(game_combo_, &QComboBox::activated, this, [this](int idx) {
      if (!game_combo_ || updating_game_combo_) {
        return;
      }
      const QString uid = game_combo_->itemData(idx, Qt::UserRole).toString();
      if (uid == kConfigureGameSetsUid) {
        open_game_set_manager();
        return;
      }
      apply_game_set(uid, true);
    });
  }
}

void MainWindow::setup_menus() {
  auto* file_menu = menuBar()->addMenu("File");

  new_action_ = file_menu->addAction("New PAK");
  new_action_->setShortcut(QKeySequence::New);
  connect(new_action_, &QAction::triggered, this, &MainWindow::create_new_pak);

  open_action_ = file_menu->addAction("Open Archive...");
  open_action_->setShortcut(QKeySequence::Open);
  connect(open_action_, &QAction::triggered, this, &MainWindow::open_pak_dialog);

  open_folder_action_ = file_menu->addAction("Open Folder...");
  open_folder_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
  connect(open_folder_action_, &QAction::triggered, this, &MainWindow::open_folder_dialog);

  recent_files_menu_ = file_menu->addMenu("Recent Files");
  connect(recent_files_menu_, &QMenu::aboutToShow, this, &MainWindow::rebuild_recent_files_menu);
  rebuild_recent_files_menu();

  save_action_ = file_menu->addAction("Save");
  save_action_->setShortcut(QKeySequence::Save);
  connect(save_action_, &QAction::triggered, this, &MainWindow::save_current);

  save_as_action_ = file_menu->addAction("Save As...");
  save_as_action_->setShortcut(QKeySequence::SaveAs);
  connect(save_as_action_, &QAction::triggered, this, &MainWindow::save_current_as);

  file_menu->addSeparator();

  exit_action_ = file_menu->addAction("Exit");
  exit_action_->setShortcut(QKeySequence::Quit);
  connect(exit_action_, &QAction::triggered, this, &MainWindow::close);

  auto* edit_menu = menuBar()->addMenu("Edit");

  undo_action_ = edit_menu->addAction("Undo");
  undo_action_->setShortcut(QKeySequence::Undo);
  connect(undo_action_, &QAction::triggered, this, [this]() {
    if (PakTab* tab = current_pak_tab()) {
      tab->undo();
    }
  });

  redo_action_ = edit_menu->addAction("Redo");
  redo_action_->setShortcut(QKeySequence::Redo);
  connect(redo_action_, &QAction::triggered, this, [this]() {
    if (PakTab* tab = current_pak_tab()) {
      tab->redo();
    }
  });

  edit_menu->addSeparator();

  cut_action_ = edit_menu->addAction("Cut");
  cut_action_->setShortcut(QKeySequence::Cut);
  connect(cut_action_, &QAction::triggered, this, [this]() {
    if (PakTab* tab = current_pak_tab()) {
      tab->cut();
    }
  });

  copy_action_ = edit_menu->addAction("Copy");
  copy_action_->setShortcut(QKeySequence::Copy);
  connect(copy_action_, &QAction::triggered, this, [this]() {
    if (PakTab* tab = current_pak_tab()) {
      tab->copy();
    }
  });

  paste_action_ = edit_menu->addAction("Paste");
  paste_action_->setShortcut(QKeySequence::Paste);
  connect(paste_action_, &QAction::triggered, this, [this]() {
    if (PakTab* tab = current_pak_tab()) {
      tab->paste();
    }
  });

  rename_action_ = edit_menu->addAction("Rename");
  rename_action_->setShortcut(QKeySequence(Qt::Key_F2));
  connect(rename_action_, &QAction::triggered, this, [this]() {
    if (PakTab* tab = current_pak_tab()) {
      tab->rename();
    }
  });

  edit_menu->addSeparator();
  preferences_action_ = edit_menu->addAction("Preferences...");
  preferences_action_->setShortcut(QKeySequence::Preferences);
  connect(preferences_action_, &QAction::triggered, this, &MainWindow::open_preferences);

  auto* help_menu = menuBar()->addMenu("Help");
  auto* check_updates = help_menu->addAction("Check for Updates...");
  connect(check_updates, &QAction::triggered, this, &MainWindow::check_for_updates);

  auto* about = help_menu->addAction("About");
  connect(about, &QAction::triggered, this, [this]() {
    const QString repo = PAKFU_GITHUB_REPO;
    const QString repo_url = QString("https://github.com/%1").arg(repo);
    const QString website_url = "https://www.darkmatter-quake.com";
    const QString html = QString(
      "<b>PakFu %1</b><br/>"
      "A modern PAK file manager.<br/><br/>"
      "<b>Disclaimer</b><br/>"
      "Use of this software is at your own risk. No warranty is provided.<br/><br/>"
      "<b>License</b><br/>"
      "GNU General Public License v3 (GPLv3). See the LICENSE file.<br/><br/>"
      "<b>Source</b><br/>"
      "<a href=\"%2\">%3</a><br/><br/>"
      "<b>Website</b><br/>"
      "<a href=\"%4\">www.darkmatter-quake.com</a><br/><br/>"
      "Created by themuffinator, DarkMatter Productions.")
        .arg(PAKFU_VERSION, repo_url, repo_url, website_url);

    QMessageBox box(this);
    box.setWindowTitle("About PakFu");
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setText(html);
    box.setStandardButtons(QMessageBox::Ok);
    for (QLabel* label : box.findChildren<QLabel*>()) {
      label->setOpenExternalLinks(true);
      label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }
    box.exec();
  });

  update_action_states();
}

void MainWindow::schedule_update_check() {
  QSettings settings;
  const bool auto_check = settings.value("updates/autoCheck", true).toBool();
  if (!auto_check) {
    return;
  }

  const QDateTime last_check = settings.value("updates/lastCheckUtc").toDateTime();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (last_check.isValid() && last_check.secsTo(now) < 24 * 60 * 60) {
    return;
  }

  QTimer::singleShot(1500, this, [this]() {
    if (updater_) {
      updater_->check_for_updates(false, this);
    }
  });
}

void MainWindow::check_for_updates() {
  if (updater_) {
    updater_->check_for_updates(true, this);
  }
}

void MainWindow::create_new_pak() {
  const QString title = QString("Untitled %1").arg(untitled_counter_++);
  auto* tab = new PakTab(PakTab::Mode::NewPak, QString(), this);
  tab->set_default_directory(default_directory_for_dialogs());
  tab->set_game_id(game_set_.game);
  tab->set_pure_pak_protector(pure_pak_protector_enabled(), false);
  const int index = add_tab(title, tab);
  tabs_->setCurrentIndex(index);
}

void MainWindow::open_pak_dialog() {
  QFileDialog dialog(this);
  dialog.setWindowTitle("Open Archive");
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setNameFilters({
    "Archives (*.pak *.pk3 *.pk4 *.pkz *.zip *.wad)",
    "Quake PAK (*.pak)",
    "Quake WAD (*.wad)",
    "ZIP-based (PK3/PK4/PKZ/ZIP) (*.pk3 *.pk4 *.pkz *.zip)",
    "All files (*.*)",
  });
  dialog.setDirectory(default_directory_for_dialogs());
#if defined(Q_OS_WIN)
  // Work around sporadic native dialog crashes reported in early development.
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
    return;
  }
  open_pak(selected.first());
}

void MainWindow::open_folder_dialog() {
  QFileDialog dialog(this);
  dialog.setWindowTitle("Open Folder");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setDirectory(default_directory_for_dialogs());
#if defined(Q_OS_WIN)
  // Work around sporadic native dialog crashes reported in early development.
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
    return;
  }
  open_pak(selected.first());
}

PakTab* MainWindow::open_pak_internal(const QString& path, bool allow_auto_select, bool add_recent) {
  if (!tabs_) {
    return nullptr;
  }
  const QString normalized = normalize_recent_path(path);
  QFileInfo info(normalized.isEmpty() ? path : normalized);
  if (!info.exists()) {
    if (add_recent) {
      remove_recent_file(normalized.isEmpty() ? path : normalized);
    }
    QMessageBox::warning(this, "Open Archive", QString("Archive not found:\n%1").arg(path));
    return nullptr;
  }

  if (allow_auto_select) {
    auto_select_game_for_archive_path(info.absoluteFilePath());
  }

  if (focus_tab_by_path(info.absoluteFilePath())) {
    if (add_recent) {
      add_recent_file(info.absoluteFilePath());
    }
    return current_pak_tab();
  }

  QString error;
  auto* tab = new PakTab(PakTab::Mode::ExistingPak, info.absoluteFilePath(), this);
  tab->set_default_directory(default_directory_for_dialogs());
  tab->set_game_id(game_set_.game);
  if (!tab->is_loaded()) {
    error = tab->load_error();
    tab->deleteLater();
    QMessageBox::warning(this, "Open Archive", error.isEmpty() ? "Failed to load archive." : error);
    return nullptr;
  }
  tab->set_pure_pak_protector(pure_pak_protector_enabled(), is_official_archive_for_current_install(info.absoluteFilePath()));

  if (add_recent) {
    add_recent_file(info.absoluteFilePath());
  }

  const QString title = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
  const int index = add_tab(title, tab);
  tabs_->setTabToolTip(index, info.absoluteFilePath());
  tabs_->setCurrentIndex(index);
  return tab;
}

bool MainWindow::focus_tab_by_path(const QString& path) {
  if (!tabs_) {
    return false;
  }
  const QString normalized = QFileInfo(path).absoluteFilePath();
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i));
    if (!pak_tab) {
      continue;
    }
    if (pak_tab->pak_path().isEmpty()) {
      continue;
    }
    if (QFileInfo(pak_tab->pak_path()).absoluteFilePath() == normalized) {
      tabs_->setCurrentIndex(i);
      return true;
    }
  }
  return false;
}

int MainWindow::add_tab(const QString& title, QWidget* tab) {
  if (!tabs_) {
    return -1;
  }
  const int index = tabs_->addTab(tab, title);
  tabs_->setTabToolTip(index, title);
  set_tab_base_title(tab, title);
  if (auto* bar = tabs_->tabBar()) {
    auto* close_btn = make_tab_close_button(bar, [this, tab]() {
      if (!tabs_) {
        return;
      }
      const int idx = tabs_->indexOf(tab);
      if (idx >= 0) {
        close_tab(idx);
      }
    });
    bar->setTabButton(index, QTabBar::RightSide, close_btn);
  }

  if (auto* pak_tab = qobject_cast<PakTab*>(tab)) {
    connect(pak_tab, &PakTab::dirty_changed, this, [this, pak_tab](bool) {
      update_tab_label(pak_tab);
      update_action_states();
      update_window_title();
    });
    if (QUndoStack* stack = pak_tab->undo_stack()) {
      connect(stack, &QUndoStack::canUndoChanged, this, [this](bool) { update_action_states(); });
      connect(stack, &QUndoStack::canRedoChanged, this, [this](bool) { update_action_states(); });
    }
    update_tab_label(pak_tab);
  }
  return index;
}

PakTab* MainWindow::current_pak_tab() const {
  if (!tabs_) {
    return nullptr;
  }
  return qobject_cast<PakTab*>(tabs_->currentWidget());
}

void MainWindow::update_action_states() {
  const PakTab* pak_tab = current_pak_tab();
  const bool has_pak = pak_tab != nullptr;
  const bool loaded = pak_tab && pak_tab->is_loaded();
  const bool editable = pak_tab && pak_tab->is_editable();
  const bool protected_archive = pak_tab && pak_tab->is_pure_protected();
  if (save_action_) {
    const bool dirty_or_new = pak_tab && (pak_tab->is_dirty() || pak_tab->pak_path().isEmpty());
    save_action_->setEnabled(has_pak && loaded && dirty_or_new && !protected_archive);
  }
  if (save_as_action_) {
    save_as_action_->setEnabled(has_pak && loaded);
  }

  if (undo_action_) {
    const QUndoStack* stack = pak_tab ? pak_tab->undo_stack() : nullptr;
    undo_action_->setEnabled(has_pak && loaded && stack && stack->canUndo());
  }
  if (redo_action_) {
    const QUndoStack* stack = pak_tab ? pak_tab->undo_stack() : nullptr;
    redo_action_->setEnabled(has_pak && loaded && stack && stack->canRedo());
  }
  if (cut_action_) {
    cut_action_->setEnabled(has_pak && loaded && editable);
  }
  if (copy_action_) {
    copy_action_->setEnabled(has_pak && loaded);
  }
  if (paste_action_) {
    paste_action_->setEnabled(has_pak && loaded && editable);
  }
  if (rename_action_) {
    rename_action_->setEnabled(has_pak && loaded && editable);
  }
}

void MainWindow::open_pak(const QString& path) {
  (void)open_pak_internal(path, !restoring_workspace_, !restoring_workspace_);
}

void MainWindow::load_game_sets() {
  game_sets_ = load_game_set_state();
  if (game_sets_.sets.isEmpty()) {
    return;
  }

  // Ensure we have a valid selected UID.
  QString want = game_set_.uid;
  if (want.isEmpty()) {
    want = game_sets_.selected_uid;
  }

  const GameSet* set = find_game_set(game_sets_, want);
  if (!set && !game_sets_.selected_uid.isEmpty()) {
    set = find_game_set(game_sets_, game_sets_.selected_uid);
  }
  if (!set && !game_sets_.sets.isEmpty()) {
    set = &game_sets_.sets.first();
  }
  if (set) {
    game_set_ = *set;
    game_sets_.selected_uid = game_set_.uid;
  }
}

void MainWindow::rebuild_game_combo() {
  if (!game_combo_) {
    return;
  }

  updating_game_combo_ = true;
  game_combo_->clear();

  for (const GameSet& set : game_sets_.sets) {
    const QString label = set.name.isEmpty() ? game_display_name(set.game) : set.name;
    game_combo_->addItem(label, set.uid);
  }

  if (game_combo_->count() > 0) {
    game_combo_->insertSeparator(game_combo_->count());
    game_combo_->addItem(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "Configure Installations…", kConfigureGameSetsUid);
  } else {
    game_combo_->addItem(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "Configure Installations…", kConfigureGameSetsUid);
  }

  int idx = game_combo_->findData(game_set_.uid);
  if (idx < 0 && !game_sets_.selected_uid.isEmpty()) {
    idx = game_combo_->findData(game_sets_.selected_uid);
  }
  if (idx < 0 && !game_sets_.sets.isEmpty()) {
    idx = 0;
  }
  if (idx >= 0) {
    game_combo_->setCurrentIndex(idx);
  }

  updating_game_combo_ = false;
}

void MainWindow::apply_game_set(const QString& uid, bool persist_selection) {
  if (uid.isEmpty() || uid == kConfigureGameSetsUid) {
    return;
  }

  const GameSet* set = find_game_set(game_sets_, uid);
  if (!set) {
    return;
  }

  const bool changed = (game_set_.uid != set->uid);
  if (changed) {
    save_workspace_for_current_install();
    game_set_ = *set;
  }

  if (persist_selection) {
    game_sets_.selected_uid = uid;
    (void)save_game_set_state(game_sets_);
  }

  if (game_combo_) {
    const int idx = game_combo_->findData(uid);
    if (idx >= 0 && game_combo_->currentIndex() != idx) {
      updating_game_combo_ = true;
      game_combo_->setCurrentIndex(idx);
      updating_game_combo_ = false;
    }
  }

  // Apply updated default directory to all open tabs.
  if (tabs_) {
    for (int i = 0; i < tabs_->count(); ++i) {
      if (auto* tab = qobject_cast<PakTab*>(tabs_->widget(i))) {
        tab->set_default_directory(default_directory_for_dialogs());
        tab->set_game_id(game_set_.game);
      }
    }
  }

  update_window_title();
  rebuild_recent_files_menu();
  if (changed) {
    restore_workspace_for_install(uid);
  }
}

void MainWindow::open_game_set_manager() {
  GameSetDialog dialog(this);
  const int r = dialog.exec();

  // Reload after closing: the dialog can add/update/delete sets.
  load_game_sets();
  rebuild_game_combo();

  if (r != QDialog::Accepted) {
    return;
  }

  const auto selected = dialog.selected_game_set();
  if (!selected.has_value()) {
    return;
  }

  apply_game_set(selected->uid, true);
}

void MainWindow::auto_select_game_for_archive_path(const QString& path) {
  if (restoring_workspace_) {
    return;
  }
  if (path.isEmpty() || game_sets_.sets.isEmpty()) {
    return;
  }

  const QString clean = clean_path(path);
  if (clean.isEmpty()) {
    return;
  }

  const auto best_uid_by_roots = [&]() -> QString {
    QString best;
    int best_score = -1;

    for (const GameSet& set : game_sets_.sets) {
      int score = -1;
      if (!set.root_dir.isEmpty() && path_is_under(clean, set.root_dir)) {
        score = 2000 + set.root_dir.size();
      } else if (!set.default_dir.isEmpty() && path_is_under(clean, set.default_dir)) {
        score = 1500 + set.default_dir.size();
      }
      if (score > best_score) {
        best_score = score;
        best = set.uid;
      }
    }
    return best;
  };

  const QString best_by_roots = best_uid_by_roots();
  if (!best_by_roots.isEmpty() && best_by_roots != game_set_.uid) {
    apply_game_set(best_by_roots, true);
    return;
  }

  const auto detected = detect_game_id_for_path(clean);
  if (!detected.has_value()) {
    return;
  }

  for (const GameSet& set : game_sets_.sets) {
    if (set.game == *detected) {
      if (set.uid != game_set_.uid) {
        apply_game_set(set.uid, true);
      }
      return;
    }
  }
}

void MainWindow::save_current() {
  PakTab* tab = current_pak_tab();
  if (!tab) {
    return;
  }
  save_tab(tab);
}

void MainWindow::save_current_as() {
  PakTab* tab = current_pak_tab();
  if (!tab) {
    return;
  }
  save_tab_as(tab);
}

void MainWindow::open_preferences() {
  if (!tabs_) {
    return;
  }

  if (preferences_tab_) {
    const int idx = tabs_->indexOf(preferences_tab_);
    if (idx >= 0) {
      tabs_->setCurrentIndex(idx);
      return;
    }
    preferences_tab_ = nullptr;
  }

  preferences_tab_ = new PreferencesTab(this);
  connect(preferences_tab_, &PreferencesTab::model_texture_smoothing_changed, this, [this](bool enabled) {
    if (!tabs_) {
      return;
    }
    for (int i = 0; i < tabs_->count(); ++i) {
      if (auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i))) {
        pak_tab->set_model_texture_smoothing(enabled);
      }
    }
  });
  connect(preferences_tab_, &PreferencesTab::image_texture_smoothing_changed, this, [this](bool enabled) {
    if (!tabs_) {
      return;
    }
    for (int i = 0; i < tabs_->count(); ++i) {
      if (auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i))) {
        pak_tab->set_image_texture_smoothing(enabled);
      }
    }
  });
  connect(preferences_tab_, &PreferencesTab::preview_fov_changed, this, [this](int degrees) {
    if (!tabs_) {
      return;
    }
    for (int i = 0; i < tabs_->count(); ++i) {
      if (auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i))) {
        pak_tab->set_3d_fov_degrees(degrees);
      }
    }
  });
  connect(preferences_tab_, &PreferencesTab::preview_renderer_changed, this, [this](PreviewRenderer renderer) {
    if (!tabs_) {
      return;
    }
    for (int i = 0; i < tabs_->count(); ++i) {
      if (auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i))) {
        pak_tab->set_preview_renderer(renderer);
      }
    }
  });
  connect(preferences_tab_, &PreferencesTab::pure_pak_protector_changed, this, [this](bool) {
    update_pure_pak_protector_for_tabs();
  });
  const int idx = add_tab("Preferences", preferences_tab_);
  tabs_->setCurrentIndex(idx);
}

void MainWindow::update_window_title() {
  if (!tabs_) {
    setWindowTitle(game_set_.name.isEmpty() ? "PakFu" : QString("PakFu (%1)").arg(game_set_.name));
    return;
  }
  const int idx = tabs_->currentIndex();
  const QString tab_title = idx >= 0 ? tabs_->tabText(idx) : QString();
  const QString base = game_set_.name.isEmpty() ? "PakFu" : QString("PakFu (%1)").arg(game_set_.name);
  setWindowTitle(tab_title.isEmpty() ? base : QString("%1 - %2").arg(base, tab_title));
}

void MainWindow::close_tab(int index) {
  if (!tabs_) {
    return;
  }
  if (index < 0 || index >= tabs_->count()) {
    return;
  }

  QWidget* w = tabs_->widget(index);
  if (auto* pak_tab = qobject_cast<PakTab*>(w)) {
    if (!maybe_save_tab(pak_tab)) {
      return;
    }
  }

  if (auto* bar = tabs_->tabBar()) {
    if (QWidget* btn = bar->tabButton(index, QTabBar::RightSide)) {
      btn->deleteLater();
    }
    if (QWidget* btn = bar->tabButton(index, QTabBar::LeftSide)) {
      btn->deleteLater();
    }
  }
  tabs_->removeTab(index);
  if (w == welcome_tab_) {
    welcome_tab_ = nullptr;
  }
  if (w == preferences_tab_) {
    preferences_tab_ = nullptr;
  }
  if (w) {
    w->deleteLater();
  }
  // Keep the app open even if all tabs are closed (blank workspace).
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (!tabs_) {
    QMainWindow::closeEvent(event);
    return;
  }

  for (int i = 0; i < tabs_->count(); ++i) {
    auto* pak_tab = qobject_cast<PakTab*>(tabs_->widget(i));
    if (!pak_tab || !pak_tab->is_dirty()) {
      continue;
    }
    tabs_->setCurrentIndex(i);
    if (!maybe_save_tab(pak_tab)) {
      event->ignore();
      return;
    }
  }

  event->accept();
  save_workspace_for_current_install();
  QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event && event->mimeData() && event->mimeData()->hasUrls()) {
    const OpenableDropPaths drop = collect_openable_paths(event->mimeData()->urls());
    if (has_openable_paths(drop)) {
      if (drop_overlay_) {
        drop_overlay_->resize(size());
        drop_overlay_->set_message(drop_overlay_message(drop));
        drop_overlay_->show();
        drop_overlay_->raise();
      }
      event->acceptProposedAction();
      return;
    }
  }
  QMainWindow::dragEnterEvent(event);
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* event) {
  if (drop_overlay_) {
    drop_overlay_->hide();
  }
  QMainWindow::dragLeaveEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
  if (event && event->mimeData() && event->mimeData()->hasUrls()) {
    const OpenableDropPaths drop = collect_openable_paths(event->mimeData()->urls());
    if (has_openable_paths(drop)) {
      if (drop_overlay_) {
        drop_overlay_->set_message(drop_overlay_message(drop));
      }
      event->acceptProposedAction();
      return;
    }
  }
  QMainWindow::dragMoveEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (drop_overlay_) {
    drop_overlay_->hide();
  }
  if (event && event->mimeData() && event->mimeData()->hasUrls()) {
    const OpenableDropPaths drop = collect_openable_paths(event->mimeData()->urls());
    QStringList paths = drop.archives;
    paths.append(drop.folders);
    if (!paths.isEmpty()) {
      open_archives(paths);
      event->acceptProposedAction();
      return;
    }
  }
  QMainWindow::dropEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (drop_overlay_ && drop_overlay_->isVisible()) {
    drop_overlay_->resize(size());
  }
}

bool MainWindow::maybe_save_tab(PakTab* tab) {
  if (!tab || !tab->is_dirty()) {
    return true;
  }

  const QString title = tab_base_title(tab).isEmpty() ? "Untitled" : tab_base_title(tab);

  QMessageBox box(this);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle("Unsaved Changes");
  box.setText(QString("Save changes to \"%1\"?").arg(title));
  box.setInformativeText("If you don't save, your changes will be lost.");

  QPushButton* save = box.addButton("Save", QMessageBox::AcceptRole);
  QPushButton* discard = box.addButton("Discard", QMessageBox::DestructiveRole);
  QPushButton* cancel = box.addButton("Cancel", QMessageBox::RejectRole);
  box.setDefaultButton(save);
  box.exec();

  if (box.clickedButton() == save) {
    return save_tab(tab);
  }
  if (box.clickedButton() == discard) {
    Q_UNUSED(discard);
    return true;
  }
  Q_UNUSED(cancel);
  return false;
}

bool MainWindow::save_tab(PakTab* tab) {
  if (!tab) {
    return false;
  }
  if (tab->pak_path().isEmpty()) {
    return save_tab_as(tab);
  }

  QString err;
  if (!tab->save(&err)) {
    QMessageBox::warning(this, "Save Archive", err.isEmpty() ? "Unable to save archive." : err);
    return false;
  }

  update_tab_label(tab);
  update_action_states();
  return true;
}

bool MainWindow::save_tab_as(PakTab* tab) {
  if (!tab) {
    return false;
  }

  QString suggested = tab->pak_path();
  if (tab->archive_format() == Archive::Format::Directory) {
    QString base = tab_base_title(tab);
    if (base.isEmpty()) {
      base = QFileInfo(tab->pak_path()).fileName();
    }
    if (base.isEmpty()) {
      base = "folder";
    }
    if (!base.endsWith(".pak", Qt::CaseInsensitive)) {
      base += ".pak";
    }
    suggested = base;
  } else if (suggested.isEmpty()) {
    QString base = tab_base_title(tab);
    if (base.isEmpty()) {
      base = "untitled";
    }
    if (!base.endsWith(".pak", Qt::CaseInsensitive)) {
      base += ".pak";
    }
    suggested = base;
  }

  QFileDialog dialog(this);
  dialog.setWindowTitle("Save Archive As");
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setFileMode(QFileDialog::AnyFile);
  {
    QStringList filters;
    const Archive::Format fmt = tab->archive_format();
    const bool is_new = tab->pak_path().isEmpty();
    if (is_new || fmt == Archive::Format::Unknown || fmt == Archive::Format::Directory) {
      filters = {
        "Quake PAK (*.pak)",
        "PK3 (ZIP) (*.pk3)",
        "PK3 (Quake Live encrypted) (*.pk3)",
        "PK4 (ZIP) (*.pk4)",
        "PKZ (ZIP) (*.pkz)",
        "ZIP (*.zip)",
        "All files (*.*)",
      };
    } else if (fmt == Archive::Format::Pak) {
      filters = {
        "Quake PAK (*.pak)",
        "All files (*.*)",
      };
    } else {
      filters = {
        "PK3 (ZIP) (*.pk3)",
        "PK3 (Quake Live encrypted) (*.pk3)",
        "PK4 (ZIP) (*.pk4)",
        "PKZ (ZIP) (*.pkz)",
        "ZIP (*.zip)",
        "All files (*.*)",
      };
    }
    dialog.setNameFilters(filters);
  }
  dialog.setDirectory(default_directory_for_dialogs());
  dialog.selectFile(suggested);
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
    return false;
  }

  QString dest = selected.first();
  const QString filter = dialog.selectedNameFilter();

  PakTab::SaveOptions options;
  QString want_ext;
  if (filter.contains("Quake Live encrypted", Qt::CaseInsensitive)) {
    options.format = Archive::Format::Zip;
    options.quakelive_encrypt_pk3 = true;
    want_ext = ".pk3";
  } else if (filter.contains("PK3", Qt::CaseInsensitive)) {
    options.format = Archive::Format::Zip;
    want_ext = ".pk3";
  } else if (filter.contains("PK4", Qt::CaseInsensitive)) {
    options.format = Archive::Format::Zip;
    want_ext = ".pk4";
  } else if (filter.contains("PKZ", Qt::CaseInsensitive)) {
    options.format = Archive::Format::Zip;
    want_ext = ".pkz";
  } else if (filter.contains("ZIP", Qt::CaseInsensitive)) {
    options.format = Archive::Format::Zip;
    want_ext = ".zip";
  } else if (filter.contains("PAK", Qt::CaseInsensitive)) {
    options.format = Archive::Format::Pak;
    want_ext = ".pak";
  }

  if (!want_ext.isEmpty() && !dest.endsWith(want_ext, Qt::CaseInsensitive)) {
    const int sep = std::max(dest.lastIndexOf('/'), dest.lastIndexOf('\\'));
    const int dot = dest.lastIndexOf('.');
    const QString current_ext = (dot > sep) ? dest.mid(dot).toLower() : QString();
    const QSet<QString> known_exts = {".pak", ".pk3", ".pk4", ".pkz", ".zip"};
    if (known_exts.contains(current_ext) && dot > sep) {
      dest = dest.left(dot) + want_ext;
    } else {
      dest += want_ext;
    }
  }

  QString err;
  if (!tab->save_as(dest, options, &err)) {
    QMessageBox::warning(this, "Save Archive As", err.isEmpty() ? "Unable to save archive." : err);
    return false;
  }

  add_recent_file(QFileInfo(dest).absoluteFilePath());

  const QFileInfo info(tab->pak_path().isEmpty() ? dest : tab->pak_path());
  set_tab_base_title(tab, info.fileName());
  if (tabs_) {
    const int idx = tabs_->indexOf(tab);
    if (idx >= 0) {
      tabs_->setTabToolTip(idx, info.absoluteFilePath());
    }
  }

  update_window_title();
  update_action_states();
  return true;
}

void MainWindow::add_recent_file(const QString& path) {
  const QString normalized = normalize_recent_path(path);
  if (normalized.isEmpty()) {
    return;
  }

  QSettings settings;
  const QString key = recent_files_key_for_game_set_uid(game_set_.uid);
  QStringList files = settings.value(key).toStringList();

  for (int i = files.size() - 1; i >= 0; --i) {
    if (recent_paths_equal(files[i], normalized)) {
      files.removeAt(i);
    }
  }
  files.prepend(normalized);
  while (files.size() > kMaxRecentFiles) {
    files.removeLast();
  }

  settings.setValue(key, files);
  rebuild_recent_files_menu();
}

void MainWindow::remove_recent_file(const QString& path) {
  const QString normalized = normalize_recent_path(path);
  if (normalized.isEmpty()) {
    return;
  }

  QSettings settings;
  const QString key = recent_files_key_for_game_set_uid(game_set_.uid);
  QStringList files = settings.value(key).toStringList();
  bool changed = false;
  for (int i = files.size() - 1; i >= 0; --i) {
    if (recent_paths_equal(files[i], normalized)) {
      files.removeAt(i);
      changed = true;
    }
  }
  if (changed) {
    settings.setValue(key, files);
    rebuild_recent_files_menu();
  }
}

void MainWindow::clear_recent_files() {
  QSettings settings;
  const QString key = recent_files_key_for_game_set_uid(game_set_.uid);
  settings.remove(key);
  rebuild_recent_files_menu();
}

void MainWindow::rebuild_recent_files_menu() {
  if (!recent_files_menu_) {
    return;
  }

  QSettings settings;

  if (!settings.value(kRecentFilesByInstallMigratedKey, false).toBool() &&
      settings.contains(kLegacyRecentFilesKey) && !game_sets_.sets.isEmpty() && !game_set_.uid.isEmpty()) {
    const QStringList legacy = normalize_recent_paths(settings.value(kLegacyRecentFilesKey).toStringList());
    QHash<QString, QStringList> by_uid;

    auto best_uid_for_path = [this](const QString& p) -> QString {
      if (p.isEmpty() || game_sets_.sets.isEmpty()) {
        return {};
      }

      const QString clean = clean_path(p);
      if (clean.isEmpty()) {
        return {};
      }

      QString best;
      int best_score = -1;
      for (const GameSet& set : game_sets_.sets) {
        int score = -1;
        if (!set.root_dir.isEmpty() && path_is_under(clean, set.root_dir)) {
          score = 2000 + set.root_dir.size();
        } else if (!set.default_dir.isEmpty() && path_is_under(clean, set.default_dir)) {
          score = 1500 + set.default_dir.size();
        }
        if (score > best_score) {
          best_score = score;
          best = set.uid;
        }
      }
      if (!best.isEmpty()) {
        return best;
      }

      const auto detected = detect_game_id_for_path(clean);
      if (!detected.has_value()) {
        return {};
      }
      for (const GameSet& set : game_sets_.sets) {
        if (set.game == *detected) {
          return set.uid;
        }
      }
      return {};
    };

    for (const QString& path : legacy) {
      QString uid = best_uid_for_path(path);
      if (uid.isEmpty()) {
        uid = game_set_.uid;
      }
      QStringList& list = by_uid[uid];
      list.push_back(path);
    }

    for (auto it = by_uid.begin(); it != by_uid.end(); ++it) {
      QStringList list = normalize_recent_paths(it.value());
      while (list.size() > kMaxRecentFiles) {
        list.removeLast();
      }
      settings.setValue(recent_files_key_for_game_set_uid(it.key()), list);
    }

    settings.remove(kLegacyRecentFilesKey);
    settings.setValue(kRecentFilesByInstallMigratedKey, true);
  }

  const QString key = recent_files_key_for_game_set_uid(game_set_.uid);
  QStringList files = settings.value(key).toStringList();

  const QStringList normalized = normalize_recent_paths(files);

  if (normalized != files) {
    settings.setValue(key, normalized);
  }

  recent_files_menu_->clear();
  recent_files_menu_->setToolTipsVisible(true);

  if (normalized.isEmpty()) {
    QAction* none = recent_files_menu_->addAction("(No recent files)");
    none->setEnabled(false);
  } else {
    for (int i = 0; i < normalized.size(); ++i) {
      const QString path = normalized[i];
      const QFileInfo info(path);
      QString label = info.fileName().isEmpty() ? path : info.fileName();
      label.replace("&", "&&");

      QString text;
      if (i < 9) {
        text = QString("&%1 %2").arg(i + 1).arg(label);
      } else {
        text = QString("%1 %2").arg(i + 1).arg(label);
      }

      QAction* act = recent_files_menu_->addAction(text);
      act->setToolTip(path);
      act->setStatusTip(path);
      if (!info.exists()) {
        act->setText(text + " (missing)");
        act->setEnabled(false);
        continue;
      }
      connect(act, &QAction::triggered, this, [this, path]() { open_pak(path); });
    }
  }

  recent_files_menu_->addSeparator();
  QAction* clear = recent_files_menu_->addAction("Clear Recent Files");
  clear->setEnabled(!normalized.isEmpty());
  connect(clear, &QAction::triggered, this, &MainWindow::clear_recent_files);
}

void MainWindow::set_tab_base_title(QWidget* tab, const QString& title) {
  if (!tab) {
    return;
  }
  tab->setProperty("pakfu_base_title", title);
  update_tab_label(tab);
}

QString MainWindow::tab_base_title(QWidget* tab) const {
  if (!tab) {
    return {};
  }
  const QVariant v = tab->property("pakfu_base_title");
  return v.isValid() ? v.toString() : QString();
}

void MainWindow::update_tab_label(QWidget* tab) {
  if (!tabs_ || !tab) {
    return;
  }
  const int idx = tabs_->indexOf(tab);
  if (idx < 0) {
    return;
  }

  QString base = tab_base_title(tab);
  if (base.isEmpty()) {
    base = tabs_->tabText(idx);
    if (base.endsWith('*')) {
      base.chop(1);
    }
  }

  QString label = base;
  if (auto* pak_tab = qobject_cast<PakTab*>(tab)) {
    if (pak_tab->is_dirty()) {
      label += "*";
    }
  }

  tabs_->setTabText(idx, label);
}
