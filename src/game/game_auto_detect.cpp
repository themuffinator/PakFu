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
    .game = GameId::Quake3Arena,
    .folder_names = {"Quake III Arena"},
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
    .game = GameId::Quake4,
    .folder_names = {"Quake 4", "Quake4"},
    .marker_any = {"q4base/pak001.pk4", "q4base/PAK001.PK4", "q4base/pak000.pk4", "q4base/pak00.pk4"},
    .default_dir_candidates = {"q4base"},
    .executable_candidates = {"Quake4.exe", "quake4.exe", "quake4", "Quake4"},
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
    const auto try_roots = [&](const QStringList& roots, const QString& source) -> bool {
      for (const QString& root : roots) {
        if (!any_marker_exists(root, support.marker_any)) {
          continue;
        }

        DetectedGameInstall install;
        install.game = support.game;
        install.root_dir = root;
        install.default_dir = choose_default_dir(root, support.default_dir_candidates);
        install.launch.executable_path = first_existing_file(root, support.executable_candidates);
        install.launch.working_dir = root;
        out.installs.push_back(install);
        out.log.push_back(QString("Detected %1 (%2): %3").arg(game_display_name(support.game), source, root));
        return true;
      }
      return false;
    };

    const QStringList steam_roots = roots_from_named_folders(steam_dirs, support.folder_names);
    if (try_roots(steam_roots, "Steam")) {
      continue;
    }

    QStringList gog_roots = gog_reg;
    gog_roots.append(roots_from_named_folders(gog_bases, support.folder_names));
    gog_roots = dedupe_existing_dirs(gog_roots);
    if (try_roots(gog_roots, "GOG.com")) {
      continue;
    }

    if (try_roots(eos_roots, "EOS")) {
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
