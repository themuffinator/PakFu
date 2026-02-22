#include "game/game_auto_detect.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

namespace {
QString clean_path(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool paths_equal(const QString& a, const QString& b) {
#if defined(Q_OS_WIN)
  return a.compare(b, Qt::CaseInsensitive) == 0;
#else
  return a == b;
#endif
}

QStringList dedupe_existing_dirs(const QStringList& paths) {
  QStringList out;
  out.reserve(paths.size());
  for (const QString& p : paths) {
    const QString cleaned = clean_path(p);
    if (cleaned.isEmpty()) {
      continue;
    }
    if (!QFileInfo::exists(cleaned)) {
      continue;
    }
    if (!QFileInfo(cleaned).isDir()) {
      continue;
    }

    bool seen = false;
    for (const QString& existing : out) {
      if (paths_equal(existing, cleaned)) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      out.push_back(cleaned);
    }
  }
  return out;
}

QStringList steam_root_candidates() {
  QStringList roots;

#if defined(Q_OS_WIN)
  {
    QSettings reg("HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat);
    const QString steam_path = reg.value("SteamPath").toString();
    if (!steam_path.isEmpty()) {
      roots.push_back(steam_path);
    }
  }

  const QString pf86 = qEnvironmentVariable("PROGRAMFILES(X86)");
  if (!pf86.isEmpty()) {
    roots.push_back(QDir(pf86).filePath("Steam"));
  }
  const QString pf = qEnvironmentVariable("PROGRAMFILES");
  if (!pf.isEmpty()) {
    roots.push_back(QDir(pf).filePath("Steam"));
  }
#elif defined(Q_OS_MACOS)
  roots.push_back(QDir::home().filePath("Library/Application Support/Steam"));
#else
  // Linux + other unix-likes.
  roots.push_back(QDir::home().filePath(".steam/steam"));
  roots.push_back(QDir::home().filePath(".local/share/Steam"));
  // Flatpak Steam.
  roots.push_back(QDir::home().filePath(".var/app/com.valvesoftware.Steam/.steam/steam"));
#endif

  return dedupe_existing_dirs(roots);
}

QStringList parse_steam_library_paths(const QString& vdf_text) {
  QStringList out;
  const QRegularExpression re(R"VDF("path"\s*"([^"]+)")VDF");
  QRegularExpressionMatchIterator it = re.globalMatch(vdf_text);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QString raw = m.captured(1);
    QString path = raw;
    // Steam escapes Windows paths as e.g. D:\\SteamLibrary.
    path.replace("\\\\", "\\");
    path = clean_path(path);
    if (!path.isEmpty() && !out.contains(path)) {
      out.push_back(path);
    }
  }
  return out;
}

QStringList steam_library_dirs(const QStringList& steam_roots) {
  QStringList libs;
  libs.reserve(steam_roots.size() * 2);

  for (const QString& root : steam_roots) {
    const QString root_clean = clean_path(root);
    if (root_clean.isEmpty()) {
      continue;
    }

    if (!libs.contains(root_clean)) {
      libs.push_back(root_clean);
    }

    const QString vdf_path = QDir(root_clean).filePath("steamapps/libraryfolders.vdf");
    QFile f(vdf_path);
    if (!f.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QString text = QString::fromUtf8(f.readAll());
    const QStringList vdf_paths = parse_steam_library_paths(text);
    for (const QString& p : vdf_paths) {
      if (!libs.contains(p)) {
        libs.push_back(p);
      }
    }
  }

  return dedupe_existing_dirs(libs);
}

QStringList steam_common_dirs() {
  const QStringList steam_roots = steam_root_candidates();
  const QStringList libs = steam_library_dirs(steam_roots);

  QStringList common_dirs;
  common_dirs.reserve(libs.size());
  for (const QString& lib : libs) {
    const QString common = QDir(lib).filePath("steamapps/common");
    if (QFileInfo::exists(common) && QFileInfo(common).isDir()) {
      common_dirs.push_back(clean_path(common));
    }
  }
  return dedupe_existing_dirs(common_dirs);
}

QStringList roots_from_named_folders(const QStringList& parent_dirs, const QStringList& folder_names) {
  QStringList out;
  for (const QString& parent : parent_dirs) {
    const QDir base(parent);
    for (const QString& folder : folder_names) {
      const QString root = clean_path(base.filePath(folder));
      if (root.isEmpty()) {
        continue;
      }
      if (QFileInfo::exists(root) && QFileInfo(root).isDir()) {
        out.push_back(root);
      }
    }
  }
  return dedupe_existing_dirs(out);
}

QString first_settings_value(QSettings& settings, const QStringList& keys) {
  for (const QString& key : keys) {
    const QString v = settings.value(key).toString().trimmed();
    if (!v.isEmpty()) {
      return v;
    }
  }
  return {};
}

QStringList gog_registry_roots() {
  QStringList roots;

#if defined(Q_OS_WIN)
  const QStringList base_keys = {
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\GOG.com\\Games",
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\GOG.com\\Games",
    "HKEY_CURRENT_USER\\SOFTWARE\\GOG.com\\Games",
  };
  const QStringList path_keys = {
    "path",
    "Path",
    "installPath",
    "InstallPath",
    "installLocation",
    "InstallLocation",
  };

  for (const QString& base_key : base_keys) {
    QSettings reg(base_key, QSettings::NativeFormat);
    const QStringList groups = reg.childGroups();
    for (const QString& group : groups) {
      reg.beginGroup(group);
      const QString path = first_settings_value(reg, path_keys);
      reg.endGroup();
      if (!path.isEmpty()) {
        roots.push_back(path);
      }
    }
  }
#endif

  return dedupe_existing_dirs(roots);
}

QStringList gog_base_dirs() {
  QStringList bases;

  bases.push_back(QDir::home().filePath("GOG Games"));

#if defined(Q_OS_WIN)
  bases.push_back("C:/GOG Games");

  const QString pf86 = qEnvironmentVariable("PROGRAMFILES(X86)");
  if (!pf86.isEmpty()) {
    bases.push_back(QDir(pf86).filePath("GOG Galaxy/Games"));
  }
  const QString pf = qEnvironmentVariable("PROGRAMFILES");
  if (!pf.isEmpty()) {
    bases.push_back(QDir(pf).filePath("GOG Galaxy/Games"));
  }
#endif

  return dedupe_existing_dirs(bases);
}

QStringList epic_manifest_dirs() {
  QStringList dirs;

#if defined(Q_OS_WIN)
  QString program_data = qEnvironmentVariable("PROGRAMDATA");
  if (program_data.isEmpty()) {
    program_data = "C:/ProgramData";
  }
  const QDir base(program_data);
  dirs.push_back(base.filePath("Epic/EpicGamesLauncher/Data/Manifests"));
  dirs.push_back(base.filePath("Epic/UnrealEngineLauncher/Data/Manifests"));
#elif defined(Q_OS_MACOS)
  const QDir base(QDir::home().filePath("Library/Application Support"));
  dirs.push_back(base.filePath("Epic/EpicGamesLauncher/Data/Manifests"));
  dirs.push_back(base.filePath("Epic/UnrealEngineLauncher/Data/Manifests"));
#endif

  return dedupe_existing_dirs(dirs);
}

QStringList epic_install_roots(const QStringList& manifest_dirs) {
  QStringList roots;

  for (const QString& dir : manifest_dirs) {
    const QDir base(dir);
    const QStringList items = base.entryList({"*.item"}, QDir::Files);
    for (const QString& item : items) {
      QFile f(base.filePath(item));
      if (!f.open(QIODevice::ReadOnly)) {
        continue;
      }
      QJsonParseError parse_error;
      const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parse_error);
      if (doc.isNull() || !doc.isObject()) {
        continue;
      }
      const QJsonObject obj = doc.object();
      const QString install_location = obj.value("InstallLocation").toString().trimmed();
      if (!install_location.isEmpty()) {
        roots.push_back(install_location);
      }
    }
  }

  return dedupe_existing_dirs(roots);
}

QString first_existing_file(const QString& root, const QStringList& relative_paths) {
  if (root.isEmpty()) {
    return {};
  }
  const QDir base(root);
  for (const QString& rel : relative_paths) {
    const QString candidate = clean_path(base.filePath(rel));
    if (candidate.isEmpty()) {
      continue;
    }
    if (QFileInfo::exists(candidate) && QFileInfo(candidate).isFile()) {
      return candidate;
    }
  }
  return {};
}

bool any_marker_exists(const QString& root, const QStringList& relative_markers) {
  if (relative_markers.isEmpty()) {
    return QFileInfo::exists(root) && QFileInfo(root).isDir();
  }
  const QDir base(root);
  for (const QString& rel : relative_markers) {
    const QString candidate = clean_path(base.filePath(rel));
    if (candidate.isEmpty()) {
      continue;
    }
    if (QFileInfo::exists(candidate)) {
      return true;
    }
  }
  return false;
}

QString choose_default_dir(const QString& root, const QStringList& candidates) {
  if (root.isEmpty()) {
    return {};
  }
  const QDir base(root);
  for (const QString& rel : candidates) {
    const QString p = clean_path(base.filePath(rel));
    if (p.isEmpty()) {
      continue;
    }
    if (QFileInfo::exists(p) && QFileInfo(p).isDir()) {
      return p;
    }
  }
  return clean_path(root);
}

struct GameSupportInfo {
  GameId game = GameId::Quake;
  QStringList folder_names;
  QStringList marker_any;
  QStringList default_dir_candidates;
  QStringList executable_candidates;
};

QString normalize_name_token(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar c : text.toLower()) {
    if (c.isLetterOrNumber()) {
      out.push_back(c);
    }
  }
  return out;
}

bool root_matches_folder_names(const QString& root, const QStringList& folder_names) {
  if (folder_names.isEmpty()) {
    return true;
  }

  const QString cleaned = clean_path(root);
  QStringList normalized_components;
  const QStringList components = cleaned.split('/', Qt::SkipEmptyParts);
  normalized_components.reserve(components.size());
  for (const QString& component : components) {
    const QString normalized = normalize_name_token(component);
    if (!normalized.isEmpty()) {
      normalized_components.push_back(normalized);
    }
  }

  for (const QString& folder_name : folder_names) {
    const QString normalized_folder = normalize_name_token(folder_name);
    if (normalized_folder.isEmpty()) {
      continue;
    }
    for (const QString& normalized_component : normalized_components) {
      if (normalized_component == normalized_folder) {
        return true;
      }
    }
  }

  return false;
}

struct GameRootMatch {
  int score = -1;
  QString executable_path;
};

GameRootMatch match_root_for_support(const QString& root,
                                     const GameSupportInfo& support,
                                     bool require_folder_name_hint) {
  GameRootMatch out;

  const bool marker_match = any_marker_exists(root, support.marker_any);
  out.executable_path = first_existing_file(root, support.executable_candidates);
  const bool executable_match = !out.executable_path.isEmpty();

  if (!marker_match && !executable_match) {
    return out;
  }

  const bool folder_hint_match = root_matches_folder_names(root, support.folder_names);
  if (require_folder_name_hint && !folder_hint_match && !executable_match) {
    return out;
  }

  out.score = 0;
  if (marker_match) {
    out.score += 100;
  }
  if (executable_match) {
    out.score += 80;
  }
  if (folder_hint_match) {
    out.score += 30;
  }

  return out;
}

QVector<GameSupportInfo> supported_game_support() {
  QVector<GameSupportInfo> out;

  out.push_back(GameSupportInfo{
    .game = GameId::Quake,
    .folder_names = {"Quake"},
    .marker_any = {"id1/pak0.pak", "id1/PAK0.PAK"},
    .default_dir_candidates = {"id1"},
    .executable_candidates = {"quake.exe", "glquake.exe", "winquake.exe", "quake", "glquake"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::QuakeRerelease,
    .folder_names = {"Quake"},
    .marker_any = {"rerelease/id1/pak0.pak", "rerelease/id1/PAK0.PAK", "rerelease"},
    .default_dir_candidates = {"rerelease/id1", "rerelease"},
    .executable_candidates = {"Quake_x64.exe", "Quake.exe", "quake_x64.exe", "rerelease/Quake_x64.exe", "Quake"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::HalfLife,
    .folder_names = {"Half-Life", "Half Life", "Half-Life 1", "HalfLife"},
    .marker_any = {"valve/pak0.pak", "valve/PAK0.PAK", "valve_hd/pak0.pak", "valve_hd/PAK0.PAK",
                   "hl.exe", "hl.sh", "hl_linux"},
    .default_dir_candidates = {"valve", "valve_hd"},
    .executable_candidates = {"hl.exe", "hl", "hl.sh", "hl_linux", "hlds.exe", "hlds"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Doom,
    .folder_names = {"DOOM", "Ultimate DOOM", "The Ultimate DOOM"},
    .marker_any = {"base/DOOM.WAD", "base/doom.wad", "base/DOOMU.WAD", "base/doomu.wad", "DOOM.WAD", "doom.wad", "DOOMU.WAD", "doomu.wad",
                   "DOOM.exe", "doom.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"DOOM.exe", "doom.exe", "gzdoom.exe", "GZDoom.exe", "zdoom.exe", "ZDOOM.exe", "chocolate-doom.exe",
                              "crispy-doom.exe", "doom"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Doom2,
    .folder_names = {"DOOM II", "Doom II", "DOOM2", "Doom2"},
    .marker_any = {"base/DOOM2.WAD", "base/doom2.wad", "DOOM2.WAD", "doom2.wad", "DOOM2.exe", "doom2.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"DOOM2.exe", "doom2.exe", "gzdoom.exe", "GZDoom.exe", "zdoom.exe", "ZDOOM.exe", "chocolate-doom.exe",
                              "crispy-doom.exe", "doom2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::FinalDoom,
    .folder_names = {"Final DOOM", "Final Doom"},
    .marker_any = {"base/TNT.WAD", "base/tnt.wad", "base/PLUTONIA.WAD", "base/plutonia.wad",
                   "TNT.WAD", "tnt.wad", "PLUTONIA.WAD", "plutonia.wad"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"gzdoom.exe", "GZDoom.exe", "zdoom.exe", "ZDOOM.exe", "chocolate-doom.exe", "crispy-doom.exe"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Heretic,
    .folder_names = {"Heretic", "Heretic: Shadow of the Serpent Riders"},
    .marker_any = {"base/HERETIC.WAD", "base/heretic.wad", "HERETIC.WAD", "heretic.wad", "HERETIC.exe", "heretic.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"HERETIC.exe", "heretic.exe", "gzdoom.exe", "GZDoom.exe", "zdoom.exe", "ZDOOM.exe", "chocolate-heretic.exe",
                              "crispy-heretic.exe", "heretic"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Hexen,
    .folder_names = {"Hexen", "Hexen: Beyond Heretic"},
    .marker_any = {"base/HEXEN.WAD", "base/hexen.wad", "HEXEN.WAD", "hexen.wad", "HEXEN.exe", "hexen.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"HEXEN.exe", "hexen.exe", "gzdoom.exe", "GZDoom.exe", "zdoom.exe", "ZDOOM.exe", "chocolate-hexen.exe",
                              "crispy-hexen.exe", "hexen"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Strife,
    .folder_names = {"Strife", "Strife: Veteran Edition"},
    .marker_any = {"base/STRIFE1.WAD", "base/strife1.wad", "STRIFE1.WAD", "strife1.wad", "STRIFE.exe", "strife.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"STRIFE.exe", "strife.exe", "gzdoom.exe", "GZDoom.exe", "zdoom.exe", "ZDOOM.exe", "chocolate-strife.exe",
                              "crispy-strife.exe", "strife"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake2,
    .folder_names = {"Quake II", "Quake II Enhanced"},
    .marker_any = {"baseq2/pak0.pak", "baseq2/PAK0.PAK"},
    .default_dir_candidates = {"baseq2"},
    .executable_candidates = {"quake2.exe", "q2.exe", "quake2", "q2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake2Rerelease,
    .folder_names = {"Quake II", "Quake II Enhanced"},
    .marker_any = {"rerelease/baseq2/pak0.pak", "rerelease/baseq2/PAK0.PAK", "Q2Game.kpf", "q2game.kpf", "rerelease"},
    .default_dir_candidates = {"rerelease/baseq2", "baseq2", "rerelease"},
    .executable_candidates = {"Quake2_x64.exe", "Quake2.exe", "quake2_x64.exe", "quake2ex.exe", "quake2ex_steam.exe",
                              "quake2ex_gog.exe", "rerelease/Quake2_x64.exe", "rerelease/quake2_x64.exe", "rerelease/quake2ex.exe", "Quake2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake2RTX,
    .folder_names = {"Quake II RTX", "Quake 2 RTX", "Q2RTX"},
    .marker_any = {"baseq2/pak0.pak", "baseq2/PAK0.PAK", "q2rtx", "q2rtx/q2rtx.cfg", "q2rtx.exe", "Q2RTX.exe"},
    .default_dir_candidates = {"baseq2", "q2rtx"},
    .executable_candidates = {"q2rtx.exe", "Q2RTX.exe", "q2rtx", "q2rtx.x64"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::SiNGold,
    .folder_names = {"SiN Gold", "SiN Gold (1998)", "SiN"},
    .marker_any = {"sin/pak0.pak", "sin/PAK0.PAK"},
    .default_dir_candidates = {"sin"},
    .executable_candidates = {"sin.exe", "SiN.exe", "sin", "SiN"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::KingpinLifeOfCrime,
    .folder_names = {"Kingpin", "Kingpin - Life of Crime", "Kingpin: Life of Crime"},
    .marker_any = {"main/pak0.pak", "main/PAK0.PAK"},
    .default_dir_candidates = {"main"},
    .executable_candidates = {"kingpin.exe", "Kingpin.exe", "kingpin", "Kingpin"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Daikatana,
    .folder_names = {"Daikatana", "ValveTestApp242980"},
    .marker_any = {"data/pak0.pak", "data/PAK0.PAK", "daikatana.exe", "Daikatana.exe", "ValveTestApp242980"},
    .default_dir_candidates = {"data", "ValveTestApp242980"},
    .executable_candidates = {"daikatana.exe", "Daikatana.exe", "daikatana"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Anachronox,
    .folder_names = {"Anachronox", "ValveTestApp242940"},
    .marker_any = {"data/pak0.pak", "data/PAK0.PAK", "anox.exe", "Anox.exe", "anachronox.exe", "ValveTestApp242940"},
    .default_dir_candidates = {"data", "ValveTestApp242940"},
    .executable_candidates = {"anox.exe", "Anox.exe", "anachronox.exe", "Anachronox.exe", "anox"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Heretic2,
    .folder_names = {"Heretic II", "Heretic 2", "Heretic2"},
    .marker_any = {"base/htic2-0.pak", "base/HTIC2-0.PAK", "base/pak0.pak", "base/PAK0.PAK",
                   "heretic2.exe", "Heretic2.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"heretic2.exe", "Heretic2.exe", "heretic2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::GravityBone,
    .folder_names = {"Gravity Bone", "gravitybone"},
    .marker_any = {"gravitybone.exe", "GravityBone.exe", "gravitybone"},
    .default_dir_candidates = {"ValveTestApp242720", "gravitybone"},
    .executable_candidates = {"gravitybone.exe", "GravityBone.exe", "gravitybone"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::ThirtyFlightsOfLoving,
    .folder_names = {"Thirty Flights of Loving", "thirty_flights_of_loving"},
    .marker_any = {"tfol.exe", "TFOL.exe", "thirty_flights_of_loving", "ValveTestApp214700"},
    .default_dir_candidates = {"ValveTestApp214700", "thirty_flights_of_loving"},
    .executable_candidates = {"tfol.exe", "TFOL.exe", "thirtyflightsofloving.exe", "tfol"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake3Arena,
    .folder_names = {"Quake III Arena", "Quake 3 Arena"},
    .marker_any = {"baseq3/pak0.pk3", "baseq3/PAK0.PK3"},
    .default_dir_candidates = {"baseq3"},
    .executable_candidates = {"quake3.exe", "Quake3.exe", "quake3", "ioquake3.x86_64", "ioquake3"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::QuakeLive,
    .folder_names = {"Quake Live"},
    .marker_any = {"baseq3/pak00.pk3", "baseq3/PAK00.PK3", "baseq3/pak01.pk3", "baseq3/PAK01.PK3"},
    .default_dir_candidates = {"baseq3"},
    .executable_candidates = {"quakelive_steam.exe", "quakelive.exe", "quakelive_steam", "quakelive"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::ReturnToCastleWolfenstein,
    .folder_names = {"Return to Castle Wolfenstein", "Return to Castle Wolfenstein Single Player",
                     "Return to Castle Wolfenstein Multiplayer", "RTCW"},
    .marker_any = {"Main/pak0.pk3", "Main/PAK0.PK3", "main/pak0.pk3", "main/PAK0.PK3"},
    .default_dir_candidates = {"Main", "main"},
    .executable_candidates = {"WolfSP.exe", "wolfsp.exe", "WolfMP.exe", "wolfmp.exe", "iowolfsp.x86_64",
                              "iowolfsp", "iowolfmp.x86_64", "iowolfmp", "WolfSP", "WolfMP"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::WolfensteinEnemyTerritory,
    .folder_names = {"Wolfenstein Enemy Territory", "Wolfenstein: Enemy Territory", "Enemy Territory", "W:ET"},
    .marker_any = {"etmain/pak0.pk3", "etmain/PAK0.PK3"},
    .default_dir_candidates = {"etmain"},
    .executable_candidates = {"ET.exe", "et.exe", "etl.exe", "etlegacy.x86_64", "etl.x86_64", "et.x86_64",
                              "etlegacy", "etl", "et"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::JediOutcast,
    .folder_names = {"STAR WARS Jedi Knight II - Jedi Outcast", "Star Wars Jedi Knight II - Jedi Outcast",
                     "Star Wars Jedi Knight II Jedi Outcast", "Jedi Outcast", "Jedi Knight II"},
    .marker_any = {"GameData/base/assets0.pk3", "GameData/base/Assets0.pk3", "gamedata/base/assets0.pk3", "base/assets0.pk3"},
    .default_dir_candidates = {"GameData/base", "gamedata/base", "base", "GameData", "gamedata"},
    .executable_candidates = {"GameData/JediOutcast.exe", "JediOutcast.exe", "GameData/josp.exe", "josp.exe",
                              "openjo_sp.x86_64", "openjo_sp", "jk2sp.exe", "jk2sp"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::JediAcademy,
    .folder_names = {"STAR WARS Jedi Knight - Jedi Academy", "Star Wars Jedi Knight - Jedi Academy", "Jedi Academy"},
    .marker_any = {"GameData/base/assets0.pk3", "GameData/base/Assets0.pk3", "gamedata/base/assets0.pk3", "base/assets0.pk3"},
    .default_dir_candidates = {"GameData/base", "gamedata/base", "base", "GameData", "gamedata"},
    .executable_candidates = {"GameData/JediAcademy.exe", "JediAcademy.exe", "GameData/jasp.exe", "jasp.exe",
                              "openjk.x86_64", "openjk", "jk3.exe", "jk3"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::StarTrekVoyagerEliteForce,
    .folder_names = {"Star Trek Voyager Elite Force", "Star Trek: Voyager - Elite Force", "Elite Force"},
    .marker_any = {"baseEF/pak0.pk3", "baseEF/PAK0.PK3", "baseef/pak0.pk3", "baseef/PAK0.PK3"},
    .default_dir_candidates = {"baseEF", "baseef"},
    .executable_candidates = {"stvoy.exe", "STVoy.exe", "holomatch.exe", "stvoy", "holomatch"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::EliteForce2,
    .folder_names = {"Star Trek: Elite Force II", "Star Trek Elite Force II", "Elite Force II", "EliteForce2"},
    .marker_any = {"base/pak0.pk3", "base/PAK0.PK3", "EF2.exe", "ef2.exe", "EliteForce2.exe", "eliteforce2.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"EF2.exe", "ef2.exe", "EliteForce2.exe", "eliteforce2.exe", "ef2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Warsow,
    .folder_names = {"Warsow"},
    .marker_any = {"basewsw", "basewsw/data0_00.pk3", "basewsw/pak0.pk3", "warsow.exe", "Warsow.exe",
                   "warsow.x86_64"},
    .default_dir_candidates = {"basewsw"},
    .executable_candidates = {"warsow.exe", "Warsow.exe", "warsow.x86_64", "warsow"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Warfork,
    .folder_names = {"Warfork"},
    .marker_any = {"basewsw", "basewsw/data0_00.pk3", "basewsw/pak0.pk3", "warfork.exe", "Warfork.exe",
                   "warfork.x86_64"},
    .default_dir_candidates = {"basewsw"},
    .executable_candidates = {"warfork.exe", "Warfork.exe", "warfork.x86_64", "warfork"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::WorldOfPadman,
    .folder_names = {"World of Padman", "WorldOfPadman", "WoP"},
    .marker_any = {"wop", "wop/wop_00.pk3", "wop/WOP_00.PK3", "wop/wop_01.pk3", "wop/pak0.pk3",
                   "wop.exe", "WoP.exe", "worldofpadman.exe", "WorldOfPadman.exe"},
    .default_dir_candidates = {"wop"},
    .executable_candidates = {"wop.exe", "WoP.exe", "worldofpadman.exe", "WorldOfPadman.exe", "wop"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::HeavyMetalFakk2,
    .folder_names = {"Heavy Metal F.A.K.K.2", "Heavy Metal FAKK2", "FAKK2", "HeavyMetalFakk2"},
    .marker_any = {"fakk", "fakk/pak0.pak", "fakk/PAK0.PAK", "fakk/pak0.pk3", "fakk/PAK0.PK3",
                   "fakk2.exe", "Fakk2.exe"},
    .default_dir_candidates = {"fakk"},
    .executable_candidates = {"fakk2.exe", "Fakk2.exe", "heavymetalfakk2.exe", "fakk2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::AmericanMcGeesAlice,
    .folder_names = {"American McGee's Alice", "American McGees Alice", "Alice"},
    .marker_any = {"base/pak0.pk3", "base/PAK0.PK3", "base/pak1_large.pk3", "base/pak1_small.pk3",
                   "base/pak2.pk3", "base/Pak2.pk3", "alice.exe", "Alice.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"alice.exe", "Alice.exe", "AMA-Win10Fix.exe"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake4,
    .folder_names = {"Quake 4", "Quake4"},
    .marker_any = {"q4base/pak001.pk4", "q4base/PAK001.PK4", "q4base/pak000.pk4", "q4base/pak00.pk4"},
    .default_dir_candidates = {"q4base"},
    .executable_candidates = {"Quake4.exe", "quake4.exe", "quake4", "Quake4"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Doom3,
    .folder_names = {"DOOM 3", "Doom 3", "DOOM3", "Doom3"},
    .marker_any = {"base/pak000.pk4", "base/PAK000.PK4", "base/pak001.pk4", "base/PAK001.PK4",
                   "Doom3.exe", "doom3.exe", "DOOM3.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"Doom3.exe", "doom3.exe", "DOOM3.exe", "dhewm3.exe", "dhewm3", "doom3"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Doom3BFGEdition,
    .folder_names = {"DOOM 3 BFG Edition", "Doom 3 BFG Edition"},
    .marker_any = {"base/pak000.pk4", "base/PAK000.PK4", "base/pak001.pk4", "base/PAK001.PK4",
                   "DOOM3BFG.exe", "doom3bfg.exe", "BFGFramework.dll", "bfgframework.dll"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"DOOM3BFG.exe", "doom3bfg.exe"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Prey,
    .folder_names = {"Prey", "Prey (2006)"},
    .marker_any = {"base/pak000.pk4", "base/PAK000.PK4", "base/pak001.pk4", "base/PAK001.PK4", "Prey.exe", "prey.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"Prey.exe", "prey.exe", "prey.x86_64", "prey.x86", "prey"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::EnemyTerritoryQuakeWars,
    .folder_names = {"Enemy Territory: Quake Wars", "Enemy Territory Quake Wars", "Enemy Territory - QUAKE Wars", "ETQW"},
    .marker_any = {"base/pak002.pk4", "base/PAK002.PK4", "base/pak001.pk4", "base/PAK001.PK4", "ETQW.exe", "etqw.exe"},
    .default_dir_candidates = {"base"},
    .executable_candidates = {"ETQW.exe", "etqw.exe", "etqw.x86_64", "etqw.x86", "etqw", "etqw-dedicated.exe", "etqw-dedicated"},
  });

  return out;
}
}  // namespace

GameAutoDetectResult auto_detect_supported_games() {
  GameAutoDetectResult out;

  const QStringList steam_dirs = steam_common_dirs();
  const QStringList gog_reg = gog_registry_roots();
  const QStringList gog_bases = gog_base_dirs();
  const QStringList eos_manifest_dirs = epic_manifest_dirs();
  const QStringList eos_roots = epic_install_roots(eos_manifest_dirs);

  for (const GameSupportInfo& support : supported_game_support()) {
    const auto try_roots = [&](const QStringList& roots, const QString& source, bool require_folder_name_hint) -> bool {
      for (const QString& root : roots) {
        const GameRootMatch match = match_root_for_support(root, support, require_folder_name_hint);
        if (match.score < 0) {
          continue;
        }

        DetectedGameInstall install;
        install.game = support.game;
        install.root_dir = root;
        install.default_dir = choose_default_dir(root, support.default_dir_candidates);
        install.launch.executable_path = match.executable_path;
        install.launch.working_dir = root;
        out.installs.push_back(install);
        out.log.push_back(QString("Detected %1 (%2): %3").arg(game_display_name(support.game), source, root));
        return true;
      }
      return false;
    };

    const QStringList steam_roots = roots_from_named_folders(steam_dirs, support.folder_names);
    if (try_roots(steam_roots, "Steam", false)) {
      continue;
    }

    QStringList gog_roots = gog_reg;
    gog_roots.append(roots_from_named_folders(gog_bases, support.folder_names));
    gog_roots = dedupe_existing_dirs(gog_roots);
    if (try_roots(gog_roots, "GOG.com", true)) {
      continue;
    }

    if (try_roots(eos_roots, "EOS", true)) {
      continue;
    }

    out.log.push_back(QString("Not found: %1").arg(game_display_name(support.game)));
  }

  if (out.installs.isEmpty()) {
    QStringList prefix;
    if (steam_dirs.isEmpty()) {
      prefix.push_back("Steam library not found (or no Steam games installed).");
    }
    if (gog_reg.isEmpty() && gog_bases.isEmpty()) {
      prefix.push_back("GOG.com installs not found.");
    }
    if (eos_roots.isEmpty()) {
      prefix.push_back("EOS installs not found.");
    }
    if (!prefix.isEmpty()) {
      prefix.append(out.log);
      out.log = prefix;
    }
  }

  return out;
}

std::optional<GameId> detect_game_id_for_path(const QString& file_or_dir_path) {
  const QString cleaned = clean_path(file_or_dir_path);
  if (cleaned.isEmpty()) {
    return std::nullopt;
  }

  QFileInfo info(cleaned);
  QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
  dir = clean_path(dir);
  if (dir.isEmpty()) {
    return std::nullopt;
  }

  // Prefer rereleases when multiple markers exist in the same install folder.
  const QVector<GameSupportInfo> support = supported_game_support();
  QVector<GameSupportInfo> priority;
  priority.reserve(support.size());

  const auto push_if = [&](GameId id) {
    for (const GameSupportInfo& s : support) {
      if (s.game == id) {
        priority.push_back(s);
        return;
      }
    }
  };

  push_if(GameId::QuakeRerelease);
  push_if(GameId::Quake);
  push_if(GameId::HalfLife);
  push_if(GameId::Doom);
  push_if(GameId::Doom2);
  push_if(GameId::FinalDoom);
  push_if(GameId::Heretic);
  push_if(GameId::Hexen);
  push_if(GameId::Strife);
  push_if(GameId::Quake2Rerelease);
  push_if(GameId::Quake2RTX);
  push_if(GameId::Quake2);
  push_if(GameId::SiNGold);
  push_if(GameId::KingpinLifeOfCrime);
  push_if(GameId::Daikatana);
  push_if(GameId::Anachronox);
  push_if(GameId::Heretic2);
  push_if(GameId::GravityBone);
  push_if(GameId::ThirtyFlightsOfLoving);
  push_if(GameId::QuakeLive);
  push_if(GameId::Quake3Arena);
  push_if(GameId::ReturnToCastleWolfenstein);
  push_if(GameId::WolfensteinEnemyTerritory);
  push_if(GameId::JediOutcast);
  push_if(GameId::JediAcademy);
  push_if(GameId::StarTrekVoyagerEliteForce);
  push_if(GameId::EliteForce2);
  push_if(GameId::Warsow);
  push_if(GameId::Warfork);
  push_if(GameId::WorldOfPadman);
  push_if(GameId::HeavyMetalFakk2);
  push_if(GameId::AmericanMcGeesAlice);
  push_if(GameId::Quake4);
  push_if(GameId::Doom3BFGEdition);
  push_if(GameId::Doom3);
  push_if(GameId::Prey);
  push_if(GameId::EnemyTerritoryQuakeWars);

  auto match_dir = [&](const QString& root) -> std::optional<GameId> {
    int best_score = -1;
    std::optional<GameId> best_game = std::nullopt;

    for (const GameSupportInfo& s : priority) {
      const GameRootMatch match = match_root_for_support(root, s, false);
      if (match.score > best_score) {
        best_score = match.score;
        best_game = s.game;
      }
    }

    return best_game;
  };

  QString cur = dir;
  for (int depth = 0; depth < 10; ++depth) {
    if (cur.isEmpty()) {
      break;
    }
    if (const auto id = match_dir(cur)) {
      return id;
    }

    const QDir d(cur);
    const QString parent = clean_path(d.absoluteFilePath(".."));
    if (parent.isEmpty() || paths_equal(parent, cur)) {
      break;
    }
    cur = parent;
  }

  return std::nullopt;
}
