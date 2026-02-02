#pragma once

#include <QFile>
#include <QVector>

#include "formats/cinematic.h"

class CinCinematicDecoder final : public CinematicDecoder {
public:
  CinCinematicDecoder();
  ~CinCinematicDecoder() override;

  bool open_file(const QString& file_path, QString* error = nullptr) override;
  void close() override;

  [[nodiscard]] bool is_open() const override;
  [[nodiscard]] CinematicInfo info() const override;
  [[nodiscard]] int frame_count() const override;

  bool reset(QString* error = nullptr) override;
  bool decode_next(CinematicFrame* out, QString* error = nullptr) override;
  bool decode_frame(int index, CinematicFrame* out, QString* error = nullptr) override;

private:
  struct HuffNode {
    int count = 0;
    bool used = false;
    int children[2] = {-1, -1};
  };

  bool build_index(QString* error);
  void build_huffman_tables(const QByteArray& histograms);
  bool decode_frame_at_current_pos(int frame_index, CinematicFrame* out, QString* error);
  bool read_u32_le(quint32* out, QString* error);
  bool read_bytes(qint64 count, QByteArray* out, QString* error);
  bool skip_bytes(qint64 count, QString* error);

  CinematicInfo info_;
  QFile file_;
  qint64 first_frame_pos_ = 0;
  int next_frame_index_ = 0;

  int audio_chunk_size1_ = 0;
  int audio_chunk_size2_ = 0;
  int current_audio_chunk_ = 0;

  // Huffman trees: 256 tables, each with up to 512 nodes.
  QVector<QVector<HuffNode>> huff_nodes_;
  QVector<int> huff_root_index_;

  QVector<qint64> frame_offsets_;
  QVector<QVector<QRgb>> palette_per_frame_;
  QVector<QRgb> palette_;
};

