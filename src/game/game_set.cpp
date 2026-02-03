#include "game/game_set.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {
constexpr char kStateKey[] = "gameInstalls/stateJson";
constexpr char kLegacyStateKey[] = "gameSets/stateJson";
constexpr int kStateVersion = 1;

QString normalize_json_string(const QByteArray& bytes) {
  if (bytes.isEmpty()) {
    return {};
  }
  const QString s = QString::fromUtf8(bytes);
  return s.trimmed();
}

QJsonObject launch_to_json(const GameLaunchSettings& launch) {
  QJsonObject obj;
  if (!launch.executable_path.isEmpty()) {
    obj.insert("executablePath", launch.executable_path);
  }
  if (!launch.arguments.isEmpty()) {
    obj.insert("arguments", launch.arguments);
  }
  if (!launch.working_dir.isEmpty()) {
    obj.insert("workingDir", launch.working_dir);
  }
  return obj;
}

GameLaunchSettings launch_from_json(const QJsonObject& obj) {
  GameLaunchSettings out;
  out.executable_path = obj.value("executablePath").toString();
  out.arguments = obj.value("arguments").toString();
  out.working_dir = obj.value("workingDir").toString();
  return out;
}

QJsonObject game_set_to_json(const GameSet& set) {
  QJsonObject obj;
  obj.insert("uid", set.uid);
  obj.insert("game", game_id_key(set.game));
  obj.insert("name", set.name);
  obj.insert("rootDir", set.root_dir);
  obj.insert("defaultDir", set.default_dir);
  obj.insert("palette", set.palette_id);
  obj.insert("launch", launch_to_json(set.launch));
  return obj;
}

GameSet game_set_from_json(const QJsonObject& obj) {
  GameSet out;
  out.uid = obj.value("uid").toString();
  bool ok = false;
  out.game = game_id_from_key(obj.value("game").toString(), &ok);
  if (!ok) {
    out.game = GameId::Quake;
  }
  out.name = obj.value("name").toString();
  out.root_dir = obj.value("rootDir").toString();
  out.default_dir = obj.value("defaultDir").toString();
  out.palette_id = obj.value("palette").toString();
  out.launch = launch_from_json(obj.value("launch").toObject());
  return out;
}
}  // namespace

QString game_id_key(GameId id) {
  switch (id) {
    case GameId::Quake:
      return "quake";
    case GameId::QuakeRerelease:
      return "quake_rerelease";
    case GameId::Quake2:
      return "quake2";
    case GameId::Quake2Rerelease:
      return "quake2_rerelease";
    case GameId::Quake3Arena:
      return "quake3_arena";
    case GameId::QuakeLive:
      return "quake_live";
    case GameId::Quake4:
      return "quake4";
  }
  return "quake";
}

GameId game_id_from_key(const QString& key, bool* ok) {
  if (ok) {
    *ok = true;
  }
  if (key == "quake") {
    return GameId::Quake;
  }
  if (key == "quake_rerelease") {
    return GameId::QuakeRerelease;
  }
  if (key == "quake2") {
    return GameId::Quake2;
  }
  if (key == "quake2_rerelease") {
    return GameId::Quake2Rerelease;
  }
  if (key == "quake3_arena") {
    return GameId::Quake3Arena;
  }
  if (key == "quake_live") {
    return GameId::QuakeLive;
  }
  if (key == "quake4") {
    return GameId::Quake4;
  }
  if (ok) {
    *ok = false;
  }
  return GameId::Quake;
}

QString game_display_name(GameId id) {
  switch (id) {
    case GameId::Quake:
      return "Quake";
    case GameId::QuakeRerelease:
      return "Quake Rerelease";
    case GameId::Quake2:
      return "Quake II";
    case GameId::Quake2Rerelease:
      return "Quake II Rerelease";
    case GameId::Quake3Arena:
      return "Quake III Arena";
    case GameId::QuakeLive:
      return "Quake Live";
    case GameId::Quake4:
      return "Quake 4";
  }
  return "Quake";
}

QString default_palette_for_game(GameId id) {
  switch (id) {
    case GameId::Quake:
    case GameId::QuakeRerelease:
      return "quake";
    case GameId::Quake2:
    case GameId::Quake2Rerelease:
    case GameId::Quake3Arena:
    case GameId::QuakeLive:
    case GameId::Quake4:
      return "quake2";
  }
  return "quake";
}

QVector<GameId> supported_game_ids() {
  return {
    GameId::Quake,
    GameId::QuakeRerelease,
    GameId::Quake2,
    GameId::Quake2Rerelease,
    GameId::Quake3Arena,
    GameId::QuakeLive,
    GameId::Quake4,
  };
}

GameSetState load_game_set_state(QString* error) {
  if (error) {
    error->clear();
  }

  QSettings settings;
  QString raw = settings.value(kStateKey).toString().trimmed();
  bool migrated = false;
  if (raw.isEmpty()) {
    raw = settings.value(kLegacyStateKey).toString().trimmed();
    if (!raw.isEmpty()) {
      migrated = true;
    }
  }
  if (raw.isEmpty()) {
    return {};
  }

  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parse_error);
  if (doc.isNull() || !doc.isObject()) {
    if (error) {
      *error =
        parse_error.errorString().isEmpty() ? "Invalid game set settings." : parse_error.errorString();
    }
    return {};
  }

  const QJsonObject root = doc.object();
  const int version = root.value("version").toInt(0);
  if (version != kStateVersion) {
    if (error) {
      *error = QString("Unsupported game set settings version: %1").arg(version);
    }
    return {};
  }

  GameSetState out;
  out.selected_uid = root.value("selectedUid").toString();
  const QJsonArray sets = root.value("sets").toArray();
  out.sets.reserve(sets.size());
  for (const QJsonValue& v : sets) {
    if (!v.isObject()) {
      continue;
    }
    GameSet set = game_set_from_json(v.toObject());
    if (set.uid.isEmpty()) {
      continue;
    }
    if (set.name.isEmpty()) {
      set.name = game_display_name(set.game);
    }
    if (set.palette_id.isEmpty()) {
      set.palette_id = default_palette_for_game(set.game);
    }
    out.sets.push_back(set);
  }

  if (migrated) {
    (void)save_game_set_state(out);
  }
  return out;
}

bool save_game_set_state(const GameSetState& state, QString* error) {
  if (error) {
    error->clear();
  }

  QJsonObject root;
  root.insert("version", kStateVersion);
  root.insert("selectedUid", state.selected_uid);

  QJsonArray sets;
  for (const GameSet& set : state.sets) {
    if (set.uid.isEmpty()) {
      continue;
    }
    GameSet normalized = set;
    if (normalized.name.isEmpty()) {
      normalized.name = game_display_name(normalized.game);
    }
    if (normalized.palette_id.isEmpty()) {
      normalized.palette_id = default_palette_for_game(normalized.game);
    }
    sets.append(game_set_to_json(normalized));
  }
  root.insert("sets", sets);

  const QJsonDocument doc(root);
  const QString json = normalize_json_string(doc.toJson(QJsonDocument::Compact));

  QSettings settings;
  settings.setValue(kStateKey, json);
  settings.sync();

  if (settings.status() != QSettings::NoError) {
    if (error) {
      *error = "Failed to save game set settings.";
    }
    return false;
  }
  return true;
}

const GameSet* find_game_set(const GameSetState& state, const QString& uid) {
  if (uid.isEmpty()) {
    return nullptr;
  }
  for (const GameSet& set : state.sets) {
    if (set.uid == uid) {
      return &set;
    }
  }
  return nullptr;
}

GameSet* find_game_set(GameSetState& state, const QString& uid) {
  if (uid.isEmpty()) {
    return nullptr;
  }
  for (GameSet& set : state.sets) {
    if (set.uid == uid) {
      return &set;
    }
  }
  return nullptr;
}
