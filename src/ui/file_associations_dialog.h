#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QCheckBox;
class QLabel;
class QTabWidget;

class FileAssociationsDialog : public QDialog {
public:
  explicit FileAssociationsDialog(QWidget* parent = nullptr);

private:
  struct Row {
    QString extension;
    int tab_index = -1;
    QCheckBox* enabled = nullptr;
    QLabel* status = nullptr;
  };

  void refresh_status();
  void apply_changes();

  QVector<Row> rows_;
  QTabWidget* tabs_ = nullptr;
  QLabel* summary_label_ = nullptr;
};
