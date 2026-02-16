#include "formats/miptex_image.h"

#include <QtGlobal>

namespace {
[[nodiscard]] quint32 read_u32_le_from(const char* p) {
  const quint32 b0 = static_cast<quint8>(p[0]);
  const quint32 b1 = static_cast<quint8>(p[1]);
  const quint32 b2 = static_cast<quint8>(p[2]);
  const quint32 b3 = static_cast<quint8>(p[3]);
  return (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

[[nodiscard]] quint16 read_u16_le_from(const char* p) {
  const quint16 b0 = static_cast<quint8>(p[0]);
  const quint16 b1 = static_cast<quint8>(p[1]);
  return static_cast<quint16>(b0 | (b1 << 8));
}

[[nodiscard]] int mip_dim(int v, int level) {
  int out = v;
  for (int i = 0; i < level; ++i) {
    out = qMax(1, out / 2);
  }
  return out;
}

[[nodiscard]] QString read_name16(const QByteArray& bytes) {
  if (bytes.size() < 16) {
    return {};
  }
  const QByteArray raw = bytes.left(16);
  const int nul = raw.indexOf('\0');
  const QByteArray trimmed = (nul >= 0) ? raw.left(nul) : raw;
  return QString::fromLatin1(trimmed).trimmed();
}

[[nodiscard]] bool uses_index255_transparency(const QString& texture_name) {
  QString name = texture_name.trimmed();
  if (name.isEmpty()) {
    return false;
  }
  name.replace('\\', '/');
  const int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.mid(slash + 1);
  }
  return name.startsWith('{');
}

[[nodiscard]] bool try_extract_embedded_palette(const QByteArray& bytes,
                                                quint32 offset0,
                                                int width,
                                                int height,
                                                QVector<QRgb>* palette_out) {
  if (!palette_out) {
    return false;
  }
  palette_out->clear();
  if (width <= 0 || height <= 0) {
    return false;
  }

  const qint64 mip0 = static_cast<qint64>(width) * static_cast<qint64>(height);
  const qint64 mip1 = static_cast<qint64>(mip_dim(width, 1)) * static_cast<qint64>(mip_dim(height, 1));
  const qint64 mip2 = static_cast<qint64>(mip_dim(width, 2)) * static_cast<qint64>(mip_dim(height, 2));
  const qint64 mip3 = static_cast<qint64>(mip_dim(width, 3)) * static_cast<qint64>(mip_dim(height, 3));
  const qint64 mip_total = mip0 + mip1 + mip2 + mip3;

  const qint64 pal_off = static_cast<qint64>(offset0) + mip_total;
  if (pal_off < 0 || pal_off + 2 > bytes.size()) {
    return false;
  }

  const quint16 pal_count = read_u16_le_from(bytes.constData() + pal_off);
  if (pal_count == 0 || pal_count > 256) {
    return false;
  }

  const qint64 pal_bytes = static_cast<qint64>(pal_count) * 3;
  if (pal_off + 2 + pal_bytes > bytes.size()) {
    return false;
  }

  palette_out->resize(256);
  palette_out->fill(qRgba(0, 0, 0, 255));
  const char* p = bytes.constData() + pal_off + 2;
  for (int i = 0; i < pal_count; ++i) {
    const quint8 r = static_cast<quint8>(p[i * 3 + 0]);
    const quint8 g = static_cast<quint8>(p[i * 3 + 1]);
    const quint8 b = static_cast<quint8>(p[i * 3 + 2]);
    (*palette_out)[i] = qRgba(r, g, b, 255);
  }
  return true;
}
}  // namespace

QImage decode_miptex_image(const QByteArray& bytes,
                           const QVector<QRgb>* external_palette,
                           int mip_level,
                           const QString& texture_name,
                           QString* error) {
  if (error) {
    error->clear();
  }

  if (bytes.size() < 40) {
    if (error) {
      *error = "MIP texture header is incomplete.";
    }
    return {};
  }

  const quint32 width_u = read_u32_le_from(bytes.constData() + 16);
  const quint32 height_u = read_u32_le_from(bytes.constData() + 20);
  if (width_u == 0 || height_u == 0 || width_u > 8192 || height_u > 8192) {
    if (error) {
      *error = "MIP texture dimensions are invalid.";
    }
    return {};
  }
  const int width = static_cast<int>(width_u);
  const int height = static_cast<int>(height_u);

  const quint32 ofs0 = read_u32_le_from(bytes.constData() + 24);
  const quint32 ofs1 = read_u32_le_from(bytes.constData() + 28);
  const quint32 ofs2 = read_u32_le_from(bytes.constData() + 32);
  const quint32 ofs3 = read_u32_le_from(bytes.constData() + 36);
  const quint32 offsets[4] = {ofs0, ofs1, ofs2, ofs3};

  const qint64 mip0_bytes = static_cast<qint64>(width) * static_cast<qint64>(height);
  if (ofs0 == 0 || static_cast<qint64>(ofs0) + mip0_bytes > bytes.size()) {
    if (error) {
      *error = "MIP texture data is out of bounds.";
    }
    return {};
  }

  const int level = qBound(0, mip_level, 3);
  const int mip_width = mip_dim(width, level);
  const int mip_height = mip_dim(height, level);
  const qint64 mip_bytes = static_cast<qint64>(mip_width) * static_cast<qint64>(mip_height);
  const quint32 offset = offsets[level];
  if (offset == 0 || static_cast<qint64>(offset) + mip_bytes > bytes.size()) {
    if (error) {
      *error = QString("MIP texture mip %1 is out of bounds.").arg(level);
    }
    return {};
  }

  QVector<QRgb> palette;
  if (!try_extract_embedded_palette(bytes, ofs0, width, height, &palette)) {
    if (!external_palette || external_palette->size() != 256) {
      if (error) {
        *error = "MIP textures require a 256-color palette.";
      }
      return {};
    }
    palette = *external_palette;
  }

  const bool transparent_255 =
    uses_index255_transparency(texture_name) || uses_index255_transparency(read_name16(bytes));

  QImage img(mip_width, mip_height, QImage::Format_ARGB32);
  if (img.isNull()) {
    if (error) {
      *error = "Unable to allocate image.";
    }
    return {};
  }

  const auto* src = reinterpret_cast<const quint8*>(bytes.constData() + offset);
  for (int y = 0; y < mip_height; ++y) {
    QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
    const qint64 row = static_cast<qint64>(y) * static_cast<qint64>(mip_width);
    for (int x = 0; x < mip_width; ++x) {
      const int idx = static_cast<int>(src[row + x]);
      const QRgb c = (idx >= 0 && idx < palette.size()) ? palette[idx] : qRgba(0, 0, 0, 255);
      if (transparent_255 && idx == 255) {
        dst[x] = qRgba(qRed(c), qGreen(c), qBlue(c), 0);
      } else {
        dst[x] = c;
      }
    }
  }

  return img;
}
