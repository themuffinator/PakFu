#include "formats/cin_cinematic.h"

#include <algorithm>
#include <limits>

#include <QDir>
#include <QFileInfo>

namespace {
constexpr int kHuffmanTableSize = 64 * 1024;
constexpr int kCinFps = 14;

QString clean_path(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
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
    if (out) {
      out->clear();
    }
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

[[nodiscard]] bool read_u32_le(QFile& f, quint32* out) {
  if (out) {
    *out = 0;
  }
  char b[4];
  const qint64 got = f.read(b, 4);
  if (got != 4) {
    return false;
  }
  if (out) {
    *out = read_u32_le_from(b);
  }
  return true;
}

[[nodiscard]] QVector<QRgb> default_gray_palette() {
  QVector<QRgb> pal;
  pal.resize(256);
  for (int i = 0; i < 256; ++i) {
    pal[i] = qRgb(i, i, i);
  }
  return pal;
}

[[nodiscard]] QVector<QRgb> parse_cin_palette_768(const QByteArray& bytes) {
  QVector<QRgb> pal;
  pal.resize(256);
  if (bytes.size() < 768) {
    return pal;
  }

  int palette_scale = 2;
  for (int i = 0; i < 768; ++i) {
    if (static_cast<quint8>(bytes[i]) > 63) {
      palette_scale = 0;
      break;
    }
  }

  for (int i = 0; i < 256; ++i) {
    quint8 r = static_cast<quint8>(bytes[i * 3]) << palette_scale;
    quint8 g = static_cast<quint8>(bytes[i * 3 + 1]) << palette_scale;
    quint8 b = static_cast<quint8>(bytes[i * 3 + 2]) << palette_scale;
    if (palette_scale == 2) {
      // Expand 6-bit VGA DAC values to 8-bit using bit replication.
      r = static_cast<quint8>(r | (r >> 6));
      g = static_cast<quint8>(g | (g >> 6));
      b = static_cast<quint8>(b | (b >> 6));
    }
    pal[i] = qRgba(r, g, b, 255);
  }

  return pal;
}

[[nodiscard]] bool read_u32_le_raw(QFile& f, quint32* out) {
  return read_u32_le(f, out);
}
}  // namespace

CinCinematicDecoder::CinCinematicDecoder() = default;
CinCinematicDecoder::~CinCinematicDecoder() = default;

bool CinCinematicDecoder::open_file(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }
  close();

  const QString clean = clean_path(file_path);
  if (clean.isEmpty()) {
    if (error) {
      *error = "Empty CIN path.";
    }
    return false;
  }

  const QFileInfo info(clean);
  if (!info.exists() || !info.isFile()) {
    if (error) {
      *error = "CIN file not found.";
    }
    return false;
  }

  file_.setFileName(info.absoluteFilePath());
  if (!file_.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open CIN file.";
    }
    close();
    return false;
  }

  quint32 width = 0;
  quint32 height = 0;
  quint32 sample_rate = 0;
  quint32 bytes_per_sample = 0;
  quint32 channels = 0;
  if (!read_u32_le(&width, error) || !read_u32_le(&height, error) || !read_u32_le(&sample_rate, error) ||
      !read_u32_le(&bytes_per_sample, error) || !read_u32_le(&channels, error)) {
    if (error && error->isEmpty()) {
      *error = "Incomplete CIN header.";
    }
    close();
    return false;
  }

  if (width == 0 || height == 0 || width > 1024 || height > 1024) {
    if (error) {
      *error = QString("Invalid CIN dimensions: %1x%2").arg(width).arg(height);
    }
    close();
    return false;
  }

  bool audio_present = (sample_rate != 0);
  if (audio_present) {
    if (sample_rate < 8000 || sample_rate > 48000) {
      if (error) {
        *error = QString("Invalid CIN audio sample rate: %1").arg(sample_rate);
      }
      close();
      return false;
    }
    if (bytes_per_sample < 1 || bytes_per_sample > 2) {
      if (error) {
        *error = QString("Invalid CIN audio bytes/sample: %1").arg(bytes_per_sample);
      }
      close();
      return false;
    }
    if (channels < 1 || channels > 2) {
      if (error) {
        *error = QString("Invalid CIN audio channels: %1").arg(channels);
      }
      close();
      return false;
    }
  } else {
    bytes_per_sample = 0;
    channels = 0;
  }

  QByteArray histograms;
  if (!read_bytes(kHuffmanTableSize, &histograms, error) || histograms.size() != kHuffmanTableSize) {
    if (error && error->isEmpty()) {
      *error = "Missing CIN Huffman tables.";
    }
    close();
    return false;
  }
  build_huffman_tables(histograms);

  info_ = CinematicInfo{};
  info_.format = "cin";
  info_.width = static_cast<int>(width);
  info_.height = static_cast<int>(height);
  info_.fps = static_cast<double>(kCinFps);
  info_.has_audio = audio_present;
  info_.audio_sample_rate = static_cast<int>(sample_rate);
  info_.audio_channels = static_cast<int>(channels);
  info_.audio_bytes_per_sample = static_cast<int>(bytes_per_sample);
  info_.audio_signed = (bytes_per_sample == 2);

  if (info_.has_audio) {
    const int block_align = static_cast<int>(bytes_per_sample * channels);
    if (sample_rate % kCinFps != 0) {
      audio_chunk_size1_ = static_cast<int>((sample_rate / kCinFps) * block_align);
      audio_chunk_size2_ = static_cast<int>(((sample_rate / kCinFps) + 1) * block_align);
    } else {
      audio_chunk_size1_ = audio_chunk_size2_ = static_cast<int>((sample_rate / kCinFps) * block_align);
    }
  }

  first_frame_pos_ = file_.pos();
  palette_ = default_gray_palette();

  if (!build_index(error)) {
    close();
    return false;
  }

  if (frame_offsets_.isEmpty()) {
    if (error) {
      *error = "No frames found in CIN file.";
    }
    close();
    return false;
  }

  info_.frame_count = frame_offsets_.size();

  if (!reset(error)) {
    close();
    return false;
  }

  return true;
}

void CinCinematicDecoder::close() {
  if (file_.isOpen()) {
    file_.close();
  }
  file_.setFileName({});
  info_ = {};
  first_frame_pos_ = 0;
  next_frame_index_ = 0;
  audio_chunk_size1_ = 0;
  audio_chunk_size2_ = 0;
  current_audio_chunk_ = 0;
  huff_nodes_.clear();
  huff_root_index_.clear();
  frame_offsets_.clear();
  palette_per_frame_.clear();
  palette_.clear();
}

bool CinCinematicDecoder::is_open() const {
  return file_.isOpen();
}

CinematicInfo CinCinematicDecoder::info() const {
  return info_;
}

int CinCinematicDecoder::frame_count() const {
  return info_.frame_count;
}

bool CinCinematicDecoder::reset(QString* error) {
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "CIN is not open.";
    }
    return false;
  }
  next_frame_index_ = 0;
  current_audio_chunk_ = 0;
  palette_ = palette_per_frame_.isEmpty() ? default_gray_palette() : palette_per_frame_.first();
  return true;
}

bool CinCinematicDecoder::decode_next(CinematicFrame* out, QString* error) {
  if (out) {
    *out = {};
  }
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "CIN is not open.";
    }
    return false;
  }
  if (next_frame_index_ < 0 || next_frame_index_ >= frame_offsets_.size()) {
    return false;
  }

  return decode_frame(next_frame_index_, out, error);
}

bool CinCinematicDecoder::decode_frame(int index, CinematicFrame* out, QString* error) {
  if (out) {
    *out = {};
  }
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "CIN is not open.";
    }
    return false;
  }
  if (index < 0 || index >= frame_offsets_.size()) {
    if (error) {
      *error = "CIN frame index out of range.";
    }
    return false;
  }

  // Restore palette + audio-chunk parity for this frame (supports random access).
  if (index < palette_per_frame_.size()) {
    palette_ = palette_per_frame_[index];
  }
  current_audio_chunk_ = (info_.has_audio ? (index % 2) : 0);

  if (!file_.seek(frame_offsets_[index])) {
    if (error) {
      *error = "Unable to seek CIN frame.";
    }
    return false;
  }

  if (!decode_frame_at_current_pos(index, out, error)) {
    return false;
  }

  next_frame_index_ = index + 1;
  return true;
}

bool CinCinematicDecoder::build_index(QString* error) {
  if (error) {
    error->clear();
  }
  if (!is_open()) {
    if (error) {
      *error = "CIN is not open.";
    }
    return false;
  }

  frame_offsets_.clear();
  palette_per_frame_.clear();

  if (!file_.seek(first_frame_pos_)) {
    if (error) {
      *error = "Unable to seek CIN frames.";
    }
    return false;
  }

  QVector<QRgb> current_palette = default_gray_palette();
  int audio_chunk = 0;

  const int expected_decoded = info_.width * info_.height;
  while (!file_.atEnd()) {
    const qint64 frame_start = file_.pos();

    quint32 command = 0;
    if (!read_u32_le(&command, error)) {
      break;
    }
    if (command == 2) {
      if (error) {
        *error = "Invalid CIN command (2).";
      }
      return false;
    }

    if (command == 1) {
      QByteArray pal_bytes;
      if (!read_bytes(768, &pal_bytes, error) || pal_bytes.size() != 768) {
        if (error && error->isEmpty()) {
          *error = "Incomplete CIN palette.";
        }
        return false;
      }
      current_palette = parse_cin_palette_768(pal_bytes);
    }

    quint32 chunk_size = 0;
    quint32 decoded_size = 0;
    if (!read_u32_le(&chunk_size, error) || !read_u32_le(&decoded_size, error)) {
      if (error) {
        *error = "Incomplete CIN frame header.";
      }
      return false;
    }
    if (chunk_size < 4) {
      if (error) {
        *error = QString("Invalid CIN chunk size: %1").arg(chunk_size);
      }
      return false;
    }
    if (static_cast<int>(decoded_size) != expected_decoded) {
      if (error) {
        *error = QString("Unexpected CIN decoded size: %1 (expected %2)").arg(decoded_size).arg(expected_decoded);
      }
      return false;
    }

    const qint64 compressed_size = static_cast<qint64>(chunk_size) - 4;
    if (!skip_bytes(compressed_size, error)) {
      if (error && error->isEmpty()) {
        *error = "Incomplete CIN frame data.";
      }
      return false;
    }

    if (info_.has_audio) {
      const int audio_size = (audio_chunk ? audio_chunk_size2_ : audio_chunk_size1_);
      audio_chunk ^= 1;
      if (audio_size > 0) {
        if (!skip_bytes(audio_size, error)) {
          if (error && error->isEmpty()) {
            *error = "Incomplete CIN audio data.";
          }
          return false;
        }
      }
    }

    frame_offsets_.push_back(frame_start);
    palette_per_frame_.push_back(current_palette);
  }

  return true;
}

void CinCinematicDecoder::build_huffman_tables(const QByteArray& histograms) {
  huff_nodes_.clear();
  huff_root_index_.clear();
  huff_nodes_.resize(256);
  huff_root_index_.resize(256);

  const auto smallest_node = [](QVector<HuffNode>& nodes, int count) -> int {
    int best = std::numeric_limits<int>::max();
    int best_index = -1;
    for (int i = 0; i < count; ++i) {
      if (nodes[i].used) {
        continue;
      }
      if (nodes[i].count <= 0) {
        continue;
      }
      if (nodes[i].count < best) {
        best = nodes[i].count;
        best_index = i;
      }
    }
    if (best_index >= 0) {
      nodes[best_index].used = true;
    }
    return best_index;
  };

  int idx = 0;
  for (int prev = 0; prev < 256; ++prev) {
    QVector<HuffNode> nodes;
    nodes.resize(512);
    for (int i = 0; i < 512; ++i) {
      nodes[i] = HuffNode{};
    }
    for (int tok = 0; tok < 256; ++tok) {
      nodes[tok].count = (idx < histograms.size()) ? static_cast<quint8>(histograms[idx]) : 0;
      nodes[tok].children[0] = -1;
      nodes[tok].children[1] = -1;
      nodes[tok].used = false;
      ++idx;
    }

    int num_nodes = 256;
    while (num_nodes + 1 < nodes.size()) {
      HuffNode& node = nodes[num_nodes];
      node.children[0] = smallest_node(nodes, num_nodes);
      if (node.children[0] == -1) {
        break;
      }
      node.children[1] = smallest_node(nodes, num_nodes);
      if (node.children[1] == -1) {
        break;
      }
      node.count = nodes[node.children[0]].count + nodes[node.children[1]].count;
      ++num_nodes;
    }

    huff_root_index_[prev] = std::max(0, num_nodes - 1);
    huff_nodes_[prev] = std::move(nodes);
  }
}

bool CinCinematicDecoder::decode_frame_at_current_pos(int frame_index, CinematicFrame* out, QString* error) {
  quint32 command = 0;
  if (!read_u32_le(&command, error)) {
    if (error && error->isEmpty()) {
      *error = "Incomplete CIN frame.";
    }
    return false;
  }
  if (command == 2) {
    if (error) {
      *error = "Invalid CIN command (2).";
    }
    return false;
  }
  if (command == 1) {
    QByteArray pal_bytes;
    if (!read_bytes(768, &pal_bytes, error) || pal_bytes.size() != 768) {
      if (error && error->isEmpty()) {
        *error = "Incomplete CIN palette.";
      }
      return false;
    }
    palette_ = parse_cin_palette_768(pal_bytes);
  }

  quint32 chunk_size = 0;
  quint32 decoded_size = 0;
  if (!read_u32_le(&chunk_size, error) || !read_u32_le(&decoded_size, error)) {
    if (error && error->isEmpty()) {
      *error = "Incomplete CIN frame header.";
    }
    return false;
  }
  if (chunk_size < 4) {
    if (error) {
      *error = QString("Invalid CIN chunk size: %1").arg(chunk_size);
    }
    return false;
  }
  const int expected_decoded = info_.width * info_.height;
  if (static_cast<int>(decoded_size) != expected_decoded) {
    if (error) {
      *error = QString("Unexpected CIN decoded size: %1 (expected %2)").arg(decoded_size).arg(expected_decoded);
    }
    return false;
  }

  const qint64 compressed_size = static_cast<qint64>(chunk_size) - 4;
  constexpr qint64 kMaxCompressedBytes = 64LL * 1024 * 1024;
  if (compressed_size < 0 || compressed_size > kMaxCompressedBytes) {
    if (error) {
      *error = QString("CIN frame is too large (%1 bytes).").arg(compressed_size);
    }
    return false;
  }
  QByteArray compressed;
  if (!read_bytes(compressed_size, &compressed, error) || compressed.size() != compressed_size) {
    if (error && error->isEmpty()) {
      *error = "Incomplete CIN frame data.";
    }
    return false;
  }

  QByteArray indices;
  indices.resize(expected_decoded);

  int prev = 0;
  int bit_pos = 0;
  quint8 v = 0;
  int dat_pos = 0;
  for (int i = 0; i < expected_decoded; ++i) {
    int node = huff_root_index_.value(prev, 0);
    const QVector<HuffNode>& nodes = huff_nodes_[prev];

    while (node >= 256) {
      if (bit_pos == 0) {
        if (dat_pos >= compressed.size()) {
          if (error) {
            *error = "CIN Huffman decode error.";
          }
          return false;
        }
        bit_pos = 8;
        v = static_cast<quint8>(compressed[dat_pos++]);
      }
      const int bit = (v & 0x01) ? 1 : 0;
      v >>= 1;
      --bit_pos;
      const int next = nodes[node].children[bit];
      if (next < 0) {
        if (error) {
          *error = "CIN Huffman tree error.";
        }
        return false;
      }
      node = next;
    }

    const quint8 pix = static_cast<quint8>(node);
    indices[i] = static_cast<char>(pix);
    prev = node;
  }

  QImage img(info_.width, info_.height, QImage::Format_ARGB32);
  if (img.isNull()) {
    if (error) {
      *error = "Unable to allocate CIN frame image.";
    }
    return false;
  }
  for (int y = 0; y < info_.height; ++y) {
    QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
    const int row = y * info_.width;
    for (int x = 0; x < info_.width; ++x) {
      const int idx = static_cast<quint8>(indices[row + x]);
      dst[x] = (idx < palette_.size()) ? palette_[idx] : qRgba(0, 0, 0, 255);
    }
  }

  QByteArray audio;
  if (info_.has_audio) {
    const int audio_size = (current_audio_chunk_ ? audio_chunk_size2_ : audio_chunk_size1_);
    current_audio_chunk_ ^= 1;
    if (audio_size > 0) {
      if (!read_bytes(audio_size, &audio, error) || audio.size() != audio_size) {
        if (error && error->isEmpty()) {
          *error = "Incomplete CIN audio data.";
        }
        return false;
      }
    }
  }

  if (out) {
    out->image = std::move(img);
    out->audio_pcm = std::move(audio);
    out->index = frame_index;
  }

  return true;
}

bool CinCinematicDecoder::read_u32_le(quint32* out, QString* error) {
  if (out) {
    *out = 0;
  }
  if (!read_u32_le_raw(file_, out)) {
    if (error && error->isEmpty()) {
      *error = "Unable to read CIN.";
    }
    return false;
  }
  return true;
}

bool CinCinematicDecoder::read_bytes(qint64 count, QByteArray* out, QString* error) {
  if (!read_exact(file_, count, out)) {
    if (error && error->isEmpty()) {
      *error = "Unable to read CIN.";
    }
    return false;
  }
  return true;
}

bool CinCinematicDecoder::skip_bytes(qint64 count, QString* error) {
  if (count < 0) {
    if (error) {
      *error = "Invalid CIN seek.";
    }
    return false;
  }
  if (count == 0) {
    return true;
  }
  if (!file_.seek(file_.pos() + count)) {
    if (error && error->isEmpty()) {
      *error = "Unable to seek in CIN.";
    }
    return false;
  }
  return true;
}
