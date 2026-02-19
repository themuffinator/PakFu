#pragma once

#include <QIcon>
#include <QSize>
#include <QString>
#include <QStringList>

class FileAssociations {
public:
  // Returns all managed extensions without a leading dot.
  static QStringList managed_extensions();
  static QStringList managed_archive_extensions();
  static QStringList managed_image_extensions();
  static QStringList managed_video_extensions();
  static QStringList managed_audio_extensions();
  static QStringList managed_model_extensions();

  // Returns all managed extensions this helper manages (for UI/help text).
  static QString managed_extension_list();
  static QString managed_archive_extension_list();
  static QString managed_image_extension_list();
  static QString managed_video_extension_list();
  static QString managed_audio_extension_list();
  static QString managed_model_extension_list();

  static bool is_archive_extension(const QString& extension);
  static bool is_image_extension(const QString& extension);
  static bool is_video_extension(const QString& extension);
  static bool is_audio_extension(const QString& extension);
  static bool is_model_extension(const QString& extension);

  // Returns a generated file-association icon for a managed extension.
  // Returns a null icon when the extension is not managed.
  static QIcon icon_for_extension(const QString& extension, const QSize& icon_size = QSize(32, 32));

  // Returns true when a specific managed extension appears registered to PakFu.
  static bool is_extension_registered(const QString& extension, QString* details = nullptr);

  // Enables/disables one managed extension registration.
  static bool set_extension_registration(const QString& extension, bool enabled, QString* error = nullptr);

  // Opens the system's default apps UI when available.
  static void open_default_apps_settings();

  // Returns true if this app appears to be registered as a handler for all managed types.
  static bool is_pak_registered(QString* details);

  // Registers this app as a handler for all managed types (best-effort).
  // Note: On modern Windows, the user may still need to choose PakFu in "Default apps".
  static bool apply_pak_registration(QString* error);
};
