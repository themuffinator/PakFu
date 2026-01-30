#pragma once

#include <QWidget>

class QLabel;
class SpinnerWidget;
class QTimer;
class QFrame;

class SplashScreen : public QWidget {
  Q_OBJECT

public:
  explicit SplashScreen(const QPixmap& logo, QWidget* parent = nullptr);
  void setStatusText(const QString& text);
  void setVersionText(const QString& text);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void on_tick();

  QPixmap logo_;
  QFrame* info_box_ = nullptr;
  QLabel* status_label_ = nullptr;
  QLabel* version_label_ = nullptr;
  SpinnerWidget* spinner_ = nullptr;
  QTimer* spinner_timer_ = nullptr;
  int spinner_angle_ = 0;
};
