#pragma once

#include <QFile>

#include "formats/cinematic.h"

class RoqCinematicDecoder final : public CinematicDecoder {
public:
  RoqCinematicDecoder();
  ~RoqCinematicDecoder() override;

  bool open_file(const QString& file_path, QString* error = nullptr) override;
  void close() override;

  [[nodiscard]] bool is_open() const override;
  [[nodiscard]] CinematicInfo info() const override;
  [[nodiscard]] int frame_count() const override;

  bool reset(QString* error = nullptr) override;
  bool decode_next(CinematicFrame* out, QString* error = nullptr) override;
  bool decode_frame(int index, CinematicFrame* out, QString* error = nullptr) override;

private:
  struct RoqCell {
    quint8 y[4] = {0, 0, 0, 0};
    quint8 u = 0;
    quint8 v = 0;
  };
  struct RoqQCell {
    quint8 idx[4] = {0, 0, 0, 0};
  };

  bool scan_file_for_info(QString* error);
  bool read_next_chunk(quint16* type, quint32* size, quint16* arg, QString* error);
  bool skip_bytes(qint64 count, QString* error);
  bool read_bytes(qint64 count, QByteArray* out, QString* error);

  void reset_decoder_state();
  bool decode_video_chunk(quint16 chunk_type, quint32 chunk_size, quint16 chunk_arg, QString* error);
  bool decode_codebook_chunk(const QByteArray& data, quint16 chunk_arg, QString* error);
  bool decode_vq_chunk(const QByteArray& data, quint16 chunk_arg, QString* error);
  QByteArray decode_audio_chunk(quint16 chunk_type, const QByteArray& preamble_and_data, QString* error);
  QImage current_frame_to_image() const;

  CinematicInfo info_;
  QFile file_;
  qint64 data_start_pos_ = 0;

  int next_frame_index_ = 0;
  int scanned_frame_count_ = -1;

  // Video state.
  RoqCell cb2x2_[256];
  RoqQCell cb4x4_[256];
  QByteArray y_cur_;
  QByteArray u_cur_;
  QByteArray v_cur_;
  QByteArray y_last_;
  QByteArray u_last_;
  QByteArray v_last_;
  bool has_last_frame_ = false;

  // Audio state.
  int audio_channels_ = 0;
  QByteArray audio_pending_;
  double audio_sample_accum_ = 0.0;
  int audio_samples_emitted_ = 0;
};
