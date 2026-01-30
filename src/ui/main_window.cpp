#include "main_window.h"

#include <functional>

#include <QAction>
#include <QDateTime>
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
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "pakfu_config.h"
#include "ui/pak_tab.h"
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

  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { update_window_title(); });
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

  file_menu->addSeparator();

  exit_action_ = file_menu->addAction("Exit");
  exit_action_->setShortcut(QKeySequence::Quit);
  connect(exit_action_, &QAction::triggered, this, &MainWindow::close);

  auto* help_menu = menuBar()->addMenu("Help");
  auto* check_updates = help_menu->addAction("Check for Updates...");
  connect(check_updates, &QAction::triggered, this, &MainWindow::check_for_updates);

  auto* about = help_menu->addAction("About");
  connect(about, &QAction::triggered, this, [this]() {
    QMessageBox::about(this,
                       "About PakFu",
                       QString("PakFu %1\nA modern PAK file manager.")
                         .arg(PAKFU_VERSION));
  });
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
  const int index = add_pak_tab(title, tab);
  tabs_->setCurrentIndex(index);
}

void MainWindow::open_pak_dialog() {
  const QString file_path =
    QFileDialog::getOpenFileName(this, "Open PAK", QString(), "PAK files (*.pak);;All files (*.*)");
  if (!file_path.isEmpty()) {
    open_pak(file_path);
  }
}

void MainWindow::focus_tab_by_path(const QString& path) {
  if (!tabs_) {
    return;
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
      return;
    }
  }
}

int MainWindow::add_pak_tab(const QString& title, QWidget* tab) {
  if (!tabs_) {
    return -1;
  }
  const int index = tabs_->addTab(tab, title);
  tabs_->setTabToolTip(index, title);
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
  return index;
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

  focus_tab_by_path(path);
  if (tabs_->currentWidget() && qobject_cast<PakTab*>(tabs_->currentWidget())) {
    // Already focused.
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
  const int index = add_pak_tab(title, tab);
  tabs_->setTabToolTip(index, info.absoluteFilePath());
  tabs_->setCurrentIndex(index);
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

  if (auto* bar = tabs_->tabBar()) {
    if (QWidget* btn = bar->tabButton(index, QTabBar::RightSide)) {
      btn->deleteLater();
    }
    if (QWidget* btn = bar->tabButton(index, QTabBar::LeftSide)) {
      btn->deleteLater();
    }
  }
  QWidget* w = tabs_->widget(index);
  tabs_->removeTab(index);
  if (w == welcome_tab_) {
    welcome_tab_ = nullptr;
  }
  if (w) {
    w->deleteLater();
  }
  if (tabs_->count() == 0) {
    close();
  }
}
