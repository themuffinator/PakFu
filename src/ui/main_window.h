#pragma once

#include <QMainWindow>

class UpdateService;

class MainWindow : public QMainWindow {
public:
  MainWindow();

private:
  void setup_menus();
  void schedule_update_check();
  void check_for_updates();

  UpdateService* updater_ = nullptr;
};
