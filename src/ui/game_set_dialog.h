#pragma once

#include <optional>

#include <QDialog>

#include "game/game_set.h"

class QLabel;
class QListView;
class QPushButton;
class QStandardItemModel;

class GameSetDialog : public QDialog {
public:
  explicit GameSetDialog(QWidget* parent = nullptr);

  [[nodiscard]] std::optional<GameSet> selected_game_set() const;

private:
  void build_ui();
  void load_state();
  void save_state_or_warn();
  void refresh_list();
  void update_ui_state();
  [[nodiscard]] QString selected_uid() const;
  [[nodiscard]] GameSet* selected_set();
  void add_game_set();
  void configure_game_set();
  void remove_game_set();
  void auto_detect();
  void open_selected();

  GameSetState state_;

  QListView* list_ = nullptr;
  QStandardItemModel* list_model_ = nullptr;
  QLabel* hint_label_ = nullptr;
  QPushButton* add_button_ = nullptr;
  QPushButton* configure_button_ = nullptr;
  QPushButton* remove_button_ = nullptr;
  QPushButton* auto_detect_button_ = nullptr;
  QPushButton* open_button_ = nullptr;
};
