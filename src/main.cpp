#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenuBar>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QThread>
#include <QTextStream>
#include <QToolButton>
#include <QWidget>

#include <QCryptographicHash>

#include <memory>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "cli/cli.h"
#include "game/game_set.h"
#include "pakfu_config.h"
#include "update/update_service.h"
#include "ui/game_set_dialog.h"
#include "ui/main_window.h"
#include "ui/splash_screen.h"
#include "ui/theme_manager.h"

namespace {
void set_app_metadata(QCoreApplication& app) {
  app.setApplicationName("PakFu");
  app.setOrganizationName("PakFu");
  app.setApplicationVersion(PAKFU_VERSION);
}

bool dir_contains_ffmpeg_media_plugin(const QString& dir_path) {
  if (dir_path.isEmpty()) {
    return false;
  }
  const QDir d(dir_path);
  if (!d.exists()) {
    return false;
  }
  const QStringList matches = d.entryList(QStringList() << "*ffmpegmediaplugin*", QDir::Files);
  return !matches.isEmpty();
}

void prefer_qt_ffmpeg_backend_if_available() {
  // Respect explicit user/system choice.
  if (qEnvironmentVariableIsSet("QT_MEDIA_BACKEND")) {
    return;
  }

  // Try to detect availability of the Qt FFmpeg multimedia backend plugin.
  // If it's present, prefer it so formats like OGV (Theora/Vorbis) work on platforms where
  // the native backend may not support them.
  const QStringList roots = QCoreApplication::libraryPaths();
  for (const QString& root : roots) {
    const QString multimedia_dir = QDir(root).filePath("multimedia");
    const QString plugins_multimedia_dir = QDir(root).filePath("plugins/multimedia");
    const QString plug_ins_multimedia_dir = QDir(root).filePath("PlugIns/multimedia");
    if (dir_contains_ffmpeg_media_plugin(multimedia_dir) ||
        dir_contains_ffmpeg_media_plugin(plugins_multimedia_dir) ||
        dir_contains_ffmpeg_media_plugin(plug_ins_multimedia_dir)) {
      qputenv("QT_MEDIA_BACKEND", "ffmpeg");
      return;
    }
  }
}

#ifdef Q_OS_WIN
QString resolve_executable_dir_winapi() {
  DWORD cap = MAX_PATH;
  for (int attempt = 0; attempt < 8; ++attempt) {
    std::wstring buf;
    buf.resize(cap);
    DWORD got = GetModuleFileNameW(nullptr, buf.data(), cap);
    if (got == 0) {
      return {};
    }
    if (got < cap - 1) {
      buf.resize(got);
      const QString path = QString::fromWCharArray(buf.c_str(), static_cast<int>(buf.size()));
      return QFileInfo(path).absolutePath();
    }
    cap *= 2;
  }
  return {};
}

void configure_qt_plugin_paths_for_local_deploy(const QString& exe_dir) {
  const auto unset_if_missing = [](const char* name) {
    if (!qEnvironmentVariableIsSet(name)) {
      return;
    }
    const QString value = qEnvironmentVariable(name);
    if (value.isEmpty()) {
      return;
    }
    if (!QFileInfo::exists(value)) {
      qunsetenv(name);
    }
  };

  // Avoid invalid hard-coded paths (common in editor launch configs).
  unset_if_missing("QT_QPA_PLATFORM_PLUGIN_PATH");
  unset_if_missing("QT_PLUGIN_PATH");

  if (exe_dir.isEmpty()) {
    return;
  }

  const QString platforms_dir = QDir(exe_dir).filePath("platforms");
  const bool has_local_platforms = QFileInfo::exists(platforms_dir);

  // If plugins were deployed next to the executable (via windeployqt), prefer those over any
  // environment-provided Qt installation path to avoid version/ABI mismatches.
  if (has_local_platforms) {
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platforms_dir.toLocal8Bit());
    qputenv("QT_PLUGIN_PATH", exe_dir.toLocal8Bit());
  }
}
#endif

bool is_archive_path(const QString& path) {
  const QString lower = path.toLower();
  const int dot = lower.lastIndexOf('.');
  const QString ext = dot >= 0 ? lower.mid(dot + 1) : QString();
  static const QSet<QString> kExts = {"pak", "pk3", "pk4", "pkz", "zip", "wad", "wad2", "wad3"};
  return kExts.contains(ext);
}

QStringList find_initial_archives(int argc, char** argv) {
  QStringList paths;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg.startsWith('-')) {
      continue;
    }
    const QFileInfo info(arg);
    if (!info.exists() || !info.isFile()) {
      continue;
    }
    const QString abs = info.absoluteFilePath();
    if (!is_archive_path(abs)) {
      continue;
    }
    paths.push_back(abs);
  }

#if defined(Q_OS_WIN)
  auto eq = [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) == 0; };
#else
  auto eq = [](const QString& a, const QString& b) { return a == b; };
#endif

  QStringList unique;
  unique.reserve(paths.size());
  for (const QString& p : paths) {
    bool seen = false;
    for (const QString& u : unique) {
      if (eq(u, p)) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      unique.push_back(p);
    }
  }
  return unique;
}

QString single_instance_server_name() {
  // Per-user stable name (prevents different accounts colliding).
  const QByteArray home = QDir::homePath().toUtf8();
  const QByteArray hash = QCryptographicHash::hash(home, QCryptographicHash::Sha1).toHex();
  return QString("PakFu-%1").arg(QString::fromLatin1(hash.left(12)));
}

QByteArray build_ipc_payload(const QStringList& paths, bool focus) {
  QJsonObject root;
  root.insert("v", 1);
  root.insert("focus", focus);
  QJsonArray arr;
  for (const QString& p : paths) {
    if (!p.isEmpty()) {
      arr.append(p);
    }
  }
  root.insert("paths", arr);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool send_ipc_payload(const QString& server_name, const QByteArray& payload) {
  // Be tolerant of slow startup or temporary contention so we don't accidentally
  // spawn a second UI window when an instance is already running.
  for (int attempt = 0; attempt < 6; ++attempt) {
    QLocalSocket socket;
    socket.connectToServer(server_name, QIODevice::WriteOnly);
    const int connect_ms = 400 + attempt * 400;
    if (!socket.waitForConnected(connect_ms)) {
      QThread::msleep(50);
      continue;
    }

    const qint64 wrote = socket.write(payload);
    socket.flush();
    const bool ok = (wrote == payload.size()) && socket.waitForBytesWritten(1200);
    socket.disconnectFromServer();
    if (ok) {
      return true;
    }

    QThread::msleep(50);
  }
  return false;
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

QIcon load_app_icon() {
  QStringList candidates;
#if defined(Q_OS_WIN)
  candidates = {
      ":/assets/img/pakfu-icon-256.ico",
      ":/assets/img/pakfu-icon-256.png",
      "assets/img/pakfu-icon-256.ico",
      "assets/img/pakfu-icon-256.png",
  };
#elif defined(Q_OS_MACOS)
  candidates = {
      ":/assets/img/pakfu-icon-256.icns",
      ":/assets/img/pakfu-icon-256.png",
      "assets/img/pakfu-icon-256.icns",
      "assets/img/pakfu-icon-256.png",
  };
#elif defined(Q_OS_LINUX)
  candidates = {
      ":/assets/img/pakfu-icon-256.png",
      ":/assets/img/pakfu-icon.png",
      "assets/img/pakfu-icon-256.png",
      "assets/img/pakfu-icon.png",
  };
#else
  candidates = {
      ":/assets/img/pakfu-icon-256.png",
      "assets/img/pakfu-icon-256.png",
      ":/assets/img/pakfu-icon-256.ico",
      "assets/img/pakfu-icon-256.ico",
  };
#endif

  for (const QString& candidate : candidates) {
    QIcon icon(candidate);
    if (!icon.isNull()) {
      return icon;
    }
  }
  return {};
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
  const QString v = QString::fromLatin1(qgetenv("PAKFU_SMOKE_TABS")).trimmed().toLower();
  if (v.isEmpty() || v == "0" || v == "false" || v == "no" || v == "off") {
    return;
  }

  QPointer<MainWindow> window_ptr(&window);

  auto find_action = [window_ptr](const QString& text) -> QAction* {
    if (!window_ptr) {
      return nullptr;
    }
    const QList<QAction*> actions = window_ptr->findChildren<QAction*>();
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

  auto click_tab_close = [](QTabWidget* tabs) {
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

  QTimer::singleShot(250, &window, [window_ptr, find_action]() {
    if (!window_ptr) {
      return;
    }
    if (QAction* act = find_action("New PAK")) {
      act->trigger();
    }
  });

  QTimer::singleShot(600, &window, [window_ptr, click_tab_close]() {
    if (!window_ptr) {
      return;
    }
    auto* tabs = qobject_cast<QTabWidget*>(window_ptr->centralWidget());
    click_tab_close(tabs);
  });

  QTimer::singleShot(900, &window, [window_ptr, click_tab_close]() {
    if (!window_ptr) {
      return;
    }
    auto* tabs = qobject_cast<QTabWidget*>(window_ptr->centralWidget());
    if (!tabs) {
      return;
    }
    if (tabs->count() > 0) {
      tabs->setCurrentIndex(0);
    }
    click_tab_close(tabs);
  });

  // If we got this far without exploding, close cleanly.
  QTimer::singleShot(1400, &window, [window_ptr]() {
    if (window_ptr) {
      window_ptr->close();
    }
  });
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

#ifdef Q_OS_WIN
  configure_qt_plugin_paths_for_local_deploy(resolve_executable_dir_winapi());
#endif

  QApplication app(argc, argv);
  const QIcon app_icon = load_app_icon();
  if (!app_icon.isNull()) {
    app.setWindowIcon(app_icon);
  }
  set_app_metadata(app);
  prefer_qt_ffmpeg_backend_if_available();
  const QString server_name = single_instance_server_name();
  const QStringList initial_archives = find_initial_archives(argc, argv);

  // Allow multiple instances for testing/debugging.
  const bool allow_multi_instance = qEnvironmentVariableIsSet("PAKFU_ALLOW_MULTI_INSTANCE");

  if (!allow_multi_instance) {
    if (send_ipc_payload(server_name, build_ipc_payload(initial_archives, true))) {
      return 0;
    }
  }

  // Primary instance: listen for open requests from subsequent launches (e.g. file associations).
  QPointer<MainWindow> main_window;
  bool main_shown = false;
  QLocalServer* ipc_server = nullptr;
  QStringList pending_paths;
  bool pending_focus = false;
  if (!allow_multi_instance) {
    ipc_server = new QLocalServer(&app);
    ipc_server->setSocketOptions(QLocalServer::UserAccessOption);
    // Clean up stale servers (e.g. after a crash) on platforms that use a socket file.
    QLocalServer::removeServer(server_name);
    if (!ipc_server->listen(server_name)) {
      QLocalServer::removeServer(server_name);
      (void)ipc_server->listen(server_name);
    }

    QObject::connect(ipc_server, &QLocalServer::newConnection, &app, [&]() {
      while (ipc_server && ipc_server->hasPendingConnections()) {
        QLocalSocket* sock = ipc_server->nextPendingConnection();
        if (!sock) {
          break;
        }
        auto buf = std::make_shared<QByteArray>();
        QObject::connect(sock, &QLocalSocket::readyRead, sock, [sock, buf]() { buf->append(sock->readAll()); });
        QObject::connect(sock,
                         &QLocalSocket::disconnected,
                         sock,
                         [sock, buf, &pending_paths, &pending_focus, &main_window, &main_shown]() {
          const QByteArray payload = *buf;
          QStringList paths;
          bool focus = true;

          QJsonParseError parse_error{};
          const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
          if (parse_error.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            focus = obj.value("focus").toBool(true);
            const QJsonArray arr = obj.value("paths").toArray();
            for (const QJsonValue& v : arr) {
              const QString p = QFileInfo(v.toString()).absoluteFilePath();
              if (!p.isEmpty() && is_archive_path(p)) {
                paths.push_back(p);
              }
            }
          } else if (!payload.isEmpty()) {
            // Legacy/fallback: treat payload as a single path.
            const QString p = QFileInfo(QString::fromUtf8(payload)).absoluteFilePath();
            if (!p.isEmpty() && is_archive_path(p)) {
              paths.push_back(p);
            }
          }

          if (!paths.isEmpty() && main_window && main_shown) {
            main_window->open_archives(paths);
          } else if (!paths.isEmpty()) {
            pending_paths.append(paths);
          }

          if (focus) {
            if (main_window && main_shown) {
              if (main_window->isMinimized()) {
                main_window->showNormal();
              }
              main_window->show();
              main_window->raise();
              main_window->activateWindow();
            } else {
              pending_focus = true;
            }
          }

          sock->deleteLater();
        });
      }
    });
  }

  ThemeManager::apply_saved_theme(app);

  std::optional<GameSet> selected;
  {
    GameSetState state = load_game_set_state();
    if (state.sets.isEmpty()) {
      GameSetDialog game_set_dialog;
      if (game_set_dialog.exec() != QDialog::Accepted) {
        return 0;
      }
      selected = game_set_dialog.selected_game_set();
    } else {
      const GameSet* by_uid = find_game_set(state, state.selected_uid);
      if (by_uid) {
        selected = *by_uid;
      } else if (!state.sets.isEmpty()) {
        selected = state.sets.first();
      }
    }
  }
  if (!selected.has_value()) {
    return 0;
  }

  MainWindow window(*selected, QString(), false);
  main_window = &window;
  if (!initial_archives.isEmpty()) {
    window.open_archives(initial_archives);
  }

  QPointer<SplashScreen> splash = show_splash(app);
  if (splash) {
    // Prevent the app from exiting when the splash is the only visible window.
    app.setQuitOnLastWindowClosed(false);
  }
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
    if (!pending_paths.isEmpty()) {
      window.open_archives(pending_paths);
      pending_paths.clear();
    }
    if (pending_focus) {
      if (window.isMinimized()) {
        window.showNormal();
      }
      window.raise();
      window.activateWindow();
      pending_focus = false;
    }
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
