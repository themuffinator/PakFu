#include "formats/ftx_image.h"

#include <cstring>

#include <QtGlobal>

namespace {
constexpr int kHeaderSize = 12;
constexpr int kMaxDimension = 16384;

[[nodiscard]] bool read_i32_le(const QByteArray& bytes, int offset, qint32* out) {
  if (!out || offset < 0 || offset + 4 > bytes.size()) {
    return false;
  }

  const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
  const quint32 v = static_cast<quint32>(p[0]) |
                    (static_cast<quint32>(p[1]) << 8) |
                    (static_cast<quint32>(p[2]) << 16) |
                    (static_cast<quint32>(p[3]) << 24);
  std::memcpy(out, &v, sizeof(qint32));
  return true;
}
}  // namespace

QImage decode_ftx_image(const QByteArray& bytes, QString* error) {
  if (error) {
    *error = {};
  }

  if (bytes.size() < kHeaderSize) {
    if (error) {
      *error = "FTX header is too small.";
    }
    return {};
  }

  qint32 width_i = 0;
  qint32 height_i = 0;
  qint32 has_alpha = 0;
  if (!read_i32_le(bytes, 0, &width_i) ||
      !read_i32_le(bytes, 4, &height_i) ||
      !read_i32_le(bytes, 8, &has_alpha)) {
    if (error) {
      *error = "Unable to parse FTX header.";
    }
    return {};
  }

  if (width_i <= 0 || height_i <= 0) {
    if (error) {
      *error = "Invalid FTX dimensions.";
    }
    return {};
  }
  if (width_i > kMaxDimension || height_i > kMaxDimension) {
    if (error) {
      *error = "FTX dimensions are unreasonably large.";
    }
    return {};
  }

  const qint64 width = width_i;
  const qint64 height = height_i;
  const qint64 pixel_bytes = width * height * 4;
  if (pixel_bytes <= 0 || pixel_bytes > (bytes.size() - kHeaderSize)) {
    if (error) {
      *error = "FTX pixel payload is truncated.";
    }
    return {};
  }

  QImage image(width_i, height_i, QImage::Format_RGBA8888);
  if (image.isNull()) {
    if (error) {
      *error = "Unable to allocate FTX image.";
    }
    return {};
  }

  const auto* src = reinterpret_cast<const unsigned char*>(bytes.constData() + kHeaderSize);
  for (int y = 0; y < height_i; ++y) {
    unsigned char* dst = image.scanLine(y);
    for (int x = 0; x < width_i; ++x) {
      const int i = (y * width_i + x) * 4;
      dst[i + 0] = src[i + 0];
      dst[i + 1] = src[i + 1];
      dst[i + 2] = src[i + 2];
      dst[i + 3] = (has_alpha != 0) ? src[i + 3] : 255;
    }
  }

  return image;
}
