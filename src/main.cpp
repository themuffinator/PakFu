#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QTextStream>
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
  };

  if (should_check_updates()) {
    updater = new UpdateService(&app);
    updater->configure(PAKFU_GITHUB_REPO, PAKFU_UPDATE_CHANNEL, PAKFU_VERSION);
    QObject::connect(updater, &UpdateService::check_completed, &app,
                     [&, updater](const UpdateCheckResult& result) {
                       if (update_finished) {
                         return;
                       }
                       update_finished = true;
                       if (splash) {
                         switch (result.state) {
                           case UpdateCheckState::UpdateAvailable:
                             splash->setStatusText("Update available.");
                             break;
                           case UpdateCheckState::UpToDate:
                             splash->setStatusText("You are up to date.");
                             break;
                           case UpdateCheckState::NoRelease:
                             splash->setStatusText("No releases found.");
                             break;
                           case UpdateCheckState::NotConfigured:
                             splash->setStatusText("Update source not configured.");
                             break;
                         case UpdateCheckState::Error:
                           splash->setStatusText("Update check failed.");
                           break;
                       }
                     }
                       QTimer::singleShot(0, &app, [&, result]() {
                         finish_and_show();
                         Q_UNUSED(result);
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
