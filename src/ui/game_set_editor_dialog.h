#pragma once

#include <QDialog>

#include "game/game_set.h"

class QComboBox;
class QLineEdit;

class GameSetEditorDialog : public QDialog {
public:
  explicit GameSetEditorDialog(const GameSet& initial, QWidget* parent = nullptr);

  GameSet edited_game_set() const { return edited_; }

private:
  void build_ui();
  void load_from_set(const GameSet& set);
  void apply_game_defaults(GameId game);
  void browse_root_dir();
  void browse_default_dir();
  void browse_executable();
  void browse_working_dir();
  [[nodiscard]] bool validate_and_apply();
  [[nodiscard]] GameId game_from_combo() const;
  [[nodiscard]] QString palette_from_combo() const;
  [[nodiscard]] QString suggested_default_dir(GameId game, const QString& root_dir) const;

  GameSet edited_;

  QComboBox* game_combo_ = nullptr;
  QLineEdit* name_edit_ = nullptr;
  QLineEdit* root_dir_edit_ = nullptr;
  QLineEdit* default_dir_edit_ = nullptr;
  QComboBox* palette_combo_ = nullptr;
  QLineEdit* exe_edit_ = nullptr;
  QLineEdit* args_edit_ = nullptr;
  QLineEdit* working_dir_edit_ = nullptr;
};

