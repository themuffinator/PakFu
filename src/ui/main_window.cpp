#include "main_window.h"

#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QDateTime>

#include "pakfu_config.h"
#include "update/update_service.h"

MainWindow::MainWindow() {
  setWindowTitle("PakFu");
  auto* label = new QLabel("PakFu UI scaffold", this);
  label->setAlignment(Qt::AlignCenter);
  setCentralWidget(label);
  resize(1000, 700);

  updater_ = new UpdateService(this);
  updater_->configure(PAKFU_GITHUB_REPO, PAKFU_UPDATE_CHANNEL, PAKFU_VERSION);

  setup_menus();
  schedule_update_check();
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
