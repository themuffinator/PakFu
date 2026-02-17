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
    case GameId::HalfLife:
      return "half_life";
    case GameId::Doom:
      return "doom";
    case GameId::Doom2:
      return "doom2";
    case GameId::FinalDoom:
      return "final_doom";
    case GameId::Heretic:
      return "heretic";
    case GameId::Hexen:
      return "hexen";
    case GameId::Strife:
      return "strife";
    case GameId::Quake2:
      return "quake2";
    case GameId::Quake2Rerelease:
      return "quake2_rerelease";
    case GameId::Quake2RTX:
      return "quake2_rtx";
    case GameId::SiNGold:
      return "sin_gold";
    case GameId::KingpinLifeOfCrime:
      return "kingpin_life_of_crime";
    case GameId::Daikatana:
      return "daikatana";
    case GameId::Anachronox:
      return "anachronox";
    case GameId::Heretic2:
      return "heretic2";
    case GameId::GravityBone:
      return "gravity_bone";
    case GameId::ThirtyFlightsOfLoving:
      return "thirty_flights_of_loving";
    case GameId::Quake3Arena:
      return "quake3_arena";
    case GameId::QuakeLive:
      return "quake_live";
    case GameId::ReturnToCastleWolfenstein:
      return "return_to_castle_wolfenstein";
    case GameId::WolfensteinEnemyTerritory:
      return "wolfenstein_enemy_territory";
    case GameId::JediOutcast:
      return "jedi_outcast";
    case GameId::JediAcademy:
      return "jedi_academy";
    case GameId::StarTrekVoyagerEliteForce:
      return "elite_force";
    case GameId::EliteForce2:
      return "elite_force2";
    case GameId::Warsow:
      return "warsow";
    case GameId::Warfork:
      return "warfork";
    case GameId::WorldOfPadman:
      return "world_of_padman";
    case GameId::HeavyMetalFakk2:
      return "heavy_metal_fakk2";
    case GameId::Quake4:
      return "quake4";
    case GameId::Doom3:
      return "doom3";
    case GameId::Doom3BFGEdition:
      return "doom3_bfg_edition";
    case GameId::Prey:
      return "prey";
    case GameId::EnemyTerritoryQuakeWars:
      return "enemy_territory_quake_wars";
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
  if (key == "half_life" || key == "half-life" || key == "halflife") {
    return GameId::HalfLife;
  }
  if (key == "doom") {
    return GameId::Doom;
  }
  if (key == "doom2" || key == "doom_2" || key == "doomii" || key == "doom_ii") {
    return GameId::Doom2;
  }
  if (key == "final_doom" || key == "finaldoom") {
    return GameId::FinalDoom;
  }
  if (key == "heretic") {
    return GameId::Heretic;
  }
  if (key == "hexen") {
    return GameId::Hexen;
  }
  if (key == "strife") {
    return GameId::Strife;
  }
  if (key == "quake2") {
    return GameId::Quake2;
  }
  if (key == "quake2_rerelease") {
    return GameId::Quake2Rerelease;
  }
  if (key == "quake2_rtx" || key == "quake2rtx" || key == "quake_ii_rtx" || key == "q2rtx") {
    return GameId::Quake2RTX;
  }
  if (key == "sin_gold") {
    return GameId::SiNGold;
  }
  if (key == "kingpin_life_of_crime") {
    return GameId::KingpinLifeOfCrime;
  }
  if (key == "daikatana") {
    return GameId::Daikatana;
  }
  if (key == "anachronox") {
    return GameId::Anachronox;
  }
  if (key == "heretic2" || key == "heretic_2") {
    return GameId::Heretic2;
  }
  if (key == "gravity_bone") {
    return GameId::GravityBone;
  }
  if (key == "thirty_flights_of_loving") {
    return GameId::ThirtyFlightsOfLoving;
  }
  if (key == "quake3_arena") {
    return GameId::Quake3Arena;
  }
  if (key == "quake_live") {
    return GameId::QuakeLive;
  }
  if (key == "return_to_castle_wolfenstein") {
    return GameId::ReturnToCastleWolfenstein;
  }
  if (key == "wolfenstein_enemy_territory") {
    return GameId::WolfensteinEnemyTerritory;
  }
  if (key == "jedi_outcast") {
    return GameId::JediOutcast;
  }
  if (key == "jedi_academy") {
    return GameId::JediAcademy;
  }
  if (key == "elite_force") {
    return GameId::StarTrekVoyagerEliteForce;
  }
  if (key == "elite_force2" || key == "elite_force_2") {
    return GameId::EliteForce2;
  }
  if (key == "warsow") {
    return GameId::Warsow;
  }
  if (key == "warfork") {
    return GameId::Warfork;
  }
  if (key == "world_of_padman" || key == "worldofpadman") {
    return GameId::WorldOfPadman;
  }
  if (key == "heavy_metal_fakk2" || key == "fakk2") {
    return GameId::HeavyMetalFakk2;
  }
  if (key == "quake4") {
    return GameId::Quake4;
  }
  if (key == "doom3") {
    return GameId::Doom3;
  }
  if (key == "doom3_bfg_edition") {
    return GameId::Doom3BFGEdition;
  }
  if (key == "prey") {
    return GameId::Prey;
  }
  if (key == "enemy_territory_quake_wars") {
    return GameId::EnemyTerritoryQuakeWars;
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
    case GameId::HalfLife:
      return "Half-Life";
    case GameId::Doom:
      return "DOOM";
    case GameId::Doom2:
      return "DOOM II";
    case GameId::FinalDoom:
      return "Final DOOM";
    case GameId::Heretic:
      return "Heretic";
    case GameId::Hexen:
      return "Hexen";
    case GameId::Strife:
      return "Strife";
    case GameId::Quake2:
      return "Quake II";
    case GameId::Quake2Rerelease:
      return "Quake II Rerelease";
    case GameId::Quake2RTX:
      return "Quake II RTX";
    case GameId::SiNGold:
      return "SiN Gold";
    case GameId::KingpinLifeOfCrime:
      return "Kingpin: Life of Crime";
    case GameId::Daikatana:
      return "Daikatana";
    case GameId::Anachronox:
      return "Anachronox";
    case GameId::Heretic2:
      return "Heretic II";
    case GameId::GravityBone:
      return "Gravity Bone";
    case GameId::ThirtyFlightsOfLoving:
      return "Thirty Flights of Loving";
    case GameId::Quake3Arena:
      return "Quake III Arena";
    case GameId::QuakeLive:
      return "Quake Live";
    case GameId::ReturnToCastleWolfenstein:
      return "Return to Castle Wolfenstein";
    case GameId::WolfensteinEnemyTerritory:
      return "Wolfenstein: Enemy Territory";
    case GameId::JediOutcast:
      return "Star Wars Jedi Knight II: Jedi Outcast";
    case GameId::JediAcademy:
      return "Star Wars Jedi Knight: Jedi Academy";
    case GameId::StarTrekVoyagerEliteForce:
      return "Star Trek Voyager: Elite Force";
    case GameId::EliteForce2:
      return "Star Trek: Elite Force II";
    case GameId::Warsow:
      return "Warsow";
    case GameId::Warfork:
      return "Warfork";
    case GameId::WorldOfPadman:
      return "World of Padman";
    case GameId::HeavyMetalFakk2:
      return "Heavy Metal: F.A.K.K.2";
    case GameId::Quake4:
      return "Quake 4";
    case GameId::Doom3:
      return "Doom 3";
    case GameId::Doom3BFGEdition:
      return "Doom 3: BFG Edition";
    case GameId::Prey:
      return "Prey";
    case GameId::EnemyTerritoryQuakeWars:
      return "Enemy Territory: Quake Wars";
  }
  return "Quake";
}

QString default_palette_for_game(GameId id) {
  switch (id) {
    case GameId::Quake:
    case GameId::QuakeRerelease:
    case GameId::HalfLife:
      return "quake";
    case GameId::Doom:
    case GameId::Doom2:
    case GameId::FinalDoom:
    case GameId::Heretic:
    case GameId::Hexen:
    case GameId::Strife:
      return "doom";
    case GameId::Quake2:
    case GameId::Quake2Rerelease:
    case GameId::Quake2RTX:
    case GameId::SiNGold:
    case GameId::KingpinLifeOfCrime:
    case GameId::Daikatana:
    case GameId::Anachronox:
    case GameId::Heretic2:
    case GameId::GravityBone:
    case GameId::ThirtyFlightsOfLoving:
    case GameId::Quake3Arena:
    case GameId::QuakeLive:
    case GameId::ReturnToCastleWolfenstein:
    case GameId::WolfensteinEnemyTerritory:
    case GameId::JediOutcast:
    case GameId::JediAcademy:
    case GameId::StarTrekVoyagerEliteForce:
    case GameId::EliteForce2:
    case GameId::Warsow:
    case GameId::Warfork:
    case GameId::WorldOfPadman:
    case GameId::HeavyMetalFakk2:
    case GameId::Quake4:
    case GameId::Doom3:
    case GameId::Doom3BFGEdition:
    case GameId::Prey:
    case GameId::EnemyTerritoryQuakeWars:
      return "quake2";
  }
  return "quake";
}

QVector<GameId> supported_game_ids() {
  return {
    GameId::Quake,
    GameId::QuakeRerelease,
    GameId::HalfLife,
    GameId::Doom,
    GameId::Doom2,
    GameId::FinalDoom,
    GameId::Heretic,
    GameId::Hexen,
    GameId::Strife,
    GameId::Quake2,
    GameId::Quake2Rerelease,
    GameId::Quake2RTX,
    GameId::SiNGold,
    GameId::KingpinLifeOfCrime,
    GameId::Daikatana,
    GameId::Anachronox,
    GameId::Heretic2,
    GameId::GravityBone,
    GameId::ThirtyFlightsOfLoving,
    GameId::Quake3Arena,
    GameId::QuakeLive,
    GameId::ReturnToCastleWolfenstein,
    GameId::WolfensteinEnemyTerritory,
    GameId::JediOutcast,
    GameId::JediAcademy,
    GameId::StarTrekVoyagerEliteForce,
    GameId::EliteForce2,
    GameId::Warsow,
    GameId::Warfork,
    GameId::WorldOfPadman,
    GameId::HeavyMetalFakk2,
    GameId::Quake4,
    GameId::Doom3,
    GameId::Doom3BFGEdition,
    GameId::Prey,
    GameId::EnemyTerritoryQuakeWars,
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
