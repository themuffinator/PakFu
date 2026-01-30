#pragma once

#include <QHash>
#include <QPoint>
#include <QSet>
#include <QWidget>

#include "pak/pak_archive.h"

class BreadcrumbBar;
class QAction;
class QActionGroup;
class QListWidget;
class QStackedWidget;
class QToolBar;
class QToolButton;
class QTreeWidget;

class PakTab : public QWidget {
  Q_OBJECT

public:
  enum class Mode {
    NewPak,
    ExistingPak,
  };

  enum class ViewMode {
    Auto,
    Details,
    List,
    SmallIcons,
    LargeIcons,
    Gallery,
  };

  explicit PakTab(Mode mode, const QString& pak_path, QWidget* parent = nullptr);

  QString pak_path() const { return pak_path_; }
  bool is_loaded() const { return loaded_; }
  QString load_error() const { return load_error_; }
  bool is_dirty() const { return dirty_; }
  bool save(QString* error);
  bool save_as(const QString& dest_path, QString* error);

signals:
  void dirty_changed(bool dirty);

private:
  struct AddedFile {
    QString pak_name;
    QString source_path;
    quint32 size = 0;
    qint64 mtime_utc_secs = 0;
  };

  void build_ui();
  void load_archive();
  void set_current_dir(const QStringList& parts);
  void refresh_listing();

  void setup_actions();
  void show_context_menu(QWidget* view, const QPoint& pos);
  void add_files();
  void add_folder();
  void new_folder();
  void delete_selected(bool skip_confirmation);
  bool add_file_mapping(const QString& pak_name, const QString& source_path, QString* error);
  bool write_pak_file(const QString& dest_path, QString* error);
  QString current_prefix() const;
  void set_dirty(bool dirty);

  void set_view_mode(ViewMode mode);
  void apply_auto_view(int file_count, int image_count);
  void update_view_controls();
  void configure_icon_view();

  QString selected_pak_path(bool* is_dir) const;
  void rebuild_added_index();
  void remove_added_file_by_name(const QString& pak_name);
  bool is_deleted_path(const QString& pak_name) const;
  void clear_deletions_under(const QString& pak_name);

  void enter_directory(const QString& name);
  void activate_crumb(int index);

  Mode mode_;
  QString pak_path_;
  bool loaded_ = false;
  QString load_error_;

  BreadcrumbBar* breadcrumbs_ = nullptr;
  QToolBar* toolbar_ = nullptr;
  QToolButton* view_button_ = nullptr;
  QActionGroup* view_group_ = nullptr;
  QAction* add_files_action_ = nullptr;
  QAction* add_folder_action_ = nullptr;
  QAction* new_folder_action_ = nullptr;
  QAction* delete_action_ = nullptr;

  QAction* view_auto_action_ = nullptr;
  QAction* view_details_action_ = nullptr;
  QAction* view_list_action_ = nullptr;
  QAction* view_small_icons_action_ = nullptr;
  QAction* view_large_icons_action_ = nullptr;
  QAction* view_gallery_action_ = nullptr;

  QStackedWidget* view_stack_ = nullptr;
  QTreeWidget* details_view_ = nullptr;
  QListWidget* icon_view_ = nullptr;
  QStringList current_dir_;
  PakArchive archive_;
  QVector<AddedFile> added_files_;
  QHash<QString, int> added_index_by_name_;
  QSet<QString> virtual_dirs_;
  QSet<QString> deleted_files_;
  QSet<QString> deleted_dir_prefixes_;
  ViewMode view_mode_ = ViewMode::Auto;
  ViewMode effective_view_ = ViewMode::Details;
  bool dirty_ = false;
};
