#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMenuBar>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTextStream>
#include <QToolButton>
#include <QWidget>

#include "cli/cli.h"
#include "pakfu_config.h"
#include "update/update_service.h"
#include "ui/main_window.h"
#include "ui/splash_screen.h"
#include "ui/theme_manager.h"

namespace {
void set_app_metadata(QCoreApplication& app) {
  app.setApplicationName("PakFu");
  app.setOrganizationName("PakFu");
  app.setApplicationVersion(PAKFU_VERSION);
}

QString find_initial_pak(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg.startsWith('-')) {
      continue;
    }
    const QFileInfo info(arg);
    if (info.exists()) {
      return info.absoluteFilePath();
    }
  }
  return {};
}

bool should_check_updates() {
  QSettings settings;
  return settings.value("updates/autoCheck", true).toBool();
}

QPixmap load_logo_pixmap() {
  QPixmap pixmap(":/assets/img/logo.png");
  if (pixmap.isNull()) {
    pixmap.load("assets/img/logo.png");
  }
  return pixmap;
}

SplashScreen* show_splash(QApplication& app) {
  QPixmap logo = load_logo_pixmap();
  if (logo.isNull()) {
    return nullptr;
  }

  QScreen* screen = app.primaryScreen();
  if (!screen) {
    return nullptr;
  }

  const int target_height = static_cast<int>(screen->availableGeometry().height() * 0.5);
  if (target_height <= 0) {
    return nullptr;
  }

  QPixmap scaled = logo.scaledToHeight(target_height, Qt::SmoothTransformation);
  auto* splash = new SplashScreen(scaled);
  splash->move(screen->availableGeometry().center() - QPoint(scaled.width() / 2, scaled.height() / 2));
  splash->show();
  splash->raise();
  splash->setStatusText("Checking for updates...");
  splash->setVersionText(QString("v%1").arg(PAKFU_VERSION));
  app.processEvents();
  return splash;
}

void run_tab_smoke_test(MainWindow& window) {
  if (!qEnvironmentVariableIsSet("PAKFU_SMOKE_TABS")) {
    return;
  }

  auto find_action = [&](const QString& text) -> QAction* {
    const QList<QAction*> actions = window.findChildren<QAction*>();
    for (QAction* a : actions) {
      if (!a) {
        continue;
      }
      if (a->text().replace("&", "") == text) {
        return a;
      }
    }
    return nullptr;
  };

  auto click_tab_close = [&](QTabWidget* tabs) {
    if (!tabs) {
      return;
    }
    QTabBar* bar = tabs->tabBar();
    if (!bar) {
      return;
    }
    const int idx = tabs->currentIndex();
    if (idx < 0) {
      return;
    }
    QWidget* btn = bar->tabButton(idx, QTabBar::RightSide);
    if (!btn) {
      return;
    }
    if (auto* tool = qobject_cast<QToolButton*>(btn)) {
      tool->click();
      return;
    }
    QMetaObject::invokeMethod(btn, "click", Qt::QueuedConnection);
  };

  QTimer::singleShot(250, &window, [&]() {
    if (QAction* act = find_action("New PAK")) {
      act->trigger();
    }
  });

  QTimer::singleShot(600, &window, [&]() {
    auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
    click_tab_close(tabs);
  });

  QTimer::singleShot(900, &window, [&]() {
    auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
    if (!tabs) {
      return;
    }
    if (tabs->count() > 0) {
      tabs->setCurrentIndex(0);
    }
    click_tab_close(tabs);
  });

  // If we got this far without exploding, close cleanly.
  QTimer::singleShot(1400, &window, [&]() { window.close(); });
}
}  // namespace

int main(int argc, char** argv) {
  if (wants_cli(argc, argv)) {
    QCoreApplication app(argc, argv);
    set_app_metadata(app);

    CliOptions options;
    QString output;
    const CliParseResult result = parse_cli(app, options, &output);
    if (result == CliParseResult::ExitOk) {
      if (!output.isEmpty()) {
        QTextStream(stdout) << output;
      }
      return 0;
    }
    if (result == CliParseResult::ExitError) {
      if (!output.isEmpty()) {
        QTextStream(stderr) << output;
      }
      return 1;
    }

    return run_cli(options);
  }

  QApplication app(argc, argv);
  set_app_metadata(app);
  ThemeManager::apply_saved_theme(app);

  const QString initial_pak = find_initial_pak(argc, argv);
  MainWindow window(initial_pak, false);

  QPointer<SplashScreen> splash = show_splash(app);
  if (splash) {
    // Prevent the app from exiting when the splash is the only visible window.
    app.setQuitOnLastWindowClosed(false);
  }
  bool main_shown = false;
  bool update_finished = false;
  QPointer<UpdateService> updater;

  auto finish_and_show = [&]() {
    if (main_shown) {
      return;
    }
    main_shown = true;
    window.show();
    window.raise();
    window.activateWindow();
    if (splash) {
      splash->close();
      splash->deleteLater();
      splash = nullptr;
    }
    app.setQuitOnLastWindowClosed(true);
    run_tab_smoke_test(window);
  };

  if (should_check_updates()) {
    updater = new UpdateService(&app);
    updater->configure(PAKFU_GITHUB_REPO, PAKFU_UPDATE_CHANNEL, PAKFU_VERSION);
    QObject::connect(updater, &UpdateService::check_completed, &app,
                     [&, updater](const UpdateCheckResult& result) {
                       Q_UNUSED(updater);
                       if (update_finished) {
                         return;
                       }
                       update_finished = true;

                       if (splash) {
                         QString status;
                         switch (result.state) {
                           case UpdateCheckState::UpdateAvailable:
                             status = result.info.version.isEmpty()
                                        ? "Update available."
                                        : QString("Update available: %1").arg(result.info.version);
                             break;
                           case UpdateCheckState::UpToDate:
                             status = "You are up to date.";
                             break;
                           case UpdateCheckState::NoRelease:
                             status = "No releases found.";
                             break;
                           case UpdateCheckState::NotConfigured:
                             status = "Update source not configured.";
                             break;
                           case UpdateCheckState::Error:
                             status = result.message.isEmpty() ? "Update check failed." : result.message;
                             break;
                         }
                         splash->setStatusText(status);
                       }

                       QTimer::singleShot(0, &app, [&, result]() {
                         Q_UNUSED(result);
                         finish_and_show();
                       });
                     });

    QTimer::singleShot(100, &app, [&, updater]() {
      if (updater) {
        QWidget* parent = splash ? static_cast<QWidget*>(splash.data()) : static_cast<QWidget*>(&window);
        updater->check_for_updates(false, parent);
      }
    });

  } else {
    finish_and_show();
  }

  return app.exec();
}
