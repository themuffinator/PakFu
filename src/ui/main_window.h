#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QStringList>

#include "game/game_set.h"

class QAction;
class QComboBox;
class QMenu;
class QTabWidget;
class QWidget;
class QCloseEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;

class DropOverlay;
class UpdateService;
class PakTab;
class QUndoStack;
class PreferencesTab;
class ImageViewerWindow;
class VideoViewerWindow;
class AudioViewerWindow;
class ModelViewerWindow;

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(const GameSet& game_set,
                      const QString& initial_pak_path = QString(),
                      bool schedule_updates = true);

  void open_archives(const QStringList& paths);

protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void setup_menus();
  void setup_central();
  void load_game_sets();
  void rebuild_game_combo();
  void apply_game_set(const QString& uid, bool persist_selection);
  void open_game_set_manager();
  void auto_select_game_for_archive_path(const QString& path);
  void schedule_update_check();
  void check_for_updates();
  void create_new_pak();
  void open_file_dialog();
  void open_pak_dialog();
  void open_folder_dialog();
  void open_file_associations_dialog();
  void open_pak(const QString& path);
  bool open_image_viewer(const QString& file_path, bool add_recent);
  bool open_video_viewer(const QString& file_path, bool add_recent);
  bool open_audio_viewer(const QString& file_path, bool add_recent);
  bool open_model_viewer(const QString& file_path, bool add_recent);
  bool open_file_in_viewer(const QString& file_path, bool allow_auto_select, bool add_recent);
  QString resolve_archive_open_path(const QString& path, bool* cancelled);
  bool run_archive_open_action(const QString& source_path,
                               const QString& install_uid,
                               bool move_file,
                               QString* resulting_path,
                               QString* error);
  PakTab* open_pak_internal(const QString& path, bool allow_auto_select, bool add_recent);
  void save_current();
  void save_current_as();
  void open_preferences();
  void update_window_title();
  void close_tab(int index);
  int add_tab(const QString& title, QWidget* tab);
  bool focus_tab_by_path(const QString& path);
  PakTab* current_pak_tab() const;
  void update_action_states();
  bool maybe_save_tab(PakTab* tab);
  bool save_tab(PakTab* tab);
  bool save_tab_as(PakTab* tab);
  void set_tab_base_title(QWidget* tab, const QString& title);
  QString tab_base_title(QWidget* tab) const;
  void update_tab_label(QWidget* tab);

  void add_recent_file(const QString& path);
  void remove_recent_file(const QString& path);
  void clear_recent_files();
  void rebuild_recent_files_menu();
  QString default_directory_for_dialogs() const;
  void save_workspace_for_current_install();
  void restore_workspace_for_install(const QString& uid);
  void clear_archive_tabs();
  void update_pure_pak_protector_for_tabs();
  bool is_official_archive_for_current_install(const QString& path) const;
  bool pure_pak_protector_enabled() const;

  GameSetState game_sets_;
  GameSet game_set_;
  QComboBox* game_combo_ = nullptr;
  bool updating_game_combo_ = false;
  UpdateService* updater_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QWidget* welcome_tab_ = nullptr;
  PreferencesTab* preferences_tab_ = nullptr;
  QAction* new_action_ = nullptr;
  QAction* open_file_action_ = nullptr;
  QAction* open_archive_action_ = nullptr;
  QAction* open_folder_action_ = nullptr;
  QAction* file_associations_action_ = nullptr;
  QAction* save_action_ = nullptr;
  QAction* save_as_action_ = nullptr;
  QAction* undo_action_ = nullptr;
  QAction* redo_action_ = nullptr;
  QAction* cut_action_ = nullptr;
  QAction* copy_action_ = nullptr;
  QAction* paste_action_ = nullptr;
  QAction* rename_action_ = nullptr;
  QAction* preferences_action_ = nullptr;
  QAction* exit_action_ = nullptr;
  QMenu* recent_files_menu_ = nullptr;
  bool restoring_workspace_ = false;
  bool schedule_updates_ = true;
  int untitled_counter_ = 1;
  DropOverlay* drop_overlay_ = nullptr;
  QPointer<ImageViewerWindow> image_viewer_window_;
  QPointer<VideoViewerWindow> video_viewer_window_;
  QPointer<AudioViewerWindow> audio_viewer_window_;
  QPointer<ModelViewerWindow> model_viewer_window_;
};
