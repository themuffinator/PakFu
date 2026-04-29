#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

struct CliOptions {
  bool list = false;
  bool info = false;
  bool extract = false;
  bool save_as = false;
  bool list_plugins = false;
  bool validate = false;
  bool platform_report = false;
  bool plugin_report = false;
  bool quakelive_encrypt_pk3 = false;
  bool check_updates = false;
  bool qa_practical = false;
  bool list_game_sets = false;
  bool auto_detect_game_sets = false;
  QString select_game_set;
  QString output_dir;
  QString save_as_path;
  QString save_format;
  QString compare_path;
  QString convert_format;
  QString asset_graph_format;
  QString package_manifest_format;
  QString preview_export_entry;
  QString run_plugin;
  QString mount_entry;
  QStringList entry_filters;
  QStringList prefix_filters;
  QStringList plugin_dirs;
  QString pak_path;
  QString update_repo;
  QString update_channel;
};

enum class CliParseResult {
  Ok,
  ExitOk,
  ExitError,
};

bool wants_cli(int argc, char** argv);
bool cli_requires_gui(int argc, char** argv);
CliParseResult parse_cli(QCoreApplication& app, CliOptions& options, QString* output);
int run_cli(const CliOptions& options);
