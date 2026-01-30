#include "main_window.h"

#include <functional>

#include <QAction>
#include <QDateTime>
#include <QCloseEvent>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "pakfu_config.h"
#include "ui/pak_tab.h"
#include "ui/preferences_tab.h"
#include "update/update_service.h"

namespace {
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
                          const std::function<void()>& on_open,
                          const std::function<void()>& on_exit) {
  auto* root = new QWidget(parent);
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(44, 40, 44, 40);
  layout->setSpacing(18);

  layout->addStretch();

  auto* title = new QLabel(random_welcome_message(), root);
  title->setAlignment(Qt::AlignCenter);
  title->setWordWrap(true);
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 8);
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* subtitle = new QLabel("Create a new PAK, open an existing one, or exit.", root);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setWordWrap(true);
  layout->addWidget(subtitle);

  auto* button_row = new QHBoxLayout();
  button_row->addStretch();

  auto* create_button = new QPushButton("Create PAK", root);
  auto* load_button = new QPushButton("Open PAK", root);
  auto* close_button = new QPushButton("Close", root);
  create_button->setMinimumWidth(170);
  load_button->setMinimumWidth(170);
  close_button->setMinimumWidth(170);

  button_row->addWidget(create_button);
  button_row->addSpacing(18);
  button_row->addWidget(load_button);
  button_row->addSpacing(18);
  button_row->addWidget(close_button);
  button_row->addStretch();

  layout->addSpacing(10);
  layout->addLayout(button_row);
  layout->addStretch();

  QObject::connect(create_button, &QPushButton::clicked, root, on_new);
  QObject::connect(load_button, &QPushButton::clicked, root, on_open);
  QObject::connect(close_button, &QPushButton::clicked, root, on_exit);

  return root;
}
}  // namespace

MainWindow::MainWindow(const QString& initial_pak_path, bool schedule_updates)
    : schedule_updates_(schedule_updates) {
  setup_central();
  setup_menus();

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
}

void MainWindow::setup_central() {
  tabs_ = new QTabWidget(this);
  tabs_->setDocumentMode(true);
  tabs_->setMovable(true);
  // We'll provide our own per-tab close button so it is always on the right.
  tabs_->setTabsClosable(false);
  setCentralWidget(tabs_);

  welcome_tab_ = build_welcome_tab(
    tabs_,
    [this]() { create_new_pak(); },
    [this]() { open_pak_dialog(); },
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
  });
  connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_tab);
}

void MainWindow::setup_menus() {
  auto* file_menu = menuBar()->addMenu("File");

  new_action_ = file_menu->addAction("New PAK");
  new_action_->setShortcut(QKeySequence::New);
  connect(new_action_, &QAction::triggered, this, &MainWindow::create_new_pak);

  open_action_ = file_menu->addAction("Open PAK...");
  open_action_->setShortcut(QKeySequence::Open);
  connect(open_action_, &QAction::triggered, this, &MainWindow::open_pak_dialog);

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
  const int index = add_tab(title, tab);
  tabs_->setCurrentIndex(index);
}

void MainWindow::open_pak_dialog() {
  QFileDialog dialog(this);
  dialog.setWindowTitle("Open PAK");
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setNameFilters({"PAK files (*.pak)", "All files (*.*)"});
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
  if (save_action_) {
    const bool dirty_or_new = pak_tab && (pak_tab->is_dirty() || pak_tab->pak_path().isEmpty());
    save_action_->setEnabled(has_pak && loaded && dirty_or_new);
  }
  if (save_as_action_) {
    save_as_action_->setEnabled(has_pak && loaded);
  }
}

void MainWindow::open_pak(const QString& path) {
  if (!tabs_) {
    return;
  }
  QFileInfo info(path);
  if (!info.exists()) {
    QMessageBox::warning(this, "Open PAK", QString("PAK not found:\n%1").arg(path));
    return;
  }

  if (focus_tab_by_path(path)) {
    return;
  }

  QString error;
  auto* tab = new PakTab(PakTab::Mode::ExistingPak, info.absoluteFilePath(), this);
  if (!tab->is_loaded()) {
    error = tab->load_error();
    tab->deleteLater();
    QMessageBox::warning(this, "Open PAK", error.isEmpty() ? "Failed to load PAK." : error);
    return;
  }

  const QString title = info.fileName();
  const int index = add_tab(title, tab);
  tabs_->setTabToolTip(index, info.absoluteFilePath());
  tabs_->setCurrentIndex(index);
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
  const int idx = add_tab("Preferences", preferences_tab_);
  tabs_->setCurrentIndex(idx);
}

void MainWindow::update_window_title() {
  if (!tabs_) {
    setWindowTitle("PakFu");
    return;
  }
  const int idx = tabs_->currentIndex();
  const QString tab_title = idx >= 0 ? tabs_->tabText(idx) : QString();
  setWindowTitle(tab_title.isEmpty() ? "PakFu" : QString("PakFu - %1").arg(tab_title));
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
  QMainWindow::closeEvent(event);
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
    QMessageBox::warning(this, "Save PAK", err.isEmpty() ? "Unable to save PAK." : err);
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
  if (suggested.isEmpty()) {
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
  dialog.setWindowTitle("Save PAK As");
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setNameFilters({"PAK files (*.pak)", "All files (*.*)"});
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
  if (!dest.endsWith(".pak", Qt::CaseInsensitive)) {
    dest += ".pak";
  }

  QString err;
  if (!tab->save_as(dest, &err)) {
    QMessageBox::warning(this, "Save PAK As", err.isEmpty() ? "Unable to save PAK." : err);
    return false;
  }

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
