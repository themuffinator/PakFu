#pragma once

#include <QStringList>
#include <QVector>

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

