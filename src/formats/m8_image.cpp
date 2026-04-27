#include "formats/m8_image.h"

#include <algorithm>
#include <limits>

#include <QtGlobal>

namespace {
constexpr int kM8MipLevels = 16;
constexpr int kM8Version = 2;
constexpr int kM8HeaderSize = 1040;
constexpr int kM8NameOffset = 4;
constexpr int kM8WidthOffset = 36;
constexpr int kM8HeightOffset = 100;
constexpr int kM8MipOffset = 164;
constexpr int kM8PaletteOffset = 260;

[[nodiscard]] quint32 read_u32le(const uchar* p) {
	return static_cast<quint32>(p[0]) |
	       (static_cast<quint32>(p[1]) << 8) |
	       (static_cast<quint32>(p[2]) << 16) |
	       (static_cast<quint32>(p[3]) << 24);
}

[[nodiscard]] QString read_name32(const QByteArray& bytes, int offset) {
	if (offset < 0 || offset + 32 > bytes.size()) {
		return {};
	}
	const QByteArray raw = bytes.mid(offset, 32);
	const int nul = raw.indexOf('\0');
	const QByteArray trimmed = (nul >= 0) ? raw.left(nul) : raw;
	return QString::fromLatin1(trimmed).trimmed();
}

[[nodiscard]] bool decode_m8_mip(const uchar* data,
                                 int size,
                                 quint32 offset,
                                 int width,
                                 int height,
                                 const uchar* palette,
                                 QImage* out,
                                 QString* error) {
	if (out) {
		*out = {};
	}
	if (!out) {
		if (error) {
			*error = "Invalid output image pointer.";
		}
		return false;
	}
	if (width <= 0 || height <= 0) {
		if (error) {
			*error = "Invalid M8 mip dimensions.";
		}
		return false;
	}

	const quint64 pixel_count = static_cast<quint64>(width) * static_cast<quint64>(height);
	if (pixel_count > static_cast<quint64>(std::numeric_limits<int>::max())) {
		if (error) {
			*error = "M8 mip is too large.";
		}
		return false;
	}

	const quint64 end = static_cast<quint64>(offset) + pixel_count;
	if (offset < kM8HeaderSize || end > static_cast<quint64>(size)) {
		if (error) {
			*error = "M8 mip data exceeds file size.";
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

	const uchar* src = data + offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();
	for (quint64 i = 0; i < pixel_count; ++i) {
		const int x = static_cast<int>(i % static_cast<quint64>(width));
		const int y = static_cast<int>(i / static_cast<quint64>(width));
		const uchar idx = src[i];
		const uchar* rgb = palette + static_cast<int>(idx) * 3;
		uchar* dst = dst_bits + y * dst_stride + x * 4;
		dst[0] = rgb[0];
		dst[1] = rgb[1];
		dst[2] = rgb[2];
		dst[3] = (idx == 255) ? 0 : 255;
	}

	*out = std::move(img);
	return true;
}
}  // namespace

QImage decode_m8_image(const QByteArray& bytes, int mip_level, const QString& texture_name, QString* error) {
	if (error) {
		error->clear();
	}
	if (bytes.size() < kM8HeaderSize) {
		if (error) {
			*error = "M8 header too small.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	if (read_u32le(data) != kM8Version) {
		if (error) {
			*error = "Unsupported M8 texture version.";
		}
		return {};
	}

	quint32 widths[kM8MipLevels] = {};
	quint32 heights[kM8MipLevels] = {};
	quint32 offsets[kM8MipLevels] = {};
	for (int i = 0; i < kM8MipLevels; ++i) {
		widths[i] = read_u32le(data + kM8WidthOffset + i * 4);
		heights[i] = read_u32le(data + kM8HeightOffset + i * 4);
		offsets[i] = read_u32le(data + kM8MipOffset + i * 4);
	}

	const int level = std::clamp(mip_level, 0, kM8MipLevels - 1);
	constexpr quint32 kMaxDim = 16384;
	if (widths[level] == 0 || heights[level] == 0 || widths[level] > kMaxDim || heights[level] > kMaxDim) {
		if (error) {
			const QString name = texture_name.isEmpty() ? read_name32(bytes, kM8NameOffset) : texture_name;
			*error = name.isEmpty()
			           ? QString("Invalid M8 dimensions for mip %1.").arg(level)
			           : QString("Invalid M8 dimensions for %1 mip %2.").arg(name).arg(level);
		}
		return {};
	}

	QImage mip;
	QString err;
	if (!decode_m8_mip(data,
	                   bytes.size(),
	                   offsets[level],
	                   static_cast<int>(widths[level]),
	                   static_cast<int>(heights[level]),
	                   data + kM8PaletteOffset,
	                   &mip,
	                   &err)) {
		if (error) {
			*error = err.isEmpty() ? QString("Unable to decode M8 mip %1.").arg(level) : err;
		}
		return {};
	}
	return mip;
}

