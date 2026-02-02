#include "formats/roq_cinematic.h"

#include <algorithm>
#include <limits>

#include <QtGlobal>

#include <QDir>
#include <QFileInfo>

namespace {
constexpr quint16 kRoqMagicNumber = 0x1084;
constexpr quint32 kRoqMagicSize = 0xFFFFFFFFu;
constexpr int kRoqPreambleSize = 8;
constexpr quint16 kRoqInfo = 0x1001;
constexpr quint16 kRoqQuadCodebook = 0x1002;
constexpr quint16 kRoqQuadVq = 0x1011;
constexpr quint16 kRoqQuadJpeg = 0x1012;
constexpr quint16 kRoqQuadHang = 0x1013;
constexpr quint16 kRoqSoundMono = 0x1020;
constexpr quint16 kRoqSoundStereo = 0x1021;
constexpr quint16 kRoqPacket = 0x1030;

constexpr int kRoqAudioSampleRate = 22050;
constexpr qint64 kRoqMaxChunkBytes = 64LL * 1024 * 1024;

constexpr int kRoqIdMot = 0x0;
constexpr int kRoqIdFcc = 0x1;
constexpr int kRoqIdSld = 0x2;
constexpr int kRoqIdCcc = 0x3;

QString clean_path(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

[[nodiscard]] quint16 read_u16_le_from(const char* b) {
  const quint16 b0 = static_cast<quint8>(b[0]);
  const quint16 b1 = static_cast<quint8>(b[1]);
  return static_cast<quint16>(b0 | (b1 << 8));
}

[[nodiscard]] quint32 read_u32_le_from(const char* b) {
  const quint32 b0 = static_cast<quint8>(b[0]);
  const quint32 b1 = static_cast<quint8>(b[1]);
  const quint32 b2 = static_cast<quint8>(b[2]);
  const quint32 b3 = static_cast<quint8>(b[3]);
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

[[nodiscard]] bool read_exact(QFile& f, qint64 n, QByteArray* out) {
  if (out) {
    out->clear();
  }
  if (n <= 0) {
    return n == 0;
  }
  if (n > std::numeric_limits<int>::max()) {
    return false;
  }
  QByteArray buf;
  buf.resize(static_cast<int>(n));
  const qint64 got = f.read(buf.data(), n);
  if (got != n) {
    return false;
  }
  if (out) {
    *out = std::move(buf);
  }
  return true;
}

[[nodiscard]] quint8 clamp_u8(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 255) {
    return 255;
  }
  return static_cast<quint8>(v);
}

struct YuvTables {
  int r_add_v[256]{};
  int g_add_u[256]{};
  int g_add_v[256]{};
  int b_add_u[256]{};
};

const YuvTables& yuv_tables() {
  static const YuvTables tables = []() {
    YuvTables t;
    for (int i = 0; i < 256; ++i) {
      const int d = i - 128;
      t.r_add_v[i] = (1436 * d) >> 10;
      t.g_add_v[i] = (731 * d) >> 10;
      t.g_add_u[i] = (352 * d) >> 10;
      t.b_add_u[i] = (1815 * d) >> 10;
    }
    return t;
  }();
  return tables;
}

[[nodiscard]] QRgb yuv_to_rgb(quint8 y, quint8 u, quint8 v) {
  const YuvTables& t = yuv_tables();
  const int yy = static_cast<int>(y);

  // Full-range (JPEG) YUV -> RGB.
  const int r = yy + t.r_add_v[v];
  const int g = yy - t.g_add_u[u] - t.g_add_v[v];
  const int b = yy + t.b_add_u[u];

  return qRgba(clamp_u8(r), clamp_u8(g), clamp_u8(b), 255);
}

[[nodiscard]] QVector<int16_t> roq_dpcm_delta_table() {
  QVector<int16_t> table;
  table.resize(256);
  for (int i = 0; i < 128; ++i) {
    const int16_t sq = static_cast<int16_t>(i * i);
    table[i] = sq;
    table[i + 128] = static_cast<int16_t>(-sq);
  }
  return table;
}
}  // namespace

RoqCinematicDecoder::RoqCinematicDecoder() = default;
RoqCinematicDecoder::~RoqCinematicDecoder() = default;

bool RoqCinematicDecoder::open_file(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }
  close();

  const QString clean = clean_path(file_path);
  if (clean.isEmpty()) {
    if (error) {
      *error = "Empty ROQ path.";
    }
    return false;
  }

  const QFileInfo info(clean);
  if (!info.exists() || !info.isFile()) {
    if (error) {
      *error = "ROQ file not found.";
    }
    return false;
  }

  file_.setFileName(info.absoluteFilePath());
  if (!file_.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open ROQ file.";
    }
    close();
    return false;
  }

  QByteArray header;
  if (!read_exact(file_, kRoqPreambleSize, &header) || header.size() != kRoqPreambleSize) {
    if (error) {
      *error = "Incomplete ROQ header.";
    }
    close();
    return false;
  }

  const quint16 magic = read_u16_le_from(header.constData());
  const quint32 magic_size = read_u32_le_from(header.constData() + 2);
  const quint16 frame_rate = read_u16_le_from(header.constData() + 6);

  if (magic != kRoqMagicNumber || magic_size != kRoqMagicSize) {
    if (error) {
      *error = "Not a valid ROQ file.";
    }
    close();
    return false;
  }

  if (frame_rate == 0) {
    if (error) {
      *error = "Invalid ROQ frame rate (0).";
    }
    close();
    return false;
  }

  info_ = CinematicInfo{};
  info_.format = "roq";
  info_.fps = static_cast<double>(frame_rate);
  info_.audio_sample_rate = kRoqAudioSampleRate;
  info_.audio_bytes_per_sample = 2;
  info_.audio_signed = true;

  data_start_pos_ = file_.pos();

  if (!scan_file_for_info(error)) {
    close();
    return false;
  }

  if (!reset(error)) {
    close();
    return false;
  }

  return true;
}

void RoqCinematicDecoder::close() {
  if (file_.isOpen()) {
    file_.close();
  }
  file_.setFileName({});
  info_ = {};
  data_start_pos_ = 0;
  next_frame_index_ = 0;
  scanned_frame_count_ = -1;
  reset_decoder_state();
  audio_channels_ = 0;
}

bool RoqCinematicDecoder::is_open() const {
  return file_.isOpen();
}

CinematicInfo RoqCinematicDecoder::info() const {
  return info_;
}

int RoqCinematicDecoder::frame_count() const {
  return info_.frame_count;
}

bool RoqCinematicDecoder::reset(QString* error) {
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "ROQ is not open.";
    }
    return false;
  }
  if (!file_.seek(data_start_pos_)) {
    if (error) {
      *error = "Unable to seek ROQ start.";
    }
    return false;
  }
  next_frame_index_ = 0;
  reset_decoder_state();
  return true;
}

bool RoqCinematicDecoder::decode_next(CinematicFrame* out, QString* error) {
  if (out) {
    *out = {};
  }
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "ROQ is not open.";
    }
    return false;
  }

  const CinematicInfo ci = info_;
  const bool has_audio = ci.has_audio &&
                         ci.audio_sample_rate > 0 &&
                         ci.audio_channels > 0 &&
                         ci.audio_bytes_per_sample > 0 &&
                         ci.fps > 0.0;
  const int block_align = has_audio ? (ci.audio_bytes_per_sample * ci.audio_channels) : 0;

  auto want_audio_bytes_for_next_frame = [&]() -> int {
    if (!has_audio) {
      return 0;
    }
    const double samples_per_frame = static_cast<double>(ci.audio_sample_rate) / ci.fps;
    audio_sample_accum_ += samples_per_frame;
    const int target_total = static_cast<int>(audio_sample_accum_);
    int want_samples = target_total - audio_samples_emitted_;
    if (want_samples < 0) {
      want_samples = 0;
    }
    audio_samples_emitted_ += want_samples;
    return want_samples * block_align;
  };

  while (!file_.atEnd()) {
    quint16 chunk_type = 0;
    quint32 chunk_size = 0;
    quint16 chunk_arg = 0;
    if (!read_next_chunk(&chunk_type, &chunk_size, &chunk_arg, error)) {
      return false;
    }

    if (chunk_type == kRoqInfo) {
      if (!skip_bytes(chunk_size, error)) {
        return false;
      }
      continue;
    }

    if (chunk_type == kRoqPacket || chunk_type == kRoqQuadHang || chunk_type == kRoqQuadJpeg) {
      if (!skip_bytes(chunk_size, error)) {
        return false;
      }
      continue;
    }

    if (chunk_type == kRoqSoundMono || chunk_type == kRoqSoundStereo) {
      QByteArray data;
      if (!read_bytes(chunk_size, &data, error)) {
        return false;
      }
      // Build preamble+data buffer so the audio decode logic matches the file format.
      QByteArray preamble_and_data;
      preamble_and_data.resize(kRoqPreambleSize + data.size());
      char* dst = preamble_and_data.data();
      dst[0] = static_cast<char>(chunk_type & 0xFF);
      dst[1] = static_cast<char>((chunk_type >> 8) & 0xFF);
      dst[2] = static_cast<char>(chunk_size & 0xFF);
      dst[3] = static_cast<char>((chunk_size >> 8) & 0xFF);
      dst[4] = static_cast<char>((chunk_size >> 16) & 0xFF);
      dst[5] = static_cast<char>((chunk_size >> 24) & 0xFF);
      dst[6] = static_cast<char>(chunk_arg & 0xFF);
      dst[7] = static_cast<char>((chunk_arg >> 8) & 0xFF);
      std::copy(data.begin(), data.end(), dst + kRoqPreambleSize);

      QString aud_err;
      const QByteArray pcm = decode_audio_chunk(chunk_type, preamble_and_data, &aud_err);
      if (!aud_err.isEmpty()) {
        if (error) {
          *error = aud_err;
        }
        return false;
      }
      if (!pcm.isEmpty()) {
        audio_pending_.append(pcm);
      }
      continue;
    }

    if (chunk_type == kRoqQuadCodebook || chunk_type == kRoqQuadVq) {
      if (!decode_video_chunk(chunk_type, chunk_size, chunk_arg, error)) {
        return false;
      }

      if (chunk_type == kRoqQuadVq) {
        const QImage img = current_frame_to_image();
        if (img.isNull()) {
          if (error) {
            *error = "Unable to decode ROQ frame.";
          }
          return false;
        }

        // Determine how much audio to attach to this frame, and read ahead (without consuming the next VQ frame)
        // to fill it. RoQ audio chunks can be interleaved before or after the VQ chunk.
        const int want_audio_bytes = want_audio_bytes_for_next_frame();
        while (has_audio && want_audio_bytes > 0 && audio_pending_.size() < want_audio_bytes && !file_.atEnd()) {
          const qint64 next_pos = file_.pos();
          quint16 next_type = 0;
          quint32 next_size = 0;
          quint16 next_arg = 0;
          QString next_err;
          if (!read_next_chunk(&next_type, &next_size, &next_arg, &next_err)) {
            if (error) {
              *error = next_err.isEmpty() ? "Unable to read ROQ." : next_err;
            }
            return false;
          }

          if (next_type == kRoqQuadVq) {
            // Leave the next frame for the next decode_next() call.
            if (!file_.seek(next_pos)) {
              if (error) {
                *error = "Unable to seek in ROQ.";
              }
              return false;
            }
            break;
          }

          if (next_type == kRoqInfo) {
            if (!skip_bytes(next_size, error)) {
              return false;
            }
            continue;
          }

          if (next_type == kRoqPacket || next_type == kRoqQuadHang || next_type == kRoqQuadJpeg) {
            if (!skip_bytes(next_size, error)) {
              return false;
            }
            continue;
          }

          if (next_type == kRoqSoundMono || next_type == kRoqSoundStereo) {
            QByteArray next_data;
            if (!read_bytes(next_size, &next_data, error)) {
              return false;
            }
            QByteArray preamble_and_data;
            preamble_and_data.resize(kRoqPreambleSize + next_data.size());
            char* dst = preamble_and_data.data();
            dst[0] = static_cast<char>(next_type & 0xFF);
            dst[1] = static_cast<char>((next_type >> 8) & 0xFF);
            dst[2] = static_cast<char>(next_size & 0xFF);
            dst[3] = static_cast<char>((next_size >> 8) & 0xFF);
            dst[4] = static_cast<char>((next_size >> 16) & 0xFF);
            dst[5] = static_cast<char>((next_size >> 24) & 0xFF);
            dst[6] = static_cast<char>(next_arg & 0xFF);
            dst[7] = static_cast<char>((next_arg >> 8) & 0xFF);
            std::copy(next_data.begin(), next_data.end(), dst + kRoqPreambleSize);

            QString aud_err;
            const QByteArray pcm = decode_audio_chunk(next_type, preamble_and_data, &aud_err);
            if (!aud_err.isEmpty()) {
              if (error) {
                *error = aud_err;
              }
              return false;
            }
            if (!pcm.isEmpty()) {
              audio_pending_.append(pcm);
            }
            continue;
          }

          if (next_type == kRoqQuadCodebook) {
            if (!decode_video_chunk(next_type, next_size, next_arg, error)) {
              return false;
            }
            continue;
          }

          if (error) {
            *error = QString("Unknown ROQ chunk type: 0x%1").arg(next_type, 4, 16, QLatin1Char('0'));
          }
          return false;
        }

        QByteArray audio_out;
        if (want_audio_bytes > 0) {
          const int take = qMin(want_audio_bytes, audio_pending_.size());
          audio_out = audio_pending_.left(take);
          audio_pending_.remove(0, take);
          if (take < want_audio_bytes) {
            audio_out.append(QByteArray(want_audio_bytes - take, 0));
          }
        }

        if (out) {
          out->image = img;
          out->audio_pcm = std::move(audio_out);
          out->index = next_frame_index_;
        }
        ++next_frame_index_;
        return true;
      }
      continue;
    }

    if (error) {
      *error = QString("Unknown ROQ chunk type: 0x%1").arg(chunk_type, 4, 16, QLatin1Char('0'));
    }
    return false;
  }

  return false;
}

bool RoqCinematicDecoder::decode_frame(int index, CinematicFrame* out, QString* error) {
  if (out) {
    *out = {};
  }
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "ROQ is not open.";
    }
    return false;
  }
  if (index < 0) {
    if (error) {
      *error = "ROQ frame index out of range.";
    }
    return false;
  }
  if (info_.frame_count >= 0 && index >= info_.frame_count) {
    if (error) {
      *error = "ROQ frame index out of range.";
    }
    return false;
  }

  if (!reset(error)) {
    return false;
  }

  CinematicFrame frame;
  for (int i = 0; i <= index; ++i) {
    QString err;
    if (!decode_next(&frame, &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to decode requested ROQ frame." : err;
      }
      return false;
    }
  }
  if (out) {
    *out = std::move(frame);
  }
  return true;
}

bool RoqCinematicDecoder::scan_file_for_info(QString* error) {
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "ROQ is not open.";
    }
    return false;
  }

  if (!file_.seek(data_start_pos_)) {
    if (error) {
      *error = "Unable to seek ROQ start.";
    }
    return false;
  }

  int width = 0;
  int height = 0;
  int frames = 0;
  int audio_channels = 0;

  while (!file_.atEnd()) {
    quint16 type = 0;
    quint32 size = 0;
    quint16 arg = 0;
    if (!read_next_chunk(&type, &size, &arg, error)) {
      break;
    }

    if (type == kRoqInfo) {
      QByteArray data;
      if (!read_bytes(size, &data, error)) {
        return false;
      }
      if (data.size() >= 4) {
        width = static_cast<int>(read_u16_le_from(data.constData()));
        height = static_cast<int>(read_u16_le_from(data.constData() + 2));
      }
      continue;
    }

    if (type == kRoqSoundMono) {
      audio_channels = 1;
      if (!skip_bytes(size, error)) {
        return false;
      }
      continue;
    }
    if (type == kRoqSoundStereo) {
      audio_channels = 2;
      if (!skip_bytes(size, error)) {
        return false;
      }
      continue;
    }

    if (type == kRoqQuadVq) {
      ++frames;
      if (!skip_bytes(size, error)) {
        return false;
      }
      continue;
    }

    if (type == kRoqQuadCodebook) {
      if (!skip_bytes(size, error)) {
        return false;
      }
      continue;
    }

    if (type == kRoqPacket || type == kRoqQuadHang || type == kRoqQuadJpeg) {
      if (!skip_bytes(size, error)) {
        return false;
      }
      continue;
    }

    if (error) {
      *error = QString("Unknown ROQ chunk type: 0x%1").arg(type, 4, 16, QLatin1Char('0'));
    }
    return false;
  }

  if (width <= 0 || height <= 0) {
    if (error) {
      *error = "ROQ INFO chunk not found (missing width/height).";
    }
    return false;
  }
  if ((width % 16) != 0 || (height % 16) != 0) {
    if (error) {
      *error = QString("ROQ dimensions must be a multiple of 16 (got %1x%2).").arg(width).arg(height);
    }
    return false;
  }

  info_.width = width;
  info_.height = height;
  info_.frame_count = frames;

  audio_channels_ = audio_channels;
  if (audio_channels_ > 0) {
    info_.has_audio = true;
    info_.audio_channels = audio_channels_;
  } else {
    info_.has_audio = false;
    info_.audio_channels = 0;
  }

  // Restore to start for actual decode.
  if (!file_.seek(data_start_pos_)) {
    if (error) {
      *error = "Unable to seek ROQ start.";
    }
    return false;
  }

  return true;
}

bool RoqCinematicDecoder::read_next_chunk(quint16* type, quint32* size, quint16* arg, QString* error) {
  if (type) {
    *type = 0;
  }
  if (size) {
    *size = 0;
  }
  if (arg) {
    *arg = 0;
  }
  QByteArray pre;
  if (!read_exact(file_, kRoqPreambleSize, &pre) || pre.size() != kRoqPreambleSize) {
    if (error && error->isEmpty()) {
      *error = "Unexpected end of ROQ.";
    }
    return false;
  }

  const quint16 t = read_u16_le_from(pre.constData());
  const quint32 s = read_u32_le_from(pre.constData() + 2);
  const quint16 a = read_u16_le_from(pre.constData() + 6);

  const qint64 remaining = file_.size() - file_.pos();
  if (remaining < 0 || s > static_cast<quint64>(remaining) || s > static_cast<quint32>(std::numeric_limits<int>::max())) {
    if (error && error->isEmpty()) {
      *error = QString("Invalid ROQ chunk size: %1").arg(s);
    }
    return false;
  }

  if (type) {
    *type = t;
  }
  if (size) {
    *size = s;
  }
  if (arg) {
    *arg = a;
  }
  return true;
}

bool RoqCinematicDecoder::skip_bytes(qint64 count, QString* error) {
  if (count < 0) {
    if (error) {
      *error = "Invalid ROQ seek.";
    }
    return false;
  }
  if (count == 0) {
    return true;
  }
  if (!file_.seek(file_.pos() + count)) {
    if (error && error->isEmpty()) {
      *error = "Unable to seek in ROQ.";
    }
    return false;
  }
  return true;
}

bool RoqCinematicDecoder::read_bytes(qint64 count, QByteArray* out, QString* error) {
  if (count < 0) {
    if (error) {
      *error = "Invalid ROQ read.";
    }
    return false;
  }
  if (count > kRoqMaxChunkBytes) {
    if (error) {
      *error = QString("ROQ chunk is too large (%1 bytes).").arg(count);
    }
    return false;
  }
  if (!read_exact(file_, count, out)) {
    if (error && error->isEmpty()) {
      *error = "Unable to read ROQ.";
    }
    return false;
  }
  return true;
}

void RoqCinematicDecoder::reset_decoder_state() {
  for (int i = 0; i < 256; ++i) {
    cb2x2_[i] = RoqCell{};
    cb4x4_[i] = RoqQCell{};
  }
  y_cur_.clear();
  u_cur_.clear();
  v_cur_.clear();
  y_last_.clear();
  u_last_.clear();
  v_last_.clear();
  has_last_frame_ = false;
  audio_pending_.clear();
  audio_sample_accum_ = 0.0;
  audio_samples_emitted_ = 0;
}

bool RoqCinematicDecoder::decode_video_chunk(quint16 chunk_type,
                                            quint32 chunk_size,
                                            quint16 chunk_arg,
                                            QString* error) {
  QByteArray data;
  if (!read_bytes(static_cast<qint64>(chunk_size), &data, error)) {
    return false;
  }

  if (chunk_type == kRoqQuadCodebook) {
    return decode_codebook_chunk(data, chunk_arg, error);
  }
  if (chunk_type == kRoqQuadVq) {
    return decode_vq_chunk(data, chunk_arg, error);
  }

  if (error) {
    *error = "Unexpected ROQ video chunk.";
  }
  return false;
}

bool RoqCinematicDecoder::decode_codebook_chunk(const QByteArray& data, quint16 chunk_arg, QString* error) {
  if (error) {
    error->clear();
  }

  int nv1 = (chunk_arg >> 8) & 0xFF;
  int nv2 = (chunk_arg)&0xFF;
  if (nv1 == 0) {
    nv1 = 256;
  }
  if (nv2 == 0 && (nv1 * 6) < data.size()) {
    nv2 = 256;
  }

  const int want = nv1 * 6 + nv2 * 4;
  if (data.size() < want) {
    if (error) {
      *error = "ROQ codebook chunk is incomplete.";
    }
    return false;
  }

  int pos = 0;
  for (int i = 0; i < nv1; ++i) {
    RoqCell& c = cb2x2_[i];
    c.y[0] = static_cast<quint8>(data[pos + 0]);
    c.y[1] = static_cast<quint8>(data[pos + 1]);
    c.y[2] = static_cast<quint8>(data[pos + 2]);
    c.y[3] = static_cast<quint8>(data[pos + 3]);
    c.u = static_cast<quint8>(data[pos + 4]);
    c.v = static_cast<quint8>(data[pos + 5]);
    pos += 6;
  }

  for (int i = 0; i < nv2; ++i) {
    RoqQCell& q = cb4x4_[i];
    q.idx[0] = static_cast<quint8>(data[pos + 0]);
    q.idx[1] = static_cast<quint8>(data[pos + 1]);
    q.idx[2] = static_cast<quint8>(data[pos + 2]);
    q.idx[3] = static_cast<quint8>(data[pos + 3]);
    pos += 4;
  }

  return true;
}

bool RoqCinematicDecoder::decode_vq_chunk(const QByteArray& data, quint16 chunk_arg, QString* error) {
  if (error) {
    error->clear();
  }

  const int w = info_.width;
  const int h = info_.height;
  if (w <= 0 || h <= 0 || (w % 16) != 0 || (h % 16) != 0) {
    if (error) {
      *error = "ROQ dimensions are invalid.";
    }
    return false;
  }

  const int pixels = w * h;
  if (pixels <= 0) {
    if (error) {
      *error = "ROQ frame size is invalid.";
    }
    return false;
  }

  if (y_cur_.size() != pixels) {
    y_cur_.resize(pixels);
    u_cur_.resize(pixels);
    v_cur_.resize(pixels);
  }
  if (y_last_.size() != pixels) {
    y_last_.resize(pixels);
    u_last_.resize(pixels);
    v_last_.resize(pixels);
    y_last_.fill(0);
    u_last_.fill(static_cast<char>(128));
    v_last_.fill(static_cast<char>(128));
    has_last_frame_ = true;
  }

  // Start from last frame.
  y_cur_ = y_last_;
  u_cur_ = u_last_;
  v_cur_ = v_last_;

  auto apply_vector_2x2 = [&](int x, int y, const RoqCell& cell) {
    if (x < 0 || y < 0 || x + 1 >= w || y + 1 >= h) {
      return;
    }
    const int i0 = y * w + x;
    y_cur_[i0] = static_cast<char>(cell.y[0]);
    y_cur_[i0 + 1] = static_cast<char>(cell.y[1]);
    y_cur_[i0 + w] = static_cast<char>(cell.y[2]);
    y_cur_[i0 + w + 1] = static_cast<char>(cell.y[3]);

    const char u = static_cast<char>(cell.u);
    const char v = static_cast<char>(cell.v);
    u_cur_[i0] = u;
    u_cur_[i0 + 1] = u;
    u_cur_[i0 + w] = u;
    u_cur_[i0 + w + 1] = u;
    v_cur_[i0] = v;
    v_cur_[i0 + 1] = v;
    v_cur_[i0 + w] = v;
    v_cur_[i0 + w + 1] = v;
  };

  auto apply_flat_2x2 = [&](int x, int y, quint8 yy, quint8 uu, quint8 vv) {
    if (x < 0 || y < 0 || x + 1 >= w || y + 1 >= h) {
      return;
    }
    const int i0 = y * w + x;
    const char yv = static_cast<char>(yy);
    y_cur_[i0] = yv;
    y_cur_[i0 + 1] = yv;
    y_cur_[i0 + w] = yv;
    y_cur_[i0 + w + 1] = yv;

    const char u = static_cast<char>(uu);
    const char v = static_cast<char>(vv);
    u_cur_[i0] = u;
    u_cur_[i0 + 1] = u;
    u_cur_[i0 + w] = u;
    u_cur_[i0 + w + 1] = u;
    v_cur_[i0] = v;
    v_cur_[i0 + 1] = v;
    v_cur_[i0 + w] = v;
    v_cur_[i0 + w + 1] = v;
  };

  auto apply_qcell_4x4 = [&](int x, int y, const RoqQCell& q) {
    apply_vector_2x2(x, y, cb2x2_[q.idx[0]]);
    apply_vector_2x2(x + 2, y, cb2x2_[q.idx[1]]);
    apply_vector_2x2(x, y + 2, cb2x2_[q.idx[2]]);
    apply_vector_2x2(x + 2, y + 2, cb2x2_[q.idx[3]]);
  };

  // RoQ VQ "8x8 VQ code" uses an index into the vq8 table, which Quake III builds by scaling the 4x4 table 2x.
  auto apply_qcell_8x8 = [&](int x, int y, const RoqQCell& q) {
    for (int n = 0; n < 4; ++n) {
      const RoqCell& cell = cb2x2_[q.idx[n]];
      const int ox = (n & 1) * 4;
      const int oy = (n & 2) * 2;
      apply_flat_2x2(x + ox + 0, y + oy + 0, cell.y[0], cell.u, cell.v);
      apply_flat_2x2(x + ox + 2, y + oy + 0, cell.y[1], cell.u, cell.v);
      apply_flat_2x2(x + ox + 0, y + oy + 2, cell.y[2], cell.u, cell.v);
      apply_flat_2x2(x + ox + 2, y + oy + 2, cell.y[3], cell.u, cell.v);
    }
  };

  auto apply_motion = [&](int x, int y, int deltax, int deltay, int sz) {
    if (!has_last_frame_) {
      return;
    }
    const int sx = x + deltax;
    const int sy = y + deltay;
    if (sx < 0 || sy < 0 || sx + sz > w || sy + sz > h) {
      return;
    }
    if (x < 0 || y < 0 || x + sz > w || y + sz > h) {
      return;
    }

    for (int dy = 0; dy < sz; ++dy) {
      const int src_row = (sy + dy) * w + sx;
      const int dst_row = (y + dy) * w + x;
      for (int dx = 0; dx < sz; ++dx) {
        const int s_idx = src_row + dx;
        const int d_idx = dst_row + dx;
        y_cur_[d_idx] = y_last_[s_idx];
        u_cur_[d_idx] = u_last_[s_idx];
        v_cur_[d_idx] = v_last_[s_idx];
      }
    }
  };

  auto read_le16 = [&](int* pos, quint16* out) -> bool {
    if (!pos || !out) {
      return false;
    }
    if (*pos + 2 > data.size()) {
      return false;
    }
    const char* b = data.constData() + *pos;
    *out = read_u16_le_from(b);
    *pos += 2;
    return true;
  };

  auto read_u8 = [&](int* pos, quint8* out) -> bool {
    if (!pos || !out) {
      return false;
    }
    if (*pos + 1 > data.size()) {
      return false;
    }
    *out = static_cast<quint8>(data[*pos]);
    *pos += 1;
    return true;
  };

  const qint8 bias_x = static_cast<qint8>((chunk_arg >> 8) & 0xFF);
  const qint8 bias_y = static_cast<qint8>(chunk_arg & 0xFF);

  int pos = 0;
  quint16 vqflg = 0;
  int vqflg_pairs_remaining = 0;
  bool ok = true;

  auto read_vqid = [&](int* out) -> bool {
    if (!out) {
      return false;
    }
    if (vqflg_pairs_remaining <= 0) {
      if (!read_le16(&pos, &vqflg)) {
        return false;
      }
      vqflg_pairs_remaining = 8;
    }
    *out = static_cast<int>((vqflg & 0xC000u) >> 14);
    vqflg <<= 2;
    --vqflg_pairs_remaining;
    return true;
  };

  int xpos = 0;
  int ypos = 0;

  while (ok && pos < data.size() && ypos < h) {
    for (int yp = ypos; yp < ypos + 16; yp += 8) {
      for (int xp = xpos; xp < xpos + 16; xp += 8) {
        if (!ok) {
          break;
        }
        if (pos >= data.size()) {
          break;
        }

        int vqid = 0;
        if (!read_vqid(&vqid)) {
          ok = false;
          break;
        }

        switch (vqid) {
          case kRoqIdMot:
            break;
          case kRoqIdFcc: {
            quint8 b = 0;
            if (!read_u8(&pos, &b)) {
              ok = false;
              break;
            }
            // RoQ motion: Dx = X + 8 - (arg>>4) - Mx, Dy = Y + 8 - (arg&0x0F) - My (see idroq.txt).
            const int mx = 8 - static_cast<int>((b >> 4) & 0xF) - bias_x;
            const int my = 8 - static_cast<int>(b & 0xF) - bias_y;
            apply_motion(xp, yp, mx, my, 8);
            break;
          }
          case kRoqIdSld: {
            quint8 idx = 0;
            if (!read_u8(&pos, &idx)) {
              ok = false;
              break;
            }
            apply_qcell_8x8(xp, yp, cb4x4_[idx]);
            break;
          }
          case kRoqIdCcc: {
            for (int k = 0; k < 4 && ok; ++k) {
              int x = xp;
              int y = yp;
              if (k & 0x01) {
                x += 4;
              }
              if (k & 0x02) {
                y += 4;
              }

              int sub_vqid = 0;
              if (!read_vqid(&sub_vqid)) {
                ok = false;
                break;
              }

              switch (sub_vqid) {
                case kRoqIdMot:
                  break;
                case kRoqIdFcc: {
                  quint8 b = 0;
                  if (!read_u8(&pos, &b)) {
                    ok = false;
                    break;
                  }
                  const int mx = 8 - static_cast<int>((b >> 4) & 0xF) - bias_x;
                  const int my = 8 - static_cast<int>(b & 0xF) - bias_y;
                  apply_motion(x, y, mx, my, 4);
                  break;
                }
                case kRoqIdSld: {
                  quint8 idx = 0;
                  if (!read_u8(&pos, &idx)) {
                    ok = false;
                    break;
                  }
                  apply_qcell_4x4(x, y, cb4x4_[idx]);
                  break;
                }
                case kRoqIdCcc: {
                  quint8 i0 = 0, i1 = 0, i2 = 0, i3 = 0;
                  if (!read_u8(&pos, &i0) || !read_u8(&pos, &i1) || !read_u8(&pos, &i2) || !read_u8(&pos, &i3)) {
                    ok = false;
                    break;
                  }
                  apply_vector_2x2(x, y, cb2x2_[i0]);
                  apply_vector_2x2(x + 2, y, cb2x2_[i1]);
                  apply_vector_2x2(x, y + 2, cb2x2_[i2]);
                  apply_vector_2x2(x + 2, y + 2, cb2x2_[i3]);
                  break;
                }
              }
            }
            break;
          }
        }
      }
    }

    xpos += 16;
    if (xpos >= w) {
      xpos = 0;
      ypos += 16;
    }
  }

  if (!ok) {
    if (error) {
      *error = "ROQ VQ chunk decode error.";
    }
    return false;
  }

  // Swap frames: keep the decoded frame in y_cur_/u_cur_/v_cur_ for display, but store it as last for the next frame.
  y_last_ = y_cur_;
  u_last_ = u_cur_;
  v_last_ = v_cur_;
  has_last_frame_ = true;

  return true;
}

QByteArray RoqCinematicDecoder::decode_audio_chunk(quint16 chunk_type,
                                                  const QByteArray& preamble_and_data,
                                                  QString* error) {
  if (error) {
    error->clear();
  }

  if (preamble_and_data.size() < kRoqPreambleSize) {
    if (error) {
      *error = "ROQ audio chunk is incomplete.";
    }
    return {};
  }

  const int channels = (chunk_type == kRoqSoundStereo) ? 2 : 1;
  if (info_.audio_channels == 0) {
    info_.audio_channels = channels;
    info_.has_audio = true;
  }

  // Build the square delta table (RoQ DPCM).
  static const QVector<int16_t> deltas = roq_dpcm_delta_table();

  // Chunk preamble: type(2), size(4), arg(2). The predictor lives in the arg field.
  const quint8 arg0 = static_cast<quint8>(preamble_and_data[6]);
  const quint8 arg1 = static_cast<quint8>(preamble_and_data[7]);
  const quint16 flag = static_cast<quint16>(arg0 | (static_cast<quint16>(arg1) << 8));

  auto wrap_s16 = [](int v) -> qint16 {
    const int u = v & 0xFFFF;
    return static_cast<qint16>((u & 0x8000) ? (u - 0x10000) : u);
  };

  const int data_bytes = preamble_and_data.size() - kRoqPreambleSize;
  if (data_bytes <= 0) {
    return {};
  }
  if (channels == 2 && (data_bytes & 1) != 0) {
    if (error) {
      *error = "ROQ stereo audio chunk has an odd byte count.";
    }
    return {};
  }

  QByteArray pcm;
  pcm.resize(data_bytes * 2);
  const char* src = preamble_and_data.constData() + kRoqPreambleSize;
  char* dst = pcm.data();

  if (channels == 1) {
    int prev = static_cast<int>(flag);
    for (int i = 0; i < data_bytes; ++i) {
      const quint8 code = static_cast<quint8>(src[i]);
      prev = static_cast<int>(wrap_s16(prev + deltas[code]));
      const qint16 sample = static_cast<qint16>(prev);

      const int out_idx = i * 2;
      dst[out_idx + 0] = static_cast<char>(sample & 0xFF);
      dst[out_idx + 1] = static_cast<char>((sample >> 8) & 0xFF);
    }
    return pcm;
  }

  int prev_l = static_cast<int>(flag & 0xFF00);
  int prev_r = static_cast<int>((flag & 0x00FF) << 8);
  int out_pos = 0;
  for (int i = 0; i < data_bytes; i += 2) {
    const quint8 code_l = static_cast<quint8>(src[i + 0]);
    const quint8 code_r = static_cast<quint8>(src[i + 1]);
    prev_l = static_cast<int>(wrap_s16(prev_l + deltas[code_l]));
    prev_r = static_cast<int>(wrap_s16(prev_r + deltas[code_r]));
    const qint16 sl = static_cast<qint16>(prev_l);
    const qint16 sr = static_cast<qint16>(prev_r);

    // Interleaved little-endian S16 (L,R).
    dst[out_pos + 0] = static_cast<char>(sl & 0xFF);
    dst[out_pos + 1] = static_cast<char>((sl >> 8) & 0xFF);
    dst[out_pos + 2] = static_cast<char>(sr & 0xFF);
    dst[out_pos + 3] = static_cast<char>((sr >> 8) & 0xFF);
    out_pos += 4;
  }

  return pcm;
}

QImage RoqCinematicDecoder::current_frame_to_image() const {
  const int w = info_.width;
  const int h = info_.height;
  if (w <= 0 || h <= 0) {
    return {};
  }
  if (y_cur_.size() != w * h || u_cur_.size() != w * h || v_cur_.size() != w * h) {
    return {};
  }

  QImage img(w, h, QImage::Format_ARGB32);
  if (img.isNull()) {
    return {};
  }

  for (int y = 0; y < h; ++y) {
    QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
    const int row = y * w;
    for (int x = 0; x < w; ++x) {
      const int idx = row + x;
      const quint8 yy = static_cast<quint8>(y_cur_[idx]);
      const quint8 uu = static_cast<quint8>(u_cur_[idx]);
      const quint8 vv = static_cast<quint8>(v_cur_[idx]);
      dst[x] = yuv_to_rgb(yy, uu, vv);
    }
  }

  return img;
}
