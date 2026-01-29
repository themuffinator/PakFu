#pragma once

#include <QString>

class QCoreApplication;

struct CliOptions {
  bool list = false;
  bool info = false;
  bool extract = false;
  QString output_dir;
  QString pak_path;
};

enum class CliParseResult {
  Ok,
  ExitOk,
  ExitError,
};

bool wants_cli(int argc, char** argv);
CliParseResult parse_cli(QCoreApplication& app, CliOptions& options, QString* output);
int run_cli(const CliOptions& options);
