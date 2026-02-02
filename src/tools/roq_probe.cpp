#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#include "formats/cinematic.h"

namespace {
QByteArray hash_image(const QImage& img) {
  if (img.isNull()) {
    return {};
  }
  QCryptographicHash h(QCryptographicHash::Sha256);
  h.addData(reinterpret_cast<const char*>(img.constBits()), img.sizeInBytes());
  return h.result().toHex();
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  QTextStream err(stderr);

  const QStringList args = app.arguments();
  if (args.size() < 2) {
    err << "Usage: roq_probe <file.roq>\n";
    return 2;
  }

  const QString file_path = QFileInfo(args[1]).absoluteFilePath();
  QString open_err;
  std::unique_ptr<CinematicDecoder> dec = open_cinematic_file(file_path, &open_err);
  if (!dec) {
    err << (open_err.isEmpty() ? "Unable to open cinematic.\n" : (open_err + "\n"));
    return 2;
  }

  const CinematicInfo info = dec->info();
  out << "Format: " << info.format << "\n";
  out << "Size: " << info.width << "x" << info.height << "\n";
  out << "FPS: " << info.fps << "\n";
  out << "Frames: " << info.frame_count << "\n";
  out << "Audio: " << (info.has_audio ? "yes" : "no") << "\n";
  if (info.has_audio) {
    out << "Audio: " << info.audio_sample_rate << " Hz, ch=" << info.audio_channels
        << ", bytes/sample=" << info.audio_bytes_per_sample << "\n";
  }

  CinematicFrame frame;
  QString dec_err;
  int decoded = 0;

  // Decode first frame via decode_frame(0) to match UI behavior.
  if (!dec->decode_frame(0, &frame, &dec_err) || frame.image.isNull()) {
    err << (dec_err.isEmpty() ? "decode_frame(0) failed.\n" : (dec_err + "\n"));
    return 2;
  }
  out << "Frame 0: img=" << frame.image.width() << "x" << frame.image.height() << " hash=" << hash_image(frame.image)
      << " audio_bytes=" << frame.audio_pcm.size() << "\n";
  decoded = 1;

  // Continue sequentially with decode_next().
  while (true) {
    CinematicFrame next;
    QString next_err;
    if (!dec->decode_next(&next, &next_err)) {
      if (!next_err.isEmpty()) {
        err << "decode_next failed at frame " << decoded << ": " << next_err << "\n";
        return 2;
      }
      break;
    }
    if (next.image.isNull()) {
      err << "decode_next returned null image at frame " << decoded << "\n";
      return 2;
    }
    if (decoded < 5 || (info.frame_count > 0 && decoded == info.frame_count - 1)) {
      out << "Frame " << decoded << ": hash=" << hash_image(next.image) << " audio_bytes=" << next.audio_pcm.size() << "\n";
    }
    ++decoded;

    if (info.frame_count > 0 && decoded >= info.frame_count) {
      break;
    }
    if (decoded > 100000) {
      err << "Aborting: decoded too many frames.\n";
      return 2;
    }
  }

  out << "Decoded frames: " << decoded << "\n";
  return 0;
}

