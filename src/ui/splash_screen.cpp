#include "splash_screen.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

class SpinnerWidget : public QWidget {
public:
  explicit SpinnerWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedSize(36, 36);
  }

  void setAngle(int angle) {
    angle_ = angle;
    update();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds(4.0, 4.0, width() - 8.0, height() - 8.0);
    QPen pen(QColor(200, 200, 200, 220), 3.0, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(bounds, angle_ * 16, 280 * 16);
  }

private:
  int angle_ = 0;
};

SplashScreen::SplashScreen(const QPixmap& logo, QWidget* parent)
    : QWidget(parent), logo_(logo) {
  setAttribute(Qt::WA_TranslucentBackground);
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SplashScreen);

  setFixedSize(logo_.size());

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 32);
  layout->setSpacing(10);

  layout->addStretch(3);

  status_label_ = new QLabel("Starting...", this);
  status_label_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  status_label_->setStyleSheet("color: rgba(255, 255, 255, 230); font-size: 15px;");
  layout->addWidget(status_label_);

  auto* spinner_row = new QHBoxLayout();
  spinner_row->addStretch();
  spinner_ = new SpinnerWidget(this);
  spinner_row->addWidget(spinner_);
  spinner_row->addStretch();
  layout->addLayout(spinner_row);

  version_label_ = new QLabel("v0.0.0", this);
  version_label_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  version_label_->setStyleSheet("color: rgba(255, 255, 255, 180); font-size: 12px;");
  layout->addWidget(version_label_);

  layout->addStretch(1);

  spinner_timer_ = new QTimer(this);
  spinner_timer_->setInterval(60);
  connect(spinner_timer_, &QTimer::timeout, this, &SplashScreen::on_tick);
  spinner_timer_->start();
}

void SplashScreen::setStatusText(const QString& text) {
  if (status_label_) {
    status_label_->setText(text);
  }
}

void SplashScreen::setVersionText(const QString& text) {
  if (version_label_) {
    version_label_->setText(text);
  }
}

void SplashScreen::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawPixmap(0, 0, logo_);
}

void SplashScreen::on_tick() {
  spinner_angle_ = (spinner_angle_ + 12) % 360;
  if (spinner_) {
    spinner_->setAngle(spinner_angle_);
  }
}
