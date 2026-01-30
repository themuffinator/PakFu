#pragma once

#include <QWidget>

#include "pak/pak_archive.h"

class BreadcrumbBar;
class QTreeWidget;

class PakTab : public QWidget {
  Q_OBJECT

public:
  enum class Mode {
    NewPak,
    ExistingPak,
  };

  explicit PakTab(Mode mode, const QString& pak_path, QWidget* parent = nullptr);

  QString pak_path() const { return pak_path_; }
  bool is_loaded() const { return loaded_; }
  QString load_error() const { return load_error_; }

private:
  void build_ui();
  void load_archive();
  void set_current_dir(const QStringList& parts);
  void refresh_listing();

  void enter_directory(const QString& name);
  void activate_crumb(int index);

  Mode mode_;
  QString pak_path_;
  bool loaded_ = false;
  QString load_error_;

  BreadcrumbBar* breadcrumbs_ = nullptr;
  QTreeWidget* listing_ = nullptr;
  QStringList current_dir_;
  QVector<PakEntry> entries_;
};
