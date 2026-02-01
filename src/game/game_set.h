#pragma once

#include <QString>
#include <QVector>

enum class GameId {
  Quake = 0,
  QuakeRerelease,
  Quake2,
  Quake2Rerelease,
  Quake3Arena,
  QuakeLive,
  Quake4,
};

QString game_id_key(GameId id);
GameId game_id_from_key(const QString& key, bool* ok = nullptr);
QString game_display_name(GameId id);
QString default_palette_for_game(GameId id);
QVector<GameId> supported_game_ids();

struct GameLaunchSettings {
  QString executable_path;
  QString arguments;
  QString working_dir;
};

struct GameSet {
  QString uid;
  GameId game = GameId::Quake;
  QString name;
  QString root_dir;
  QString default_dir;
  QString palette_id;
  GameLaunchSettings launch;
};

struct GameSetState {
  QVector<GameSet> sets;
  QString selected_uid;
};

[[nodiscard]] GameSetState load_game_set_state(QString* error = nullptr);
[[nodiscard]] bool save_game_set_state(const GameSetState& state, QString* error = nullptr);

[[nodiscard]] const GameSet* find_game_set(const GameSetState& state, const QString& uid);
[[nodiscard]] GameSet* find_game_set(GameSetState& state, const QString& uid);
