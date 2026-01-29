#include <QApplication>
#include <QCoreApplication>
#include <QTextStream>

#include "cli/cli.h"
#include "ui/main_window.h"

namespace {
void set_app_metadata(QCoreApplication& app) {
  app.setApplicationName("PakFu");
  app.setApplicationVersion("0.1.0");
}
}  // namespace

int main(int argc, char** argv) {
  if (wants_cli(argc, argv)) {
    QCoreApplication app(argc, argv);
    set_app_metadata(app);

    CliOptions options;
    QString output;
    const CliParseResult result = parse_cli(app, options, &output);
    if (result == CliParseResult::ExitOk) {
      if (!output.isEmpty()) {
        QTextStream(stdout) << output;
      }
      return 0;
    }
    if (result == CliParseResult::ExitError) {
      if (!output.isEmpty()) {
        QTextStream(stderr) << output;
      }
      return 1;
    }

    return run_cli(options);
  }

  QApplication app(argc, argv);
  set_app_metadata(app);

  MainWindow window;
  window.show();
  return app.exec();
}
