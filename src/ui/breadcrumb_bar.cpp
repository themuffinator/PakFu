#include "breadcrumb_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

BreadcrumbBar::BreadcrumbBar(QWidget* parent) : QWidget(parent) {
  layout_ = new QHBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(8);
  rebuild();
}

void BreadcrumbBar::set_crumbs(const QStringList& crumbs) {
  crumbs_ = crumbs;
  rebuild();
}

void BreadcrumbBar::rebuild() {
  if (!layout_) {
    return;
  }

  while (QLayoutItem* item = layout_->takeAt(0)) {
    if (QWidget* w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }

  if (crumbs_.isEmpty()) {
    crumbs_ = {"Root"};
  }

  for (int i = 0; i < crumbs_.size(); ++i) {
    auto* btn = new QToolButton(this);
    btn->setText(crumbs_[i]);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    connect(btn, &QToolButton::clicked, this, [this, i]() { emit crumb_activated(i); });
    layout_->addWidget(btn);

    if (i + 1 < crumbs_.size()) {
      auto* sep = new QLabel(">", this);
      sep->setAlignment(Qt::AlignCenter);
      sep->setStyleSheet("color: palette(mid);");
      layout_->addWidget(sep);
    }
  }

  layout_->addStretch();
}
