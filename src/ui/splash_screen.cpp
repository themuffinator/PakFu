#include "splash_screen.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

class SpinnerWidget : public QWidget {
public:
  explicit SpinnerWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedSize(20, 20);
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

    const QRectF bounds(2.5, 2.5, width() - 5.0, height() - 5.0);
    QPen pen(QColor(220, 220, 220, 220), 2.25, Qt::SolidLine, Qt::RoundCap);
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
  layout->setContentsMargins(0, 0, 0, qMax(6, static_cast<int>(height() * 0.015)));
  layout->setSpacing(0);

  layout->addStretch(1);

  const int box_height = qMax(44, static_cast<int>(height() * 0.05));
  info_box_ = new QFrame(this);
  info_box_->setObjectName("splashInfoBox");
  info_box_->setFixedHeight(box_height);
  info_box_->setMaximumWidth(static_cast<int>(width() * 0.92));
  info_box_->setStyleSheet(
    "#splashInfoBox {"
    "  background-color: rgba(0, 0, 0, 255);"
    "  border: 1px solid rgba(160, 160, 160, 180);"
    "  border-radius: 12px;"
    "}");

  auto* box_layout = new QHBoxLayout(info_box_);
  box_layout->setContentsMargins(14, 8, 14, 8);
  box_layout->setSpacing(10);

  spinner_ = new SpinnerWidget(info_box_);
  box_layout->addWidget(spinner_, 0, Qt::AlignVCenter);

  status_label_ = new QLabel("Starting...", info_box_);
  status_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  status_label_->setStyleSheet("color: rgba(255, 255, 255, 230); font-size: 13px;");
  status_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  status_label_->setWordWrap(false);
  box_layout->addWidget(status_label_, 1);

  version_label_ = new QLabel("v0.0.0", info_box_);
  version_label_->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
  version_label_->setStyleSheet("color: rgba(220, 220, 220, 200); font-size: 12px;");
  box_layout->addWidget(version_label_, 0, Qt::AlignVCenter);

  layout->addWidget(info_box_, 0, Qt::AlignHCenter | Qt::AlignBottom);

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
