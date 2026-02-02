#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <memory>

struct CinematicInfo {
  QString format;  // "cin" or "roq"

  int width = 0;
  int height = 0;
  double fps = 0.0;
  int frame_count = -1;  // -1 if unknown

  bool has_audio = false;
  int audio_sample_rate = 0;
  int audio_channels = 0;
  int audio_bytes_per_sample = 0;  // 1 or 2
  bool audio_signed = false;       // true for signed PCM
};

struct CinematicFrame {
  QImage image;
  QByteArray audio_pcm;
  int index = -1;
};

class CinematicDecoder {
public:
  virtual ~CinematicDecoder() = default;

  virtual bool open_file(const QString& file_path, QString* error = nullptr) = 0;
  virtual void close() = 0;

  [[nodiscard]] virtual bool is_open() const = 0;
  [[nodiscard]] virtual CinematicInfo info() const = 0;
  [[nodiscard]] virtual int frame_count() const = 0;

  virtual bool reset(QString* error = nullptr) = 0;
  virtual bool decode_next(CinematicFrame* out, QString* error = nullptr) = 0;
  virtual bool decode_frame(int index, CinematicFrame* out, QString* error = nullptr) = 0;
};

[[nodiscard]] std::unique_ptr<CinematicDecoder> open_cinematic_file(const QString& file_path,
                                                                    QString* error = nullptr);

