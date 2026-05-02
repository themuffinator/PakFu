#include "update_service.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTimer>
#include <QVersionNumber>

#include "ui/ui_icons.h"

namespace {
constexpr char kUserAgent[] = "PakFu-Updater";
constexpr char kLastCheckKey[] = "updates/lastCheckUtc";
constexpr char kSkipVersionKey[] = "updates/skipVersion";
constexpr char kAutoCheckKey[] = "updates/autoCheck";

QUrl releases_api_url(const QString& github_repo) {
  return QUrl(QString("https://api.github.com/repos/%1/releases").arg(github_repo));
}

int score_asset_name(const QString& name) {
  const QString lower = name.toLower();
  int score = 0;

#if defined(Q_OS_WIN)
  if (lower.contains("win") || lower.contains("windows")) {
    score += 50;
  }
  if (lower.contains("x64") || lower.contains("amd64") || lower.contains("x86_64")) {
    score += 10;
  }
  if (lower.endsWith(".exe")) {
    score += 100;
  } else if (lower.endsWith(".msi")) {
    score += 90;
  } else if (lower.endsWith(".zip")) {
    score += 40;
  }
#elif defined(Q_OS_MACOS)
  if (lower.contains("mac") || lower.contains("osx") || lower.contains("macos")) {
    score += 50;
  }
  if (lower.contains("arm64") || lower.contains("aarch64")) {
#if defined(Q_PROCESSOR_ARM_64)
    score += 10;
#endif
  }
  if (lower.contains("x64") || lower.contains("x86_64")) {
#if defined(Q_PROCESSOR_X86_64)
    score += 10;
#endif
  }
  if (lower.endsWith(".dmg")) {
    score += 100;
  } else if (lower.endsWith(".pkg")) {
    score += 90;
  } else if (lower.endsWith(".zip")) {
    score += 40;
  }
#else
  if (lower.contains("linux")) {
    score += 50;
  }
  if (lower.contains("x64") || lower.contains("amd64") || lower.contains("x86_64")) {
#if defined(Q_PROCESSOR_X86_64)
    score += 10;
#endif
  }
  if (lower.contains("arm64") || lower.contains("aarch64")) {
#if defined(Q_PROCESSOR_ARM_64)
    score += 10;
#endif
  }
  if (lower.endsWith(".appimage")) {
    score += 100;
  } else if (lower.endsWith(".tar.xz")) {
    score += 70;
  } else if (lower.endsWith(".tar.gz") || lower.endsWith(".tgz")) {
    score += 60;
  } else if (lower.endsWith(".zip")) {
    score += 30;
  }
#endif

  return score;
}

bool is_installable_name(const QString& name) {
  const QString lower = name.toLower();
#if defined(Q_OS_WIN)
  return lower.endsWith(".exe") || lower.endsWith(".msi");
#elif defined(Q_OS_MACOS)
  return lower.endsWith(".dmg") || lower.endsWith(".pkg");
#else
  return lower.endsWith(".appimage");
#endif
}

bool is_release_manifest_name(const QString& name) {
  const QString lower = name.toLower();
  return lower.endsWith(".json") && lower.contains("release-manifest");
}

bool looks_like_sha256(const QString& text) {
  if (text.size() != 64) {
    return false;
  }
  for (const QChar ch : text) {
    if (!ch.isDigit() && (ch.toLower() < 'a' || ch.toLower() > 'f')) {
      return false;
    }
  }
  return true;
}

void show_transient_status(QWidget* parent, const QString& message, int timeout_ms = 7000) {
  if (parent) {
    if (auto* status_bar = parent->findChild<QStatusBar*>()) {
      status_bar->showMessage(message, timeout_ms);
      status_bar->setAccessibleDescription(message);
      return;
    }
  }
  qInfo().noquote() << QString("PakFu Updates: %1").arg(message);
}

}  // namespace

namespace {
[[nodiscard]] bool start_installer_after_exit(const QString& installer_path, QString* error) {
  if (installer_path.isEmpty()) {
    if (error) {
      *error = qApp->translate("UpdateService", "Installer path is empty.");
    }
    return false;
  }

  const QFileInfo installer_info(installer_path);
  if (!installer_info.exists() || !installer_info.isFile()) {
    if (error) {
      *error = qApp->translate("UpdateService", "Downloaded installer file is missing.");
    }
    return false;
  }

  const QString installer_absolute_path = installer_info.absoluteFilePath();
  const QString app_absolute_path = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();

  const QString temp_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (temp_dir.isEmpty()) {
    if (error) {
      *error = qApp->translate("UpdateService", "No temporary directory is available.");
    }
    return false;
  }

  const qint64 pid = QCoreApplication::applicationPid();
  if (pid <= 0) {
    if (error) {
      *error = qApp->translate("UpdateService", "Unable to determine application PID.");
    }
    return false;
  }

#if defined(Q_OS_WIN)
  QTemporaryFile script(QDir(temp_dir).filePath("pakfu-update-XXXXXX.cmd"));
  script.setAutoRemove(false);
  if (!script.open()) {
    if (error) {
      *error = qApp->translate("UpdateService", "Unable to create update launcher script.");
    }
    return false;
  }

  const QByteArray payload =
    "@echo off\r\n"
    "setlocal EnableExtensions\r\n"
    "set \"PID=%~1\"\r\n"
    "set \"INSTALLER=%~f2\"\r\n"
    "set \"APP=%~f3\"\r\n"
    "set \"APP_TO_START=%APP%\"\r\n"
    "set \"EXT=%~x2\"\r\n"
    ":waitloop\r\n"
    "tasklist /FI \"PID eq %PID%\" 2>NUL | findstr /I \"%PID%\" >NUL\r\n"
    "if not errorlevel 1 (\r\n"
    "  timeout /T 1 /NOBREAK >NUL\r\n"
    "  goto waitloop\r\n"
    ")\r\n"
    "if /I \"%EXT%\"==\".msi\" (\r\n"
    "  start \"\" /wait msiexec.exe /i \"%INSTALLER%\"\r\n"
    "  if exist \"%LOCALAPPDATA%\\PakFu\\pakfu.exe\" set \"APP_TO_START=%LOCALAPPDATA%\\PakFu\\pakfu.exe\"\r\n"
    ") else (\r\n"
    "  start \"\" /wait \"%INSTALLER%\"\r\n"
    ")\r\n"
    "if exist \"%APP_TO_START%\" (\r\n"
    "  start \"\" \"%APP_TO_START%\"\r\n"
    ")\r\n"
    "start \"\" /B cmd.exe /C del /F /Q \"%~f0\" >NUL 2>&1\r\n"
    "endlocal\r\n";
  script.write(payload);
  script.close();

  const QString script_path = QDir::toNativeSeparators(script.fileName());
  const QString installer_arg = QDir::toNativeSeparators(installer_absolute_path);
  const QString app_arg = QDir::toNativeSeparators(app_absolute_path);
  const QString cmd = QString("\"\"%1\" %2 \"%3\" \"%4\"\"")
                        .arg(script_path, QString::number(pid), installer_arg, app_arg);

  if (!QProcess::startDetached("cmd.exe", {"/C", cmd})) {
    if (error) {
      *error = qApp->translate("UpdateService", "Unable to start deferred update launcher.");
    }
    return false;
  }

  return true;
#else
  QTemporaryFile script(QDir(temp_dir).filePath("pakfu-update-XXXXXX.sh"));
  script.setAutoRemove(false);
  if (!script.open()) {
    if (error) {
      *error = qApp->translate("UpdateService", "Unable to create update launcher script.");
    }
    return false;
  }

  const QByteArray payload =
    "#!/bin/sh\n"
    "PID=\"$1\"\n"
    "INSTALLER=\"$2\"\n"
    "while kill -0 \"$PID\" 2>/dev/null; do\n"
    "  sleep 1\n"
    "done\n"
    "if [ \"$(uname)\" = \"Darwin\" ]; then\n"
    "  open \"$INSTALLER\"\n"
    "else\n"
    "  chmod +x \"$INSTALLER\" 2>/dev/null\n"
    "  \"$INSTALLER\" >/dev/null 2>&1 &\n"
    "fi\n"
    "rm -- \"$0\" >/dev/null 2>&1 &\n";
  script.write(payload);
  script.close();

  QFile::setPermissions(script.fileName(),
                        QFile::permissions(script.fileName()) | QFileDevice::ExeUser | QFileDevice::ReadUser |
                          QFileDevice::WriteUser);

  if (!QProcess::startDetached("sh", {script.fileName(), QString::number(pid), installer_absolute_path})) {
    if (error) {
      *error = qApp->translate("UpdateService", "Unable to start deferred update launcher.");
    }
    return false;
  }

  return true;
#endif
}
}  // namespace

UpdateService::UpdateService(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {
  qRegisterMetaType<UpdateCheckResult>();
  check_timeout_ = new QTimer(this);
  check_timeout_->setSingleShot(true);
  connect(check_timeout_, &QTimer::timeout, this, [this]() {
    if (!check_reply_) {
      return;
    }
    check_error_override_ = tr("Update check timed out.");
    check_reply_->abort();
  });
}

void UpdateService::set_dialogs_enabled(bool enabled) {
  dialogs_enabled_ = enabled;
}

void UpdateService::configure(const QString& github_repo,
                              const QString& channel,
                              const QString& current_version) {
  github_repo_ = github_repo.trimmed();
  channel_ = channel.trimmed();
  current_version_ = current_version.trimmed();
}

void UpdateService::check_for_updates(bool user_initiated, QWidget* parent) {
  user_initiated_ = user_initiated;
  parent_window_ = parent;
  check_error_override_.clear();

  if (github_repo_.isEmpty() || !github_repo_.contains('/')) {
    if (user_initiated_) {
      show_error_message(parent_window_, tr("Update check is not configured with a GitHub repository."));
    }
    UpdateCheckResult result;
    result.state = UpdateCheckState::NotConfigured;
    result.message = tr("Update check is not configured with a GitHub repository.");
    emit check_completed(result);
    return;
  }

  QSettings settings;
  settings.setValue(kLastCheckKey, QDateTime::currentDateTimeUtc());

  const QUrl api_url = releases_api_url(github_repo_);

  QNetworkRequest request(api_url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", kUserAgent);
  request.setTransferTimeout(15000);

  abort_checks();
  check_reply_ = network_->get(request);
  connect(check_reply_, &QNetworkReply::finished, this, &UpdateService::on_check_finished);
  if (check_timeout_) {
    check_timeout_->start(20000);
  }
}

void UpdateService::abort_checks() {
  if (check_timeout_) {
    check_timeout_->stop();
  }
  if (check_reply_) {
    check_reply_->disconnect(this);
    check_reply_->abort();
    check_reply_->deleteLater();
    check_reply_ = nullptr;
  }
  if (manifest_reply_) {
    manifest_reply_->disconnect(this);
    manifest_reply_->abort();
    manifest_reply_->deleteLater();
    manifest_reply_ = nullptr;
  }
  if (download_reply_) {
    download_reply_->disconnect(this);
    download_reply_->abort();
    download_reply_->deleteLater();
    download_reply_ = nullptr;
  }
}

UpdateCheckResult UpdateService::check_for_updates_sync() {
  UpdateCheckResult result;
  if (github_repo_.isEmpty() || !github_repo_.contains('/')) {
    result.state = UpdateCheckState::NotConfigured;
    result.message = tr("Update check is not configured with a GitHub repository.");
    return result;
  }

  const QUrl api_url = releases_api_url(github_repo_);

  QNetworkRequest request(api_url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", kUserAgent);
  request.setTransferTimeout(15000);

  QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply(network_->get(request));
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  timeout.start(20000);
  connect(&timeout, &QTimer::timeout, &loop, [&reply]() {
    if (reply) {
      reply->abort();
    }
  });
  connect(reply.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (!reply) {
    result.state = UpdateCheckState::Error;
    result.message = tr("Update check failed.");
    return result;
  }

  if (reply->error() != QNetworkReply::NoError) {
    result.state = UpdateCheckState::Error;
    result.message = tr("Unable to reach GitHub for update checks.");
    return result;
  }

  const QByteArray payload = reply->readAll();
  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    result.state = UpdateCheckState::Error;
    result.message = tr("GitHub update response could not be parsed.");
    return result;
  }

  UpdateInfo info;
  if (doc.isObject()) {
    info = parse_release_object(doc.object());
  } else if (doc.isArray()) {
    info = select_release_from_array(doc.array());
  }

  if (info.version.isEmpty()) {
    result.state = UpdateCheckState::NoRelease;
    result.message = tr("No valid full release was found.");
    return result;
  }

  const QString normalized_latest = normalize_version(info.version);
  const QString normalized_current = normalize_version(current_version_);
  if (!is_newer_version(normalized_latest, normalized_current)) {
    result.state = UpdateCheckState::UpToDate;
    result.info = info;
    return result;
  }

  result.state = UpdateCheckState::UpdateAvailable;
  result.info = info;
  return result;
}

void UpdateService::on_check_finished() {
  auto* reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply) {
    UpdateCheckResult result;
    result.state = UpdateCheckState::Error;
    result.message = tr("Update check failed.");
    emit check_completed(result);
    return;
  }
  if (reply != check_reply_) {
    reply->deleteLater();
    return;
  }
  check_reply_ = nullptr;
  if (check_timeout_) {
    check_timeout_->stop();
  }

  QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply_guard(reply);

  auto handle_error = [&](const QString& message, UpdateCheckState state = UpdateCheckState::Error) {
    UpdateCheckResult result;
    result.state = state;
    result.message = message;
    if (user_initiated_) {
      show_error_message(parent_window_, message);
      emit check_completed(result);
      return;
    }
    if (!dialogs_enabled_) {
      emit check_completed(result);
      return;
    }
    prompt_update_error(message);
  };

  if (reply->error() != QNetworkReply::NoError) {
    const QString message =
      check_error_override_.isEmpty() ? tr("Unable to reach GitHub for update checks.") : check_error_override_;
    check_error_override_.clear();
    handle_error(message);
    return;
  }

  check_error_override_.clear();

  const QByteArray payload = reply->readAll();
  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    handle_error(tr("GitHub update response could not be parsed."));
    return;
  }

  UpdateInfo info;
  if (doc.isObject()) {
    info = parse_release_object(doc.object());
  } else if (doc.isArray()) {
    info = select_release_from_array(doc.array());
  } else {
    handle_error(tr("GitHub update response was empty."));
    return;
  }

  if (info.version.isEmpty()) {
    UpdateCheckResult result;
    result.state = UpdateCheckState::NoRelease;
    result.message = tr("No valid full release was found.");
    if (user_initiated_) {
      show_error_message(parent_window_, result.message);
    }
    emit check_completed(result);
    return;
  }

  const QString normalized_latest = normalize_version(info.version);
  const QString normalized_current = normalize_version(current_version_);

  QSettings settings;
  const QString skipped = settings.value(kSkipVersionKey).toString();
  if (!user_initiated_ && !skipped.isEmpty() && normalized_latest == skipped) {
    UpdateCheckResult result;
    result.state = UpdateCheckState::UpToDate;
    result.info = info;
    emit check_completed(result);
    return;
  }

  if (!is_newer_version(normalized_latest, normalized_current)) {
    if (user_initiated_) {
      show_no_update_message(parent_window_);
    }
    UpdateCheckResult result;
    result.state = UpdateCheckState::UpToDate;
    result.info = info;
    emit check_completed(result);
    return;
  }

  UpdateCheckResult result;
  result.state = UpdateCheckState::UpdateAvailable;
  result.info = info;

  if (!dialogs_enabled_) {
    emit check_completed(result);
    return;
  }

  prompt_update(info, parent_window_, user_initiated_);
  emit check_completed(result);
}

void UpdateService::show_update_prompt(const UpdateInfo& info, QWidget* parent, bool user_initiated) {
  parent_window_ = parent;
  user_initiated_ = user_initiated;
  prompt_update(info, parent, user_initiated);
}

UpdateInfo UpdateService::parse_release_object(const QJsonObject& release_obj) const {
  if (release_obj.value("draft").toBool() || release_obj.value("prerelease").toBool()) {
    return UpdateInfo{};
  }

  UpdateInfo info;
  info.version = release_obj.value("tag_name").toString().trimmed();
  info.title = release_obj.value("name").toString().trimmed();
  info.notes = release_obj.value("body").toString();
  info.html_url = QUrl(release_obj.value("html_url").toString());
  info.prerelease = release_obj.value("prerelease").toBool();
  const QJsonArray assets = release_obj.value("assets").toArray();
  info.asset_url = select_asset(assets, &info.asset_name, &info.asset_size);
  info.manifest_url = select_manifest_asset(assets, &info.manifest_name, &info.manifest_size);
  return info;
}

UpdateInfo UpdateService::select_release_from_array(const QJsonArray& releases) const {
  for (const QJsonValue& value : releases) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject release_obj = value.toObject();
    if (release_obj.value("draft").toBool()) {
      continue;
    }
    const bool prerelease = release_obj.value("prerelease").toBool();
    if (prerelease) {
      continue;
    }
    return parse_release_object(release_obj);
  }

  return UpdateInfo{};
}

QUrl UpdateService::select_asset(const QJsonArray& assets,
                                 QString* asset_name,
                                 qint64* asset_size) const {
  int best_score = -1;
  QUrl best_url;
  QString best_name;
  qint64 best_size = 0;

  for (const QJsonValue& value : assets) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject asset = value.toObject();
    const QString name = asset.value("name").toString();
    const QUrl url = QUrl(asset.value("browser_download_url").toString());
    if (name.isEmpty() || !url.isValid() || is_release_manifest_name(name)) {
      continue;
    }
    const int score = score_asset_name(name);
    if (score <= 0) {
      continue;
    }
    if (score > best_score) {
      best_score = score;
      best_url = url;
      best_name = name;
      best_size = asset.value("size").toVariant().toLongLong();
    }
  }

  if (asset_name) {
    *asset_name = best_name;
  }
  if (asset_size) {
    *asset_size = best_size;
  }
  return best_url;
}

QUrl UpdateService::select_manifest_asset(const QJsonArray& assets,
                                          QString* manifest_name,
                                          qint64* manifest_size) const {
  QUrl best_url;
  QString best_name;
  qint64 best_size = 0;

  for (const QJsonValue& value : assets) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject asset = value.toObject();
    const QString name = asset.value("name").toString();
    const QUrl url = QUrl(asset.value("browser_download_url").toString());
    if (!is_release_manifest_name(name) || !url.isValid()) {
      continue;
    }
    best_url = url;
    best_name = name;
    best_size = asset.value("size").toVariant().toLongLong();
    break;
  }

  if (manifest_name) {
    *manifest_name = best_name;
  }
  if (manifest_size) {
    *manifest_size = best_size;
  }
  return best_url;
}

QString UpdateService::normalize_version(const QString& version) const {
  QString normalized = version.trimmed();
  if (normalized.startsWith('v') || normalized.startsWith('V')) {
    normalized = normalized.mid(1);
  }
  return normalized;
}

bool UpdateService::is_newer_version(const QString& latest, const QString& current) const {
  const QVersionNumber latest_version = QVersionNumber::fromString(latest);
  const QVersionNumber current_version = QVersionNumber::fromString(current);

  if (!latest_version.isNull() && !current_version.isNull()) {
    return QVersionNumber::compare(latest_version, current_version) > 0;
  }

  return latest.compare(current, Qt::CaseInsensitive) != 0;
}

bool UpdateService::is_installable_asset(const QString& name) const {
  return is_installable_name(name);
}

void UpdateService::show_no_update_message(QWidget* parent) const {
  show_transient_status(parent, tr("You are already on the latest version."));
}

void UpdateService::show_error_message(QWidget* parent, const QString& message) const {
  QMessageBox::warning(parent, tr("PakFu Updates"), message);
}

void UpdateService::prompt_update_error(const QString& message) {
  const bool splash_parent = parent_window_ && parent_window_->windowFlags().testFlag(Qt::SplashScreen);
  QWidget* dialog_parent = splash_parent ? nullptr : parent_window_.data();
  QMessageBox box(dialog_parent);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(tr("Update Check Failed"));
  box.setText(message);
  if (splash_parent) {
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
  }
  QPushButton* retry = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
  QPushButton* ignore = box.addButton(tr("Ignore"), QMessageBox::RejectRole);
  retry->setIcon(UiIcons::icon(UiIcons::Id::CheckUpdates, retry->style()));
  ignore->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, ignore->style()));
  box.setDefaultButton(retry);
  box.raise();
  box.activateWindow();
  box.exec();

  if (box.clickedButton() == retry) {
    check_for_updates(user_initiated_, parent_window_);
    return;
  }

  Q_UNUSED(ignore);
  UpdateCheckResult result;
  result.state = UpdateCheckState::Error;
  result.message = message;
  emit check_completed(result);
}

void UpdateService::prompt_update(const UpdateInfo& info, QWidget* parent, bool user_initiated) {
  const bool splash_parent = parent && parent->windowFlags().testFlag(Qt::SplashScreen);
  QWidget* dialog_parent = splash_parent ? nullptr : parent;
  QString summary = tr("PakFu %1 is available.").arg(normalize_version(info.version));
  if (!current_version_.isEmpty()) {
    summary = tr("PakFu %1 is available (you have %2).")
                .arg(normalize_version(info.version), normalize_version(current_version_));
  }

  QMessageBox box(dialog_parent);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Update Available"));
  box.setText(summary);
  if (splash_parent) {
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
  }
  if (!info.notes.trimmed().isEmpty()) {
    const QString trimmed = info.notes.trimmed();
    box.setInformativeText(trimmed.left(600));
    box.setDetailedText(trimmed);
  }

  auto* dont_ask = new QCheckBox(tr("Don't ask again"));
  box.setCheckBox(dont_ask);

  QPushButton* download_button = nullptr;
  if (info.asset_url.isValid()) {
    const QString label = is_installable_asset(info.asset_name) ? tr("Download && Install") : tr("Download");
    download_button = box.addButton(label, QMessageBox::AcceptRole);
    if (download_button) {
      download_button->setIcon(UiIcons::icon(UiIcons::Id::Save, download_button->style()));
    }
  }
  QPushButton* open_button = box.addButton(tr("Open Release Page"), QMessageBox::ActionRole);
  open_button->setIcon(UiIcons::icon(UiIcons::Id::OpenArchive, open_button->style()));
  QPushButton* skip_button = box.addButton(tr("Skip This Version"), QMessageBox::RejectRole);
  skip_button->setIcon(UiIcons::icon(UiIcons::Id::DeleteItem, skip_button->style()));
  QPushButton* later_button = box.addButton(user_initiated ? tr("Close") : tr("Later"), QMessageBox::DestructiveRole);
  later_button->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, later_button->style()));
  box.setDefaultButton(download_button ? download_button : open_button);
  box.raise();
  box.activateWindow();
  box.exec();

  if (dont_ask && dont_ask->isChecked()) {
    QSettings settings;
    settings.setValue(kAutoCheckKey, false);
  }

  if (box.clickedButton() == download_button) {
    begin_download(info, dialog_parent);
    return;
  }
  if (box.clickedButton() == open_button && info.html_url.isValid()) {
    QDesktopServices::openUrl(info.html_url);
    return;
  }
  if (box.clickedButton() == skip_button) {
    QSettings settings;
    settings.setValue(kSkipVersionKey, normalize_version(info.version));
    return;
  }

}

void UpdateService::begin_download(const UpdateInfo& info, QWidget* parent) {
  if (!info.asset_url.isValid()) {
    if (info.html_url.isValid()) {
      QDesktopServices::openUrl(info.html_url);
    }
    return;
  }

  if (!info.manifest_url.isValid()) {
    show_error_message(parent,
                       tr("This release does not include a verification manifest. PakFu will not download or launch "
                          "updates that cannot be checked against the release manifest."));
    return;
  }

  parent_window_ = parent;
  pending_download_info_ = info;
  download_expected_sha256_.clear();
  download_expected_size_ = -1;

  if (manifest_reply_) {
    manifest_reply_->disconnect(this);
    manifest_reply_->abort();
    manifest_reply_->deleteLater();
    manifest_reply_ = nullptr;
  }

  QNetworkRequest request(info.manifest_url);
  request.setRawHeader("Accept", "application/json,application/octet-stream");
  request.setRawHeader("User-Agent", kUserAgent);
  request.setTransferTimeout(15000);
  manifest_reply_ = network_->get(request);
  connect(manifest_reply_, &QNetworkReply::finished, this, &UpdateService::on_manifest_finished);
}

void UpdateService::begin_asset_download(const UpdateInfo& info, QWidget* parent) {
  const QString temp_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (temp_dir.isEmpty()) {
    show_error_message(parent, tr("No temporary directory is available to store the update."));
    return;
  }

  const QString filename = info.asset_name.isEmpty()
                              ? QString("pakfu-update-%1").arg(normalize_version(info.version))
                              : info.asset_name;
  download_path_ = QDir(temp_dir).filePath(filename);
  download_installable_ = is_installable_asset(filename);

  download_file_.reset(new QSaveFile(download_path_));
  if (!download_file_->open(QIODevice::WriteOnly)) {
    show_error_message(parent, tr("Unable to create the update file."));
    download_file_.reset();
    return;
  }

  if (download_reply_) {
    download_reply_->disconnect(this);
    download_reply_->abort();
    download_reply_->deleteLater();
    download_reply_ = nullptr;
  }

  QNetworkRequest request(info.asset_url);
  request.setRawHeader("Accept", "application/octet-stream");
  request.setRawHeader("User-Agent", kUserAgent);
  download_reply_ = network_->get(request);

  progress_dialog_ = new QProgressDialog(tr("Downloading update..."), tr("Cancel"), 0, 100, parent);
  progress_dialog_->setWindowModality(Qt::WindowModal);
  progress_dialog_->setAutoClose(true);
  progress_dialog_->setAutoReset(true);
  if (parent && parent->windowFlags().testFlag(Qt::SplashScreen)) {
    progress_dialog_->setWindowFlag(Qt::WindowStaysOnTopHint, true);
  }
  progress_dialog_->raise();
  progress_dialog_->activateWindow();

  connect(download_reply_, &QNetworkReply::readyRead, this, &UpdateService::on_download_ready_read);
  connect(download_reply_, &QNetworkReply::downloadProgress, this, &UpdateService::on_download_progress);
  connect(download_reply_, &QNetworkReply::finished, this, &UpdateService::on_download_finished);
  connect(progress_dialog_, &QProgressDialog::canceled, download_reply_, &QNetworkReply::abort);
}

void UpdateService::on_manifest_finished() {
  auto* reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply) {
    show_error_message(parent_window_, tr("Release manifest verification failed."));
    return;
  }
  if (reply != manifest_reply_) {
    reply->deleteLater();
    return;
  }
  manifest_reply_ = nullptr;
  QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply_guard(reply);

  if (reply->error() != QNetworkReply::NoError) {
    show_error_message(parent_window_, tr("Release manifest could not be downloaded, so the update was not started."));
    return;
  }

  QString expected_sha256;
  qint64 expected_size = -1;
  QString parse_err;
  if (!parse_manifest_for_download(reply->readAll(),
                                   pending_download_info_,
                                   &expected_sha256,
                                   &expected_size,
                                   &parse_err)) {
    show_error_message(parent_window_,
                       parse_err.isEmpty() ? tr("Release manifest did not contain a valid checksum for this update.")
                                           : parse_err);
    return;
  }

  download_expected_sha256_ = expected_sha256;
  download_expected_size_ = expected_size;
  begin_asset_download(pending_download_info_, parent_window_);
}

void UpdateService::on_download_ready_read() {
  auto* reply = qobject_cast<QNetworkReply*>(sender());
  if (reply && reply == download_reply_ && download_file_) {
    download_file_->write(reply->readAll());
  }
}

void UpdateService::on_download_progress(qint64 received, qint64 total) {
  auto* reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply || reply != download_reply_) {
    return;
  }
  if (!progress_dialog_) {
    return;
  }
  if (total > 0) {
    progress_dialog_->setMaximum(static_cast<int>(total));
    progress_dialog_->setValue(static_cast<int>(received));
  } else {
    progress_dialog_->setMaximum(0);
  }
}

bool UpdateService::parse_manifest_for_download(const QByteArray& payload,
                                                const UpdateInfo& info,
                                                QString* expected_sha256,
                                                qint64* expected_size,
                                                QString* error) const {
  if (expected_sha256) {
    expected_sha256->clear();
  }
  if (expected_size) {
    *expected_size = -1;
  }

  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) {
      *error = tr("Release manifest could not be parsed.");
    }
    return false;
  }

  const QJsonObject root = doc.object();
  const QString product = root.value("product").toString().trimmed();
  if (!product.isEmpty() && product.compare("pakfu", Qt::CaseInsensitive) != 0) {
    if (error) {
      *error = tr("Release manifest is not for PakFu.");
    }
    return false;
  }

  const QString manifest_version = normalize_version(root.value("version").toString());
  const QString release_version = normalize_version(info.version);
  if (!manifest_version.isEmpty() && !release_version.isEmpty() &&
      manifest_version.compare(release_version, Qt::CaseInsensitive) != 0) {
    if (error) {
      *error = tr("Release manifest version does not match the selected update.");
    }
    return false;
  }

  const QJsonArray assets = root.value("assets").toArray();
  for (const QJsonValue& value : assets) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject asset = value.toObject();
    const QString name = asset.value("name").toString();
    if (name.compare(info.asset_name, Qt::CaseSensitive) != 0) {
      continue;
    }

    const QString sha256 = asset.value("sha256").toString().trimmed().toLower();
    if (!looks_like_sha256(sha256)) {
      if (error) {
        *error = tr("Release manifest entry for %1 does not include a valid SHA-256 checksum.").arg(name);
      }
      return false;
    }

    const qint64 size = asset.value("size").toVariant().toLongLong();
    if (size <= 0) {
      if (error) {
        *error = tr("Release manifest entry for %1 does not include a valid size.").arg(name);
      }
      return false;
    }

    if (expected_sha256) {
      *expected_sha256 = sha256;
    }
    if (expected_size) {
      *expected_size = size;
    }
    if (error) {
      error->clear();
    }
    return true;
  }

  if (error) {
    *error = tr("Release manifest does not contain the selected asset: %1").arg(info.asset_name);
  }
  return false;
}

bool UpdateService::verify_downloaded_file(QString* error) const {
  const QFileInfo info(download_path_);
  if (!info.exists() || !info.isFile()) {
    if (error) {
      *error = tr("Downloaded update file is missing.");
    }
    return false;
  }

  if (download_expected_size_ > 0 && info.size() != download_expected_size_) {
    if (error) {
      *error = tr("Downloaded update size mismatch. Expected %1 bytes, got %2 bytes.")
                 .arg(download_expected_size_)
                 .arg(info.size());
    }
    return false;
  }

  if (!looks_like_sha256(download_expected_sha256_)) {
    if (error) {
      *error = tr("No valid expected SHA-256 checksum is available for the downloaded update.");
    }
    return false;
  }

  QFile file(download_path_);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = tr("Unable to open downloaded update for verification.");
    }
    return false;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  QByteArray buffer;
  buffer.resize(1024 * 1024);
  for (;;) {
    const qint64 got = file.read(buffer.data(), buffer.size());
    if (got < 0) {
      if (error) {
        *error = tr("Unable to read downloaded update for verification.");
      }
      return false;
    }
    if (got == 0) {
      break;
    }
    hash.addData(QByteArrayView(buffer.constData(), static_cast<qsizetype>(got)));
  }

  const QString actual = QString::fromLatin1(hash.result().toHex());
  if (actual.compare(download_expected_sha256_, Qt::CaseInsensitive) != 0) {
    if (error) {
      *error = tr("Downloaded update SHA-256 checksum does not match the release manifest.");
    }
    return false;
  }

  if (error) {
    error->clear();
  }
  return true;
}

void UpdateService::on_download_finished() {
  auto* reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply) {
    return;
  }
  if (reply != download_reply_) {
    reply->deleteLater();
    return;
  }
  download_reply_ = nullptr;
  QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply_guard(reply);

  if (progress_dialog_) {
    progress_dialog_->close();
    progress_dialog_->deleteLater();
    progress_dialog_ = nullptr;
  }

  if (reply->error() != QNetworkReply::NoError) {
    show_error_message(parent_window_, tr("Update download failed."));
    if (download_file_) {
      download_file_->cancelWriting();
      download_file_.reset();
    }
    QFile::remove(download_path_);
    return;
  }

  if (download_file_) {
    download_file_->write(reply->readAll());
    if (!download_file_->commit()) {
      show_error_message(parent_window_, tr("Unable to finalize the downloaded update."));
      download_file_.reset();
      return;
    }
    download_file_.reset();
  }

  QString verify_err;
  if (!verify_downloaded_file(&verify_err)) {
    QFile::remove(download_path_);
    show_error_message(parent_window_,
                       verify_err.isEmpty()
                         ? tr("Downloaded update could not be verified against the release manifest.")
                         : verify_err);
    return;
  }

  if (!download_installable_) {
    const QString folder = QFileInfo(download_path_).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    show_transient_status(parent_window_, tr("Update downloaded; opened the containing folder."), 10000);
    return;
  }

  if (!launch_installer(download_path_, parent_window_)) {
    show_error_message(parent_window_, tr("Downloaded update could not be launched."));
  }
}

bool UpdateService::launch_installer(const QString& file_path, QWidget* parent) const {
  if (file_path.isEmpty()) {
    return false;
  }

  const QFileInfo info(file_path);
  if (!info.exists()) {
    return false;
  }

  QMessageBox box(parent);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Install Update"));
  box.setText(tr("The update has been downloaded.\n\nPakFu will close and start the installer."));
  if (parent && parent->windowFlags().testFlag(Qt::SplashScreen)) {
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
  }

  QPushButton* install = box.addButton(tr("Install and Restart"), QMessageBox::AcceptRole);
  QPushButton* later = box.addButton(tr("Later"), QMessageBox::RejectRole);
  install->setIcon(UiIcons::icon(UiIcons::Id::Save, install->style()));
  later->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, later->style()));
  box.setDefaultButton(install);
  box.raise();
  box.activateWindow();
  box.exec();

  if (box.clickedButton() != install) {
    Q_UNUSED(later);
    return true;  // Download succeeded; user chose to install later.
  }

  QString err;
  if (!start_installer_after_exit(file_path, &err)) {
    show_error_message(parent, err.isEmpty() ? tr("Unable to schedule the installer launch.") : err);
    return false;
  }

  QApplication::closeAllWindows();
  QCoreApplication::quit();
  return true;
}
