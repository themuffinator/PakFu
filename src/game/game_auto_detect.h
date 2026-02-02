#pragma once

#include <QStringList>
#include <QVector>

#include <optional>

#include "game/game_set.h"

struct DetectedGameInstall {
  GameId game = GameId::Quake;
  QString root_dir;
  QString default_dir;
  GameLaunchSettings launch;
};

struct GameAutoDetectResult {
  QVector<DetectedGameInstall> installs;
  QStringList log;
};

[[nodiscard]] GameAutoDetectResult auto_detect_supported_games();

// Best-effort detection of a supported game by inspecting a file or directory path on disk.
// This checks for known marker files/folders and executable names in the directory and its parents.
[[nodiscard]] std::optional<GameId> detect_game_id_for_path(const QString& file_or_dir_path);
