#pragma once

#include <QMainWindow>

class QAction;
class QTabWidget;
class QWidget;

class UpdateService;

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(const QString& initial_pak_path = QString(), bool schedule_updates = true);

private:
  void setup_menus();
  void setup_central();
  void schedule_update_check();
  void check_for_updates();
  void create_new_pak();
  void open_pak_dialog();
  void open_pak(const QString& path);
  void update_window_title();
  void close_tab(int index);
  int add_pak_tab(const QString& title, QWidget* tab);
  void focus_tab_by_path(const QString& path);

  UpdateService* updater_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QWidget* welcome_tab_ = nullptr;
  QAction* new_action_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* exit_action_ = nullptr;
  bool schedule_updates_ = true;
  int untitled_counter_ = 1;
};
