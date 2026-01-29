#pragma once

#include <QMainWindow>

class QLabel;
class QStackedWidget;

class UpdateService;

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(const QString& initial_pak_path = QString(), bool schedule_updates = true);

private:
  void setup_menus();
  void setup_central();
  void schedule_update_check();
  void check_for_updates();
  void open_pak(const QString& path);

  UpdateService* updater_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QLabel* status_label_ = nullptr;
  bool schedule_updates_ = true;
};
