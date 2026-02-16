#include "formats/lmp_image.h"

#include <limits>

#include <QtGlobal>

namespace {
constexpr int kPaletteBytes = 256 * 3;
constexpr int kQPicHeaderBytes = 8;
constexpr int kWad3PaletteCountBytes = 2;
constexpr int kConcharsWidth = 128;
constexpr int kConcharsHeight = 128;
constexpr int kColormapWidth = 256;
constexpr int kColormapHeight = 64;
constexpr int kPopWidth = 16;
constexpr int kPopHeight = 16;
constexpr int kPaletteGridCols = 16;
constexpr int kPaletteGridRows = 16;

[[nodiscard]] quint32 read_u32le(const uchar* p) {
  return (static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) | (static_cast<quint32>(p[2]) << 16) |
          (static_cast<quint32>(p[3]) << 24));
}

[[nodiscard]] quint16 read_u16le(const uchar* p) {
  return (static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8));
}

[[nodiscard]] bool decode_paletted_indices(const uchar* indices,
                                           quint64 index_count,
                                           int width,
                                           int height,
                                           const QVector<QRgb>& palette,
                                           int transparent_index,
                                           QImage* out,
                                           QString* error) {
  if (error) {
    error->clear();
  }
  if (!out) {
    if (error) {
      *error = "Invalid output image pointer.";
    }
    return false;
  }
  *out = {};

  if (!indices || width <= 0 || height <= 0) {
    if (error) {
      *error = "Invalid LMP image data.";
    }
    return false;
  }
  if (palette.size() != 256) {
    if (error) {
      *error = "LMP decode requires a 256-color palette.";
    }
    return false;
  }

  const quint64 want = static_cast<quint64>(width) * static_cast<quint64>(height);
  if (want != index_count) {
    if (error) {
      *error = "LMP image pixel data size mismatch.";
    }
    return false;
  }

  if (want > static_cast<quint64>(std::numeric_limits<int>::max())) {
    if (error) {
      *error = "LMP image is too large.";
    }
    return false;
  }

  QImage img(width, height, QImage::Format_RGBA8888);
  if (img.isNull()) {
    if (error) {
      *error = "Unable to allocate image.";
    }
    return false;
  }

  uchar* dst_bits = img.bits();
  const int dst_stride = img.bytesPerLine();

  for (int y = 0; y < height; ++y) {
    const uchar* src = indices + static_cast<quint64>(y) * static_cast<quint64>(width);
    uchar* dst = dst_bits + y * dst_stride;
    for (int x = 0; x < width; ++x) {
      const int idx = static_cast<int>(src[x]);
      const QRgb c = palette[idx];
      dst[x * 4 + 0] = static_cast<uchar>(qRed(c));
      dst[x * 4 + 1] = static_cast<uchar>(qGreen(c));
      dst[x * 4 + 2] = static_cast<uchar>(qBlue(c));
      const bool transparent = (transparent_index >= 0 && transparent_index <= 255 && idx == transparent_index);
      dst[x * 4 + 3] = transparent ? 0 : 255;
    }
  }

  *out = std::move(img);
  return true;
}

[[nodiscard]] QImage decode_palette_grid_16x16(const QVector<QRgb>& palette, QString* error) {
  if (error) {
    error->clear();
  }
  if (palette.size() != 256) {
    if (error) {
      *error = "Palette is invalid.";
    }
    return {};
  }

  constexpr int cell = 8;
  const int w = kPaletteGridCols * cell;
  const int h = kPaletteGridRows * cell;
  QImage img(w, h, QImage::Format_RGBA8888);
  if (img.isNull()) {
    if (error) {
      *error = "Unable to allocate image.";
    }
    return {};
  }

  img.fill(Qt::transparent);
  uchar* bits = img.bits();
  const int stride = img.bytesPerLine();

  for (int i = 0; i < 256; ++i) {
    const int gx = i % kPaletteGridCols;
    const int gy = i / kPaletteGridCols;
    const QRgb c = palette[i];
    for (int yy = 0; yy < cell; ++yy) {
      uchar* row = bits + (gy * cell + yy) * stride + gx * cell * 4;
      for (int xx = 0; xx < cell; ++xx) {
        row[xx * 4 + 0] = static_cast<uchar>(qRed(c));
        row[xx * 4 + 1] = static_cast<uchar>(qGreen(c));
        row[xx * 4 + 2] = static_cast<uchar>(qBlue(c));
        row[xx * 4 + 3] = 255;
      }
    }
  }

  return img;
}
}  // namespace

bool extract_lmp_palette_256(const QByteArray& bytes, QVector<QRgb>* out_palette, QString* error) {
  if (error) {
    error->clear();
  }
  if (out_palette) {
    out_palette->clear();
  }

  if (bytes.size() < kPaletteBytes) {
    if (error) {
      *error = "LMP palette is too small (expected 768 bytes).";
    }
    return false;
  }

  if (!out_palette) {
    return true;
  }

  out_palette->resize(256);
  const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
  for (int i = 0; i < 256; ++i) {
    const uchar r = data[i * 3 + 0];
    const uchar g = data[i * 3 + 1];
    const uchar b = data[i * 3 + 2];
    (*out_palette)[i] = qRgba(r, g, b, 255);
  }
  return true;
}

QImage decode_lmp_image(const QByteArray& bytes, const QString& file_name, const QVector<QRgb>* palette, QString* error) {
  if (error) {
    error->clear();
  }
  if (bytes.isEmpty()) {
    if (error) {
      *error = "Empty LMP data.";
    }
    return {};
  }

  const QString lower = file_name.toLower();

  // palette.lmp: 256 * RGB (768 bytes). Show as a palette grid.
  if (bytes.size() >= kPaletteBytes && lower.endsWith("palette.lmp")) {
    QVector<QRgb> pal;
    QString pal_err;
    if (!extract_lmp_palette_256(bytes.left(kPaletteBytes), &pal, &pal_err) || pal.size() != 256) {
      if (error) {
        *error = pal_err.isEmpty() ? "Invalid palette.lmp." : pal_err;
      }
      return {};
    }
    return decode_palette_grid_16x16(pal, error);
  }

  // conchars.lmp: raw 128x128 8bpp indices (no header). Use index 0 as transparent like Quake console font.
  if (lower.endsWith("conchars.lmp")) {
    const int want = kConcharsWidth * kConcharsHeight;
    if (bytes.size() < want) {
      if (error) {
        *error = "Invalid conchars.lmp size (expected at least 16384 bytes).";
      }
      return {};
    }
    if (!palette || palette->size() != 256) {
      if (error) {
        *error = "conchars.lmp requires a 256-color Quake palette (gfx/palette.lmp).";
      }
      return {};
    }
    QImage img;
    QString err;
    if (!decode_paletted_indices(reinterpret_cast<const uchar*>(bytes.constData()),
                                 static_cast<quint64>(want),
                                 kConcharsWidth,
                                 kConcharsHeight,
                                 *palette,
                                 /*transparent_index=*/0,
                                 &img,
                                 &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to decode conchars.lmp." : err;
      }
      return {};
    }
    return img;
  }

  // colormap.lmp: raw 256x64 8bpp indices (no header). Useful for inspection even though it's primarily a lighting table.
  if (lower.endsWith("colormap.lmp")) {
    const int want = kColormapWidth * kColormapHeight;
    if (bytes.size() < want) {
      if (error) {
        *error = "Invalid colormap.lmp size (expected at least 16384 bytes).";
      }
      return {};
    }
    if (!palette || palette->size() != 256) {
      if (error) {
        *error = "colormap.lmp requires a 256-color Quake palette (gfx/palette.lmp).";
      }
      return {};
    }
    QImage img;
    QString err;
    if (!decode_paletted_indices(reinterpret_cast<const uchar*>(bytes.constData()),
                                 static_cast<quint64>(want),
                                 kColormapWidth,
                                 kColormapHeight,
                                 *palette,
                                 /*transparent_index=*/-1,
                                 &img,
                                 &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to decode colormap.lmp." : err;
      }
      return {};
    }
    return img;
  }

  // pop.lmp: raw 16x16 8bpp indices (no header), used by Quake as a software renderer marker.
  if (lower.endsWith("pop.lmp")) {
    const int want = kPopWidth * kPopHeight;
    if (bytes.size() < want) {
      if (error) {
        *error = "Invalid pop.lmp size (expected at least 256 bytes).";
      }
      return {};
    }
    if (!palette || palette->size() != 256) {
      if (error) {
        *error = "pop.lmp requires a 256-color Quake palette (gfx/palette.lmp).";
      }
      return {};
    }
    QImage img;
    QString err;
    if (!decode_paletted_indices(reinterpret_cast<const uchar*>(bytes.constData()),
                                 static_cast<quint64>(want),
                                 kPopWidth,
                                 kPopHeight,
                                 *palette,
                                 /*transparent_index=*/0,
                                 &img,
                                 &err)) {
      if (error) {
        *error = err.isEmpty() ? "Unable to decode pop.lmp." : err;
      }
      return {};
    }
    return img;
  }

  // QPIC: width/height + indices.
  if (bytes.size() < kQPicHeaderBytes) {
    if (error) {
      *error = "LMP header too small.";
    }
    return {};
  }

  const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
  const quint32 w_u32 = read_u32le(data + 0);
  const quint32 h_u32 = read_u32le(data + 4);
  if (w_u32 == 0 || h_u32 == 0) {
    if (error) {
      *error = "Invalid LMP dimensions.";
    }
    return {};
  }

  constexpr quint32 kMaxDim = 16384;
  if (w_u32 > kMaxDim || h_u32 > kMaxDim) {
    if (error) {
      *error = "LMP dimensions are unreasonably large.";
    }
    return {};
  }

  const quint64 pixel_count = static_cast<quint64>(w_u32) * static_cast<quint64>(h_u32);
  if (pixel_count > static_cast<quint64>(std::numeric_limits<int>::max())) {
    if (error) {
      *error = "LMP image is too large.";
    }
    return {};
  }

  const quint64 qpic_data_end = static_cast<quint64>(kQPicHeaderBytes) + pixel_count;
  if (qpic_data_end > static_cast<quint64>(bytes.size())) {
    if (error) {
      *error = "LMP image data exceeds file size.";
    }
    return {};
  }

  const auto* indices = data + kQPicHeaderBytes;

  // Half-Life / WAD3-style QPIC stores an embedded 8-bit palette trailer:
  // [u16 color_count][RGB triples][optional trailing u16].
  // Prefer this palette when present so GoldSrc .lmp files decode without gfx/palette.lmp.
  const quint64 remain_after_qpic = static_cast<quint64>(bytes.size()) - qpic_data_end;
  if (remain_after_qpic >= kWad3PaletteCountBytes) {
    const auto* pal_hdr = data + qpic_data_end;
    const quint16 color_count = read_u16le(pal_hdr);
    if (color_count > 0 && color_count <= 256) {
      const quint64 pal_bytes = static_cast<quint64>(color_count) * 3ULL;
      if (remain_after_qpic >= static_cast<quint64>(kWad3PaletteCountBytes) + pal_bytes) {
        QVector<QRgb> embedded_palette;
        embedded_palette.resize(256);
        for (int i = 0; i < 256; ++i) {
          embedded_palette[i] = qRgba(0, 0, 0, 255);
        }

        const auto* pal_data = pal_hdr + kWad3PaletteCountBytes;
        for (int i = 0; i < static_cast<int>(color_count); ++i) {
          const uchar r = pal_data[i * 3 + 0];
          const uchar g = pal_data[i * 3 + 1];
          const uchar b = pal_data[i * 3 + 2];
          embedded_palette[i] = qRgba(r, g, b, 255);
        }

        QImage img;
        QString err;
        if (!decode_paletted_indices(indices,
                                     pixel_count,
                                     static_cast<int>(w_u32),
                                     static_cast<int>(h_u32),
                                     embedded_palette,
                                     /*transparent_index=*/255,
                                     &img,
                                     &err)) {
          if (error) {
            *error = err.isEmpty() ? "Unable to decode embedded-palette LMP image." : err;
          }
          return {};
        }
        return img;
      }
    }
  }

  if (!palette || palette->size() != 256) {
    if (error) {
      *error = "LMP image requires a palette (embedded WAD3/GoldSrc palette or external gfx/palette.lmp).";
    }
    return {};
  }

  QImage img;
  QString err;
  if (!decode_paletted_indices(indices,
                               pixel_count,
                               static_cast<int>(w_u32),
                               static_cast<int>(h_u32),
                               *palette,
                               /*transparent_index=*/255,
                               &img,
                               &err)) {
    if (error) {
      *error = err.isEmpty() ? "Unable to decode LMP image." : err;
    }
    return {};
  }
  return img;
}
