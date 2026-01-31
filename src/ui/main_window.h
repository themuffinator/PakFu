#pragma once

#include <QMainWindow>

class QAction;
class QMenu;
class QTabWidget;
class QWidget;
class QCloseEvent;

class UpdateService;
class PakTab;
class QUndoStack;

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(const QString& initial_pak_path = QString(), bool schedule_updates = true);

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  void setup_menus();
  void setup_central();
  void schedule_update_check();
  void check_for_updates();
  void create_new_pak();
  void open_pak_dialog();
  void open_pak(const QString& path);
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

  UpdateService* updater_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QWidget* welcome_tab_ = nullptr;
  QWidget* preferences_tab_ = nullptr;
  QAction* new_action_ = nullptr;
  QAction* open_action_ = nullptr;
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
  bool schedule_updates_ = true;
  int untitled_counter_ = 1;
};
