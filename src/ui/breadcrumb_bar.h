#pragma once

#include <QWidget>

class QHBoxLayout;

class BreadcrumbBar : public QWidget {
  Q_OBJECT

public:
  explicit BreadcrumbBar(QWidget* parent = nullptr);
  void set_crumbs(const QStringList& crumbs);
  QStringList crumbs() const { return crumbs_; }

signals:
  void crumb_activated(int index);

private:
  void rebuild();

  QStringList crumbs_;
  QHBoxLayout* layout_ = nullptr;
};

