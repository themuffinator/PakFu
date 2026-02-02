#include "formats/cinematic.h"

#include <QFileInfo>

#include "formats/cin_cinematic.h"
#include "formats/roq_cinematic.h"

namespace {
QString file_ext_lower(const QString& name) {
  const QString lower = name.toLower();
  const int dot = lower.lastIndexOf('.');
  return dot >= 0 ? lower.mid(dot + 1) : QString();
}
}  // namespace

std::unique_ptr<CinematicDecoder> open_cinematic_file(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  const QFileInfo info(file_path);
  const QString ext = file_ext_lower(info.fileName());

  std::unique_ptr<CinematicDecoder> dec;
  if (ext == "cin") {
    dec = std::make_unique<CinCinematicDecoder>();
  } else if (ext == "roq") {
    dec = std::make_unique<RoqCinematicDecoder>();
  } else {
    if (error) {
      *error = "Unsupported cinematic format.";
    }
    return nullptr;
  }

  QString open_err;
  if (!dec->open_file(file_path, &open_err)) {
    if (error) {
      *error = open_err.isEmpty() ? "Unable to open cinematic file." : open_err;
    }
    return nullptr;
  }

  return dec;
}

