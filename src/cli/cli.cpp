#include "cli.h"

#include <algorithm>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QUuid>

#include "archive/archive.h"
#include "archive/path_safety.h"
#include "game/game_auto_detect.h"
#include "game/game_set.h"
#include "pakfu_config.h"
#include "update/update_service.h"

namespace {
QString normalize_output(const QString& text) {
  return text.endsWith('\n') ? text : text + '\n';
}

QString describe_game_set_line(const GameSet& set, bool selected) {
  QString line;
  line += selected ? "* " : "  ";
  line += set.uid.isEmpty() ? "(missing-uid)" : set.uid;
  line += "  ";
  line += set.name.isEmpty() ? game_display_name(set.game) : set.name;
  line += "  [" + game_id_key(set.game) + "]";
  if (!set.default_dir.isEmpty()) {
    line += "  default=" + QFileInfo(set.default_dir).absoluteFilePath();
  }
  if (!set.root_dir.isEmpty()) {
    line += "  root=" + QFileInfo(set.root_dir).absoluteFilePath();
  }
  return line;
}

int apply_auto_detect_to_state(GameSetState& state, QStringList* log) {
  const GameAutoDetectResult detected = auto_detect_supported_games();
  if (log) {
    *log = detected.log;
  }

  int changes = 0;
  for (const DetectedGameInstall& install : detected.installs) {
    GameSet* existing = nullptr;
    for (GameSet& set : state.sets) {
      if (set.game == install.game) {
        existing = &set;
        break;
      }
    }

    if (existing) {
      existing->root_dir = install.root_dir;
      existing->default_dir = install.default_dir;
      if (!install.launch.executable_path.isEmpty()) {
        existing->launch.executable_path = install.launch.executable_path;
      }
      if (!install.launch.working_dir.isEmpty()) {
        existing->launch.working_dir = install.launch.working_dir;
      }
      if (existing->palette_id.isEmpty()) {
        existing->palette_id = default_palette_for_game(existing->game);
      }
      if (existing->name.isEmpty()) {
        existing->name = game_display_name(existing->game);
      }
      ++changes;
      continue;
    }

    GameSet set;
    set.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    set.game = install.game;
    set.name = game_display_name(set.game);
    set.root_dir = install.root_dir;
    set.default_dir = install.default_dir;
    set.palette_id = default_palette_for_game(set.game);
    set.launch = install.launch;
    state.sets.push_back(set);
    ++changes;
  }

  if (state.selected_uid.isEmpty() && !state.sets.isEmpty()) {
    state.selected_uid = state.sets.first().uid;
  }

  return changes;
}

const GameSet* find_game_set_by_selector(const GameSetState& state, const QString& selector, QString* error) {
  if (error) {
    error->clear();
  }
  const QString s = selector.trimmed();
  if (s.isEmpty()) {
    if (error) {
      *error = "Empty game set selector.";
    }
    return nullptr;
  }

  if (const GameSet* by_uid = find_game_set(state, s)) {
    return by_uid;
  }

  QVector<const GameSet*> matches;
  matches.reserve(state.sets.size());
  for (const GameSet& set : state.sets) {
    const QString key = game_id_key(set.game);
    const QString display = game_display_name(set.game);
    const QString name = set.name;
    if (key.compare(s, Qt::CaseInsensitive) == 0 ||
        display.compare(s, Qt::CaseInsensitive) == 0 ||
        name.compare(s, Qt::CaseInsensitive) == 0) {
      matches.push_back(&set);
    }
  }

  if (matches.isEmpty()) {
    if (error) {
      *error = "Game set not found: " + s;
    }
    return nullptr;
  }
  if (matches.size() > 1) {
    if (error) {
      *error = "Game set selector is ambiguous: " + s;
    }
    return nullptr;
  }
  return matches.first();
}
}  // namespace

bool wants_cli(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == "--cli" || arg == "--list" || arg == "--info" || arg == "--extract" ||
        arg == "--check-updates" || arg == "--update-repo" || arg == "--update-channel" ||
        arg == "--list-game-sets" || arg == "--auto-detect-game-sets" || arg == "--select-game-set" ||
        arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v") {
      return true;
    }
  }
  return false;
}

CliParseResult parse_cli(QCoreApplication& app, CliOptions& options, QString* output) {
  QCommandLineParser parser;
  parser.setApplicationDescription("PakFu command-line interface");
  parser.addHelpOption();
  parser.addVersionOption();

  const QCommandLineOption cli_option("cli", "Run in CLI mode (no UI).");
  const QCommandLineOption list_option({"l", "list"}, "List entries in the archive.");
  const QCommandLineOption info_option({"i", "info"}, "Show archive summary information.");
  const QCommandLineOption extract_option({"x", "extract"}, "Extract archive contents.");
  const QCommandLineOption check_updates_option("check-updates", "Check GitHub for new releases.");
  const QCommandLineOption list_game_sets_option("list-game-sets", "List configured Game Sets.");
  const QCommandLineOption auto_detect_game_sets_option(
    "auto-detect-game-sets",
    "Auto-detect supported games (Steam → GOG.com → EOS) and create/update Game Sets.");
  const QCommandLineOption select_game_set_option(
    "select-game-set",
    "Select the active Game Set (by UID, game key, or name).",
    "selector");
  const QCommandLineOption update_repo_option(
    "update-repo",
    "Override the GitHub repo used for update checks (owner/name).",
    "repo");
  const QCommandLineOption update_channel_option(
    "update-channel",
    "Override the update channel (stable, beta, dev).",
    "channel");
  const QCommandLineOption output_option(
    {"o", "output"},
    "Output directory for extraction.",
    "dir");

  parser.addOption(cli_option);
  parser.addOption(list_option);
  parser.addOption(info_option);
  parser.addOption(extract_option);
  parser.addOption(check_updates_option);
  parser.addOption(list_game_sets_option);
  parser.addOption(auto_detect_game_sets_option);
  parser.addOption(select_game_set_option);
  parser.addOption(update_repo_option);
  parser.addOption(update_channel_option);
  parser.addOption(output_option);
  parser.addPositionalArgument("archive", "Path to an archive (PAK/PK3/PK4/PKZ/ZIP).");

  if (!parser.parse(app.arguments())) {
    if (output) {
      *output = normalize_output(parser.errorText()) + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet("help")) {
    if (output) {
      *output = parser.helpText();
    }
    return CliParseResult::ExitOk;
  }

  if (parser.isSet("version")) {
    if (output) {
      *output = normalize_output(app.applicationName() + ' ' + app.applicationVersion());
    }
    return CliParseResult::ExitOk;
  }

  options.list = parser.isSet(list_option);
  options.info = parser.isSet(info_option);
  options.extract = parser.isSet(extract_option);
  options.check_updates = parser.isSet(check_updates_option);
  options.list_game_sets = parser.isSet(list_game_sets_option);
  options.auto_detect_game_sets = parser.isSet(auto_detect_game_sets_option);
  options.select_game_set = parser.value(select_game_set_option);
  options.output_dir = parser.value(output_option);
  options.update_repo = parser.value(update_repo_option);
  options.update_channel = parser.value(update_channel_option);

  const QStringList positional = parser.positionalArguments();
  if (!positional.isEmpty()) {
    options.pak_path = positional.first();
  }

  const bool any_action = options.list || options.info || options.extract || options.check_updates ||
                          options.list_game_sets || options.auto_detect_game_sets ||
                          !options.select_game_set.isEmpty();
  if (!any_action && options.pak_path.isEmpty()) {
    if (output) {
      *output = parser.helpText();
    }
    return CliParseResult::ExitOk;
  }

  if (!any_action && !options.pak_path.isEmpty()) {
    options.info = true;
  }

  if ((options.list || options.info || options.extract) && options.pak_path.isEmpty()) {
    if (output) {
      *output = normalize_output("Missing archive path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  return CliParseResult::Ok;
}

int run_cli(const CliOptions& options) {
  QTextStream out(stdout);
  QTextStream err(stderr);

  if (options.list_game_sets || options.auto_detect_game_sets || !options.select_game_set.isEmpty()) {
    QString load_err;
    GameSetState state = load_game_set_state(&load_err);
    if (!load_err.isEmpty()) {
      err << load_err << "\n";
      return 2;
    }

    if (options.auto_detect_game_sets) {
      QStringList log;
      const int changes = apply_auto_detect_to_state(state, &log);
      QString save_err;
      if (!save_game_set_state(state, &save_err)) {
        err << (save_err.isEmpty() ? "Failed to save game sets.\n" : save_err + "\n");
        return 2;
      }
      out << "Auto-detect: " << changes << " change(s)\n";
      if (!log.isEmpty()) {
        for (const QString& line : log) {
          out << line << "\n";
        }
      }
    }

    if (!options.select_game_set.isEmpty()) {
      QString sel_err;
      const GameSet* selected = find_game_set_by_selector(state, options.select_game_set, &sel_err);
      if (!selected) {
        err << (sel_err.isEmpty() ? "Game set not found.\n" : sel_err + "\n");
        return 2;
      }
      state.selected_uid = selected->uid;
      QString save_err;
      if (!save_game_set_state(state, &save_err)) {
        err << (save_err.isEmpty() ? "Failed to save game sets.\n" : save_err + "\n");
        return 2;
      }
      out << "Selected Game Set:\n";
      out << describe_game_set_line(*selected, true) << "\n";
    }

    if (options.list_game_sets) {
      if (state.sets.isEmpty()) {
        out << "No Game Sets configured.\n";
        return 0;
      }
      for (const GameSet& set : state.sets) {
        out << describe_game_set_line(set, set.uid == state.selected_uid) << "\n";
      }
    }

    if (options.list_game_sets || options.auto_detect_game_sets || !options.select_game_set.isEmpty()) {
      return 0;
    }
  }

  if (options.check_updates) {
    UpdateService updater;
    const QString repo = options.update_repo.isEmpty() ? PAKFU_GITHUB_REPO : options.update_repo;
    const QString channel = options.update_channel.isEmpty() ? PAKFU_UPDATE_CHANNEL : options.update_channel;
    updater.configure(repo, channel, PAKFU_VERSION);
    const UpdateCheckResult result = updater.check_for_updates_sync();
    switch (result.state) {
      case UpdateCheckState::UpdateAvailable:
        out << "Update available: " << result.info.version << "\n";
        if (!result.info.asset_name.isEmpty()) {
          out << "Asset: " << result.info.asset_name << "\n";
        }
        if (result.info.html_url.isValid()) {
          out << "Release: " << result.info.html_url.toString() << "\n";
        }
        return 0;
      case UpdateCheckState::UpToDate:
        out << "PakFu is up to date.\n";
        return 0;
      case UpdateCheckState::NoRelease:
        err << "No releases found.\n";
        return 2;
      case UpdateCheckState::NotConfigured:
        err << "Update repo not configured.\n";
        return 2;
      case UpdateCheckState::Error:
        err << (result.message.isEmpty() ? "Update check failed.\n" : result.message + '\n');
        return 2;
    }
  }

  if (options.pak_path.isEmpty()) {
    err << "No archive path provided.\n";
    return 2;
  }

  QFileInfo archive_info(options.pak_path);
  if (!archive_info.exists()) {
    err << "Archive not found: " << options.pak_path << "\n";
    return 2;
  }

  Archive archive;
  QString load_err;
  if (!archive.load(archive_info.absoluteFilePath(), &load_err)) {
    err << (load_err.isEmpty() ? "Unable to load archive.\n" : load_err + "\n");
    return 2;
  }

  auto format_string = [](Archive::Format f) -> QString {
    switch (f) {
      case Archive::Format::Pak:
        return "PAK";
      case Archive::Format::Zip:
        return "ZIP";
      case Archive::Format::Unknown:
        break;
    }
    return "Unknown";
  };

  const QVector<ArchiveEntry>& entries = archive.entries();

  if (options.info) {
    out << "Archive: " << QFileInfo(archive.path()).absoluteFilePath() << "\n";
    if (archive.readable_path() != archive.path()) {
      out << "Readable: " << QFileInfo(archive.readable_path()).absoluteFilePath() << "\n";
    }
    out << "Format: " << format_string(archive.format()) << "\n";
    if (archive.is_quakelive_encrypted_pk3()) {
      out << "Quake Live encrypted PK3: yes\n";
    }
    out << "Entries: " << entries.size() << "\n";

    quint64 total = 0;
    for (const ArchiveEntry& e : entries) {
      if (!e.name.endsWith('/')) {
        total += static_cast<quint64>(e.size);
      }
    }
    out << "Total uncompressed: " << total << " bytes\n";
  }

  if (options.list) {
    QVector<ArchiveEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const ArchiveEntry& a, const ArchiveEntry& b) {
      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const ArchiveEntry& e : sorted) {
      out << e.size << "\t" << e.name << "\n";
    }
  }

  if (options.extract) {
    QString out_dir = options.output_dir.trimmed();
    if (out_dir.isEmpty()) {
      const QString base = archive_info.completeBaseName().isEmpty() ? "archive" : archive_info.completeBaseName();
      out_dir = QDir::current().filePath(base + "_extract");
    }
    QDir od(out_dir);
    if (!od.exists() && !od.mkpath(".")) {
      err << "Unable to create output directory: " << QFileInfo(out_dir).absoluteFilePath() << "\n";
      return 2;
    }

    int ok = 0;
    int failed = 0;
    int skipped = 0;

    for (const ArchiveEntry& e : entries) {
      const QString name = normalize_archive_entry_name(e.name);
      if (!is_safe_archive_entry_name(name)) {
        ++skipped;
        err << "Skipping unsafe entry: " << e.name << "\n";
        continue;
      }

      const QString dest = od.filePath(name);
      if (name.endsWith('/')) {
        QDir d(dest);
        if (!d.exists() && !d.mkpath(".")) {
          ++failed;
          err << "Unable to create directory: " << dest << "\n";
        }
        continue;
      }

      QString ex_err;
      if (!archive.extract_entry_to_file(name, dest, &ex_err)) {
        ++failed;
        err << (ex_err.isEmpty() ? "Extract failed: " + name + "\n" : ex_err + "\n");
        continue;
      }
      ++ok;
    }

    out << "Extracted: " << ok << " file(s)\n";
    if (skipped > 0) {
      out << "Skipped: " << skipped << " unsafe entr" << (skipped == 1 ? "y" : "ies") << "\n";
    }
    if (failed > 0) {
      err << "Failed: " << failed << " item(s)\n";
      return 2;
    }
  }

  return 0;
}
