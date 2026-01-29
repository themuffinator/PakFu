#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

#include "pakfu_config.h"
#include "update/update_service.h"

namespace {
QString normalize_output(const QString& text) {
  return text.endsWith('\n') ? text : text + '\n';
}
}  // namespace

bool wants_cli(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == "--cli" || arg == "--list" || arg == "--info" || arg == "--extract" ||
        arg == "--check-updates" || arg == "--update-repo" || arg == "--update-channel" ||
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
  const QCommandLineOption list_option({"l", "list"}, "List entries in the PAK.");
  const QCommandLineOption info_option({"i", "info"}, "Show archive summary information.");
  const QCommandLineOption extract_option({"x", "extract"}, "Extract archive contents.");
  const QCommandLineOption check_updates_option("check-updates", "Check GitHub for new releases.");
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
  parser.addOption(update_repo_option);
  parser.addOption(update_channel_option);
  parser.addOption(output_option);
  parser.addPositionalArgument("pak", "Path to a PAK file.");

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
  options.output_dir = parser.value(output_option);
  options.update_repo = parser.value(update_repo_option);
  options.update_channel = parser.value(update_channel_option);

  const QStringList positional = parser.positionalArguments();
  if (!positional.isEmpty()) {
    options.pak_path = positional.first();
  }

  const bool any_action = options.list || options.info || options.extract || options.check_updates;
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
      *output = normalize_output("Missing PAK path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  return CliParseResult::Ok;
}

int run_cli(const CliOptions& options) {
  QTextStream out(stdout);
  QTextStream err(stderr);

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
    err << "No PAK path provided.\n";
    return 2;
  }

  QFileInfo pak_info(options.pak_path);
  if (!pak_info.exists()) {
    err << "PAK not found: " << options.pak_path << "\n";
    return 2;
  }

  out << "PakFu CLI scaffold\n";
  out << "PAK: " << pak_info.absoluteFilePath() << "\n";
  if (options.list) {
    out << "Action: list (not implemented)\n";
  }
  if (options.info) {
    out << "Action: info (not implemented)\n";
  }
  if (options.extract) {
    out << "Action: extract (not implemented)\n";
    if (!options.output_dir.isEmpty()) {
      out << "Output directory: " << options.output_dir << "\n";
    }
  }

  return 0;
}
