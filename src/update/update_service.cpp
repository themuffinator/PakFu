#include "update_service.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
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
#include <QVersionNumber>

namespace {
constexpr char kUserAgent[] = "PakFu-Updater";
constexpr char kLastCheckKey[] = "updates/lastCheckUtc";
constexpr char kSkipVersionKey[] = "updates/skipVersion";

QString asset_extension(const QString& name) {
  const QFileInfo info(name);
  return info.suffix().toLower();
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

}  // namespace

UpdateService::UpdateService(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {}

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

  if (github_repo_.isEmpty() || !github_repo_.contains('/')) {
    if (user_initiated_) {
      show_error_message(parent_window_, "Update check is not configured with a GitHub repository.");
    }
    return;
  }

  QSettings settings;
  settings.setValue(kLastCheckKey, QDateTime::currentDateTimeUtc());

  QUrl api_url;
  if (channel_.compare("stable", Qt::CaseInsensitive) == 0) {
    api_url = QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(github_repo_));
  } else {
    api_url = QUrl(QString("https://api.github.com/repos/%1/releases").arg(github_repo_));
  }

  QNetworkRequest request(api_url);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", kUserAgent);

  if (check_reply_) {
    check_reply_->deleteLater();
  }
  check_reply_ = network_->get(request);
  connect(check_reply_, &QNetworkReply::finished, this, &UpdateService::on_check_finished);
}

void UpdateService::on_check_finished() {
  QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply(check_reply_);
  check_reply_ = nullptr;

  if (!reply) {
    return;
  }

  if (reply->error() != QNetworkReply::NoError) {
    if (user_initiated_) {
      show_error_message(parent_window_, "Unable to reach GitHub for update checks.");
    }
    return;
  }

  const QByteArray payload = reply->readAll();
  QJsonParseError parse_error{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    if (user_initiated_) {
      show_error_message(parent_window_, "GitHub update response could not be parsed.");
    }
    return;
  }

  UpdateInfo info;
  if (doc.isObject()) {
    info = parse_release_object(doc.object());
  } else if (doc.isArray()) {
    info = select_release_from_array(doc.array());
  } else {
    if (user_initiated_) {
      show_error_message(parent_window_, "GitHub update response was empty.");
    }
    return;
  }

  if (info.version.isEmpty()) {
    if (user_initiated_) {
      show_error_message(parent_window_, "No valid release was found.");
    }
    return;
  }

  const QString normalized_latest = normalize_version(info.version);
  const QString normalized_current = normalize_version(current_version_);

  QSettings settings;
  const QString skipped = settings.value(kSkipVersionKey).toString();
  if (!user_initiated_ && !skipped.isEmpty() && normalized_latest == skipped) {
    return;
  }

  if (!is_newer_version(normalized_latest, normalized_current)) {
    if (user_initiated_) {
      show_no_update_message(parent_window_);
    }
    return;
  }

  prompt_update(info, parent_window_, user_initiated_);
}

UpdateInfo UpdateService::parse_release_object(const QJsonObject& release_obj) const {
  UpdateInfo info;
  info.version = release_obj.value("tag_name").toString().trimmed();
  info.title = release_obj.value("name").toString().trimmed();
  info.notes = release_obj.value("body").toString();
  info.html_url = QUrl(release_obj.value("html_url").toString());
  info.prerelease = release_obj.value("prerelease").toBool();
  const QJsonArray assets = release_obj.value("assets").toArray();
  info.asset_url = select_asset(assets, &info.asset_name, &info.asset_size);
  return info;
}

UpdateInfo UpdateService::select_release_from_array(const QJsonArray& releases) const {
  const bool wants_prerelease = channel_.compare("stable", Qt::CaseInsensitive) != 0;
  for (const QJsonValue& value : releases) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject release_obj = value.toObject();
    if (release_obj.value("draft").toBool()) {
      continue;
    }
    const bool prerelease = release_obj.value("prerelease").toBool();
    if (wants_prerelease && !prerelease) {
      continue;
    }
    if (!wants_prerelease && prerelease) {
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
    const int score = score_asset_name(name);
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

void UpdateService::show_no_update_message(QWidget* parent) {
  QMessageBox::information(parent, "PakFu Updates", "You are already on the latest version.");
}

void UpdateService::show_error_message(QWidget* parent, const QString& message) {
  QMessageBox::warning(parent, "PakFu Updates", message);
}

void UpdateService::prompt_update(const UpdateInfo& info, QWidget* parent, bool user_initiated) {
  QWidget* dialog_parent = parent;
  QString summary = QString("PakFu %1 is available.").arg(normalize_version(info.version));
  if (!current_version_.isEmpty()) {
    summary = QString("PakFu %1 is available (you have %2).")
                .arg(normalize_version(info.version), normalize_version(current_version_));
  }

  QMessageBox box(dialog_parent);
  box.setIcon(QMessageBox::Information);
  box.setWindowTitle("Update Available");
  box.setText(summary);
  if (!info.notes.trimmed().isEmpty()) {
    const QString trimmed = info.notes.trimmed();
    box.setInformativeText(trimmed.left(600));
  }

  QPushButton* download_button = nullptr;
  if (info.asset_url.isValid()) {
    download_button = box.addButton("Download && Install", QMessageBox::AcceptRole);
  }
  QPushButton* open_button = box.addButton("Open Release Page", QMessageBox::ActionRole);
  QPushButton* skip_button = box.addButton("Skip This Version", QMessageBox::RejectRole);
  box.addButton(user_initiated ? "Close" : "Later", QMessageBox::DestructiveRole);
  box.setDefaultButton(download_button ? download_button : open_button);
  box.exec();

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

  const QString temp_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (temp_dir.isEmpty()) {
    show_error_message(parent, "No temporary directory is available to store the update.");
    return;
  }

  const QString filename = info.asset_name.isEmpty()
                              ? QString("pakfu-update-%1").arg(normalize_version(info.version))
                              : info.asset_name;
  download_path_ = QDir(temp_dir).filePath(filename);

  download_file_.reset(new QSaveFile(download_path_));
  if (!download_file_->open(QIODevice::WriteOnly)) {
    show_error_message(parent, "Unable to create the update file.");
    download_file_.reset();
    return;
  }

  if (download_reply_) {
    download_reply_->deleteLater();
  }

  QNetworkRequest request(info.asset_url);
  request.setRawHeader("Accept", "application/octet-stream");
  request.setRawHeader("User-Agent", kUserAgent);
  download_reply_ = network_->get(request);

  progress_dialog_ = new QProgressDialog("Downloading update...", "Cancel", 0, 100, parent);
  progress_dialog_->setWindowModality(Qt::WindowModal);
  progress_dialog_->setAutoClose(true);
  progress_dialog_->setAutoReset(true);

  connect(download_reply_, &QNetworkReply::readyRead, this, &UpdateService::on_download_ready_read);
  connect(download_reply_, &QNetworkReply::downloadProgress, this, &UpdateService::on_download_progress);
  connect(download_reply_, &QNetworkReply::finished, this, &UpdateService::on_download_finished);
  connect(progress_dialog_, &QProgressDialog::canceled, download_reply_, &QNetworkReply::abort);
}

void UpdateService::on_download_ready_read() {
  if (download_reply_ && download_file_) {
    download_file_->write(download_reply_->readAll());
  }
}

void UpdateService::on_download_progress(qint64 received, qint64 total) {
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

void UpdateService::on_download_finished() {
  QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> reply(download_reply_);
  download_reply_ = nullptr;

  if (progress_dialog_) {
    progress_dialog_->close();
    progress_dialog_->deleteLater();
    progress_dialog_ = nullptr;
  }

  if (!reply) {
    return;
  }

  if (reply->error() != QNetworkReply::NoError) {
    show_error_message(parent_window_, "Update download failed.");
    if (download_file_) {
      download_file_->cancelWriting();
      download_file_.reset();
    }
    return;
  }

  if (download_file_) {
    download_file_->write(reply->readAll());
    if (!download_file_->commit()) {
      show_error_message(parent_window_, "Unable to finalize the downloaded update.");
      download_file_.reset();
      return;
    }
    download_file_.reset();
  }

  if (!launch_installer(download_path_, nullptr)) {
    show_error_message(parent_window_, "Downloaded update could not be launched.");
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

  const QString ext = asset_extension(file_path);
  bool launched = false;

#if defined(Q_OS_WIN)
  if (ext == "exe" || ext == "msi") {
    launched = QProcess::startDetached(file_path, {});
  } else {
    launched = QDesktopServices::openUrl(QUrl::fromLocalFile(file_path));
  }
#elif defined(Q_OS_MACOS)
  if (ext == "dmg" || ext == "pkg") {
    launched = QProcess::startDetached("open", {file_path});
  } else {
    launched = QDesktopServices::openUrl(QUrl::fromLocalFile(file_path));
  }
#else
  if (ext == "appimage") {
    QFile::setPermissions(file_path, QFile::permissions(file_path) | QFileDevice::ExeUser);
    launched = QProcess::startDetached(file_path, {});
  } else {
    launched = QDesktopServices::openUrl(QUrl::fromLocalFile(file_path));
  }
#endif

  if (launched) {
    const auto response = QMessageBox::question(
      parent,
      "Finish Update",
      "The installer has been launched. Quit PakFu now to complete the update?",
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::Yes);
    if (response == QMessageBox::Yes) {
      QCoreApplication::quit();
    }
  }

  return launched;
}
