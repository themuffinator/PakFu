#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QScopedPointer>
#include <QSaveFile>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QTimer;
class QWidget;

struct UpdateInfo {
  QString version;
  QString title;
  QString notes;
  QUrl html_url;
  bool prerelease = false;
  QUrl asset_url;
  QString asset_name;
  qint64 asset_size = 0;
};

enum class UpdateCheckState {
  UpdateAvailable,
  UpToDate,
  NoRelease,
  NotConfigured,
  Error,
};

struct UpdateCheckResult {
  UpdateCheckState state = UpdateCheckState::Error;
  UpdateInfo info;
  QString message;
};

class UpdateService : public QObject {
  Q_OBJECT

public:
  explicit UpdateService(QObject* parent = nullptr);

  void configure(const QString& github_repo, const QString& channel, const QString& current_version);
  void set_dialogs_enabled(bool enabled);
  void check_for_updates(bool user_initiated, QWidget* parent = nullptr);
  UpdateCheckResult check_for_updates_sync();
  void show_update_prompt(const UpdateInfo& info, QWidget* parent, bool user_initiated);
  void abort_checks();

signals:
  void check_completed(const UpdateCheckResult& result);

private slots:
  void on_check_finished();
  void on_download_ready_read();
  void on_download_progress(qint64 received, qint64 total);
  void on_download_finished();

private:
  UpdateInfo parse_release_object(const QJsonObject& release_obj) const;
  UpdateInfo select_release_from_array(const QJsonArray& releases) const;
  QUrl select_asset(const QJsonArray& assets, QString* asset_name, qint64* asset_size) const;
  QString normalize_version(const QString& version) const;
  bool is_newer_version(const QString& latest, const QString& current) const;
  bool is_installable_asset(const QString& name) const;
  void show_no_update_message(QWidget* parent) const;
  void show_error_message(QWidget* parent, const QString& message) const;
  void prompt_update_error(const QString& message);
  void prompt_update(const UpdateInfo& info, QWidget* parent, bool user_initiated);
  void begin_download(const UpdateInfo& info, QWidget* parent);
  bool launch_installer(const QString& file_path, QWidget* parent) const;

  QString github_repo_;
  QString channel_;
  QString current_version_;
  bool user_initiated_ = false;

  QNetworkAccessManager* network_ = nullptr;
  QPointer<QNetworkReply> check_reply_;
  QPointer<QNetworkReply> download_reply_;
  QPointer<QTimer> check_timeout_;
  QString check_error_override_;
  QScopedPointer<QSaveFile> download_file_;
  QString download_path_;
  bool download_installable_ = false;
  QPointer<QProgressDialog> progress_dialog_;
  QPointer<QWidget> parent_window_;
  bool dialogs_enabled_ = true;
};

Q_DECLARE_METATYPE(UpdateCheckResult)
