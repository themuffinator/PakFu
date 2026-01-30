#pragma once

#include <QWidget>

#include "ui/theme_manager.h"

class QComboBox;
class QLabel;
class QPushButton;

class PreferencesTab : public QWidget {
  Q_OBJECT

public:
  explicit PreferencesTab(QWidget* parent = nullptr);

signals:
  void theme_changed(AppTheme theme);

private:
  void build_ui();
  void load_settings();
  void apply_theme_from_combo();
  void refresh_association_status();
  void apply_association();

  QComboBox* theme_combo_ = nullptr;
  QLabel* assoc_status_ = nullptr;
  QPushButton* assoc_apply_ = nullptr;
  QPushButton* assoc_details_ = nullptr;
};
