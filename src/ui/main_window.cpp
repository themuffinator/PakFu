#include "main_window.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "pakfu_config.h"
#include "update/update_service.h"

MainWindow::MainWindow(const QString& initial_pak_path, bool schedule_updates)
    : schedule_updates_(schedule_updates) {
  setWindowTitle("PakFu");
  setup_central();
  resize(1000, 700);

  updater_ = new UpdateService(this);
  updater_->configure(PAKFU_GITHUB_REPO, PAKFU_UPDATE_CHANNEL, PAKFU_VERSION);

  setup_menus();

  if (schedule_updates_) {
    schedule_update_check();
  }

  if (!initial_pak_path.isEmpty()) {
    open_pak(initial_pak_path);
  }
}

void MainWindow::setup_central() {
  stack_ = new QStackedWidget(this);

  auto* start_page = new QWidget(stack_);
  auto* start_layout = new QVBoxLayout(start_page);
  start_layout->setContentsMargins(40, 40, 40, 40);
  start_layout->addStretch();

  auto* title = new QLabel("PakFu Triage", start_page);
  title->setAlignment(Qt::AlignCenter);
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 6);
  title->setFont(title_font);
  start_layout->addWidget(title);

  auto* subtitle = new QLabel("Choose what to do next.", start_page);
  subtitle->setAlignment(Qt::AlignCenter);
  start_layout->addWidget(subtitle);

  auto* button_row = new QHBoxLayout();
  auto* create_button = new QPushButton("Create PAK", start_page);
  auto* load_button = new QPushButton("Open PAK", start_page);
  auto* close_button = new QPushButton("Close", start_page);
  create_button->setMinimumWidth(160);
  load_button->setMinimumWidth(160);
  close_button->setMinimumWidth(160);
  button_row->addStretch();
  button_row->addWidget(create_button);
  button_row->addSpacing(20);
  button_row->addWidget(load_button);
  button_row->addSpacing(20);
  button_row->addWidget(close_button);
  button_row->addStretch();
  start_layout->addSpacing(20);
  start_layout->addLayout(button_row);
  start_layout->addStretch();

  connect(create_button, &QPushButton::clicked, this, [this]() {
    QMessageBox::information(this, "Create PAK", "PAK creation is not implemented yet.");
  });

  connect(load_button, &QPushButton::clicked, this, [this]() {
    const QString file_path = QFileDialog::getOpenFileName(this, "Open PAK", QString(), "PAK files (*.pak);;All files (*.*)");
    if (!file_path.isEmpty()) {
      open_pak(file_path);
    }
  });

  connect(close_button, &QPushButton::clicked, this, [this]() {
    close();
  });

  auto* content_page = new QWidget(stack_);
  auto* content_layout = new QVBoxLayout(content_page);
  content_layout->setContentsMargins(40, 40, 40, 40);
  status_label_ = new QLabel("No PAK loaded.", content_page);
  status_label_->setAlignment(Qt::AlignCenter);
  content_layout->addWidget(status_label_);

  stack_->addWidget(start_page);
  stack_->addWidget(content_page);
  stack_->setCurrentWidget(start_page);
  setCentralWidget(stack_);
  setWindowTitle("PakFu - Triage");
}

void MainWindow::setup_menus() {
  auto* help_menu = menuBar()->addMenu("Help");
  auto* check_updates = help_menu->addAction("Check for Updates...");
  connect(check_updates, &QAction::triggered, this, &MainWindow::check_for_updates);

  auto* about = help_menu->addAction("About");
  connect(about, &QAction::triggered, this, [this]() {
    QMessageBox::about(this, "About PakFu",
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

void MainWindow::open_pak(const QString& path) {
  if (!stack_ || !status_label_) {
    return;
  }
  status_label_->setText(QString("Loaded PAK:\n%1").arg(path));
  stack_->setCurrentIndex(1);
  setWindowTitle(QString("PakFu - %1").arg(QFileInfo(path).fileName()));
}
