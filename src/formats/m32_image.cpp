#include "formats/m32_image.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include <QtGlobal>

namespace {
constexpr int kM32MipLevels = 16;
constexpr int kM32Version = 4;
constexpr int kM32HeaderSize = 968;
constexpr int kM32NameOffset = 4;
constexpr int kM32WidthOffset = 516;
constexpr int kM32HeightOffset = 580;
constexpr int kM32MipOffset = 644;

[[nodiscard]] quint32 read_u32le(const uchar* p) {
	return static_cast<quint32>(p[0]) |
	       (static_cast<quint32>(p[1]) << 8) |
	       (static_cast<quint32>(p[2]) << 16) |
	       (static_cast<quint32>(p[3]) << 24);
}

[[nodiscard]] QString read_name128(const QByteArray& bytes, int offset) {
	if (offset < 0 || offset + 128 > bytes.size()) {
		return {};
	}
	const QByteArray raw = bytes.mid(offset, 128);
	const int nul = raw.indexOf('\0');
	const QByteArray trimmed = (nul >= 0) ? raw.left(nul) : raw;
	QString out = QString::fromLatin1(trimmed).trimmed();
	out.replace('\\', '/');
	return out;
}

[[nodiscard]] bool decode_m32_mip(const uchar* data,
                                  int size,
                                  quint32 offset,
                                  int width,
                                  int height,
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
			*error = "Invalid M32 mip dimensions.";
		}
		return false;
	}

	const quint64 pixel_count = static_cast<quint64>(width) * static_cast<quint64>(height);
	if (pixel_count > static_cast<quint64>(std::numeric_limits<int>::max() / 4)) {
		if (error) {
			*error = "M32 mip is too large.";
		}
		return false;
	}

	const quint64 byte_count = pixel_count * 4;
	const quint64 end = static_cast<quint64>(offset) + byte_count;
	if (offset < kM32HeaderSize || end > static_cast<quint64>(size)) {
		if (error) {
			*error = "M32 mip data exceeds file size.";
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
	for (int y = 0; y < height; ++y) {
		const quint64 row_off = static_cast<quint64>(y) * static_cast<quint64>(width) * 4;
		std::memcpy(dst_bits + y * dst_stride, src + row_off, static_cast<size_t>(width * 4));
	}

	*out = std::move(img);
	return true;
}
}  // namespace

QImage decode_m32_image(const QByteArray& bytes, int mip_level, const QString& texture_name, QString* error) {
	if (error) {
		error->clear();
	}
	if (bytes.size() < kM32HeaderSize) {
		if (error) {
			*error = "M32 header too small.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	if (read_u32le(data) != kM32Version) {
		if (error) {
			*error = "Unsupported M32 texture version.";
		}
		return {};
	}

	quint32 widths[kM32MipLevels] = {};
	quint32 heights[kM32MipLevels] = {};
	quint32 offsets[kM32MipLevels] = {};
	for (int i = 0; i < kM32MipLevels; ++i) {
		widths[i] = read_u32le(data + kM32WidthOffset + i * 4);
		heights[i] = read_u32le(data + kM32HeightOffset + i * 4);
		offsets[i] = read_u32le(data + kM32MipOffset + i * 4);
	}

	const int level = std::clamp(mip_level, 0, kM32MipLevels - 1);
	constexpr quint32 kMaxDim = 16384;
	if (widths[level] == 0 || heights[level] == 0 || widths[level] > kMaxDim || heights[level] > kMaxDim) {
		if (error) {
			const QString name = texture_name.isEmpty() ? read_name128(bytes, kM32NameOffset) : texture_name;
			*error = name.isEmpty()
			           ? QString("Invalid M32 dimensions for mip %1.").arg(level)
			           : QString("Invalid M32 dimensions for %1 mip %2.").arg(name).arg(level);
		}
		return {};
	}

	QImage mip;
	QString err;
	if (!decode_m32_mip(data,
	                    bytes.size(),
	                    offsets[level],
	                    static_cast<int>(widths[level]),
	                    static_cast<int>(heights[level]),
	                    &mip,
	                    &err)) {
		if (error) {
			*error = err.isEmpty() ? QString("Unable to decode M32 mip %1.").arg(level) : err;
		}
		return {};
	}
	return mip;
}
