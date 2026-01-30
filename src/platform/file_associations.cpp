#include "file_associations.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace {
QString quoted(const QString& s) {
  QString out = s;
  out.replace('"', "\\\"");
  return '"' + out + '"';
}
}  // namespace

bool FileAssociations::is_pak_registered(QString* details) {
#if defined(Q_OS_WIN)
  const QString exe = QCoreApplication::applicationFilePath();
  const QString exe_name = QFileInfo(exe).fileName();

  QSettings ext("HKEY_CURRENT_USER\\Software\\Classes\\.pak", QSettings::NativeFormat);
  const QString prog_id = ext.value(".").toString();

  QSettings cmd("HKEY_CURRENT_USER\\Software\\Classes\\PakFu.pak\\shell\\open\\command", QSettings::NativeFormat);
  const QString open_cmd = cmd.value(".").toString();

  const QString expected_cmd = QString("%1 \"%2\"").arg(quoted(QDir::toNativeSeparators(exe)), "%1");
  const bool ok = (prog_id == "PakFu.pak") && open_cmd.contains(exe_name, Qt::CaseInsensitive);

  if (details) {
    *details = QString(".pak ProgID: %1\nOpen command: %2\nExpected: %3")
                 .arg(prog_id.isEmpty() ? "<unset>" : prog_id,
                      open_cmd.isEmpty() ? "<unset>" : open_cmd,
                      expected_cmd);
  }
  return ok;
#else
  if (details) {
    *details = "File associations are installer-managed on this platform.";
  }
  return false;
#endif
}

bool FileAssociations::apply_pak_registration(QString* error) {
#if defined(Q_OS_WIN)
  const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  if (exe.isEmpty()) {
    if (error) {
      *error = "Unable to determine application path.";
    }
    return false;
  }

  const QString exe_name = QFileInfo(exe).fileName();
  const QString prog_id = "PakFu.pak";
  const QString friendly = "PakFu Archive";
  const QString open_cmd = QString("%1 \"%2\"").arg(quoted(exe), "%1");
  const QString icon = QString("%1,0").arg(quoted(exe));

  QSettings ext("HKEY_CURRENT_USER\\Software\\Classes\\.pak", QSettings::NativeFormat);
  ext.setValue(".", prog_id);

  QSettings prog(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(prog_id), QSettings::NativeFormat);
  prog.setValue(".", friendly);

  QSettings icon_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\DefaultIcon").arg(prog_id), QSettings::NativeFormat);
  icon_key.setValue(".", icon);

  QSettings cmd_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\shell\\open\\command").arg(prog_id), QSettings::NativeFormat);
  cmd_key.setValue(".", open_cmd);

  // Also register under "Applications\\pakfu.exe" so Windows can present it in picker UI.
  QSettings app_cmd(QString("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\%1\\shell\\open\\command").arg(exe_name),
                    QSettings::NativeFormat);
  app_cmd.setValue(".", open_cmd);

  ext.sync();
  prog.sync();
  icon_key.sync();
  cmd_key.sync();
  app_cmd.sync();

  // Windows 10/11 typically require user confirmation via "Default apps" UI, but this at least
  // registers the ProgID and command so it can be chosen.
  QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));

  return true;
#else
  if (error) {
    *error = "File associations are installer-managed on this platform.";
  }
  return false;
#endif
}
