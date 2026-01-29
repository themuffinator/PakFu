#pragma once

#include <QString>

class QCoreApplication;

struct CliOptions {
  bool list = false;
  bool info = false;
  bool extract = false;
  bool check_updates = false;
  QString output_dir;
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
CliParseResult parse_cli(QCoreApplication& app, CliOptions& options, QString* output);
int run_cli(const CliOptions& options);
