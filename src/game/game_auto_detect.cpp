#include "game/game_auto_detect.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace {
QString clean_path(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
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
    if (!out.contains(cleaned)) {
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
  QStringList steam_folder_names;
  QStringList marker_any;
  QStringList default_dir_candidates;
  QStringList executable_candidates;
};

QVector<GameSupportInfo> supported_game_support() {
  QVector<GameSupportInfo> out;

  out.push_back(GameSupportInfo{
    .game = GameId::Quake,
    .steam_folder_names = {"Quake"},
    .marker_any = {"id1/pak0.pak", "id1/PAK0.PAK"},
    .default_dir_candidates = {"id1"},
    .executable_candidates = {"quake.exe", "glquake.exe", "winquake.exe", "quake", "glquake"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::QuakeRerelease,
    .steam_folder_names = {"Quake"},
    .marker_any = {"rerelease/id1/pak0.pak", "rerelease/id1/PAK0.PAK", "rerelease"},
    .default_dir_candidates = {"rerelease/id1", "rerelease"},
    .executable_candidates = {"Quake_x64.exe", "Quake.exe", "quake_x64.exe", "rerelease/Quake_x64.exe", "Quake"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake2,
    .steam_folder_names = {"Quake II"},
    .marker_any = {"baseq2/pak0.pak", "baseq2/PAK0.PAK"},
    .default_dir_candidates = {"baseq2"},
    .executable_candidates = {"quake2.exe", "q2.exe", "quake2", "q2"},
  });

  out.push_back(GameSupportInfo{
    .game = GameId::Quake2Rerelease,
    .steam_folder_names = {"Quake II"},
    .marker_any = {"rerelease/baseq2/pak0.pak", "rerelease/baseq2/PAK0.PAK", "rerelease"},
    .default_dir_candidates = {"rerelease/baseq2", "baseq2", "rerelease"},
    .executable_candidates = {"Quake2_x64.exe", "Quake2.exe", "quake2_x64.exe", "rerelease/Quake2_x64.exe", "Quake2"},
  });

  return out;
}

QStringList candidate_roots_for_support(const GameSupportInfo& support, const QStringList& common_dirs) {
  QStringList out;
  for (const QString& common : common_dirs) {
    const QDir base(common);
    for (const QString& folder : support.steam_folder_names) {
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
}  // namespace

GameAutoDetectResult auto_detect_supported_games() {
  GameAutoDetectResult out;

  const QStringList common_dirs = steam_common_dirs();
  if (common_dirs.isEmpty()) {
    out.log.push_back("Steam library not found (or no Steam games installed).");
  }

  for (const GameSupportInfo& support : supported_game_support()) {
    const QStringList roots = candidate_roots_for_support(support, common_dirs);
    if (roots.isEmpty()) {
      out.log.push_back(QString("Not found: %1").arg(game_display_name(support.game)));
      continue;
    }

    bool found_for_game = false;
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
      out.log.push_back(QString("Detected %1: %2").arg(game_display_name(support.game), root));
      found_for_game = true;
      break;
    }

    if (!found_for_game) {
      out.log.push_back(QString("Not found: %1").arg(game_display_name(support.game)));
    }
  }

  return out;
}
