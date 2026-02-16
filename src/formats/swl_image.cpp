#include "formats/swl_image.h"

#include <algorithm>
#include <limits>

#include <QtGlobal>

namespace {
constexpr int kSwlHeaderSize = 1236;
constexpr int kPaletteOffset = 72;
constexpr int kOffsetTableOffset = 1100;
constexpr int kMipCount = 4;

[[nodiscard]] quint32 read_u32le(const uchar* p) {
	return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) | (static_cast<quint32>(p[2]) << 16) |
		   (static_cast<quint32>(p[3]) << 24);
}
}  // namespace

QImage decode_swl_image(const QByteArray& bytes, int mip_level, const QString& texture_name, QString* error) {
	if (error) {
		error->clear();
	}
	Q_UNUSED(texture_name);

	if (bytes.size() < kSwlHeaderSize) {
		if (error) {
			*error = "SWL header too small.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const quint32 width_u32 = read_u32le(data + 64);
	const quint32 height_u32 = read_u32le(data + 68);
	if (width_u32 == 0 || height_u32 == 0) {
		if (error) {
			*error = "Invalid SWL dimensions.";
		}
		return {};
	}

	constexpr quint32 kMaxDim = 16384;
	if (width_u32 > kMaxDim || height_u32 > kMaxDim) {
		if (error) {
			*error = "SWL dimensions are unreasonably large.";
		}
		return {};
	}

	const int level = std::clamp(mip_level, 0, kMipCount - 1);
	int width = static_cast<int>(width_u32);
	int height = static_cast<int>(height_u32);
	for (int i = 0; i < level; ++i) {
		width = std::max(1, width / 2);
		height = std::max(1, height / 2);
	}

	const quint32 mip_offset = read_u32le(data + kOffsetTableOffset + level * 4);
	if (mip_offset == 0) {
		if (error) {
			*error = QString("SWL mip %1 is missing.").arg(level);
		}
		return {};
	}

	const quint64 pixel_count = static_cast<quint64>(width) * static_cast<quint64>(height);
	if (pixel_count > static_cast<quint64>(std::numeric_limits<int>::max())) {
		if (error) {
			*error = "SWL mip is too large.";
		}
		return {};
	}
	if (static_cast<quint64>(mip_offset) + pixel_count > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = QString("SWL mip %1 exceeds file size.").arg(level);
		}
		return {};
	}

	QImage image(width, height, QImage::Format_RGBA8888);
	if (image.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return {};
	}

	const auto* src = data + mip_offset;
	for (quint64 i = 0; i < pixel_count; ++i) {
		const int x = static_cast<int>(i % static_cast<quint64>(width));
		const int y = static_cast<int>(i / static_cast<quint64>(width));
		const int pal_idx = static_cast<int>(src[i]);
		const int pal_off = kPaletteOffset + pal_idx * 4;
		const uchar r = data[pal_off + 0];
		const uchar g = data[pal_off + 1];
		const uchar b = data[pal_off + 2];
		// SiN treats index 255 as transparent in 8-bit assets.
		const uchar a = (pal_idx == 255) ? 0 : 255;
		image.setPixelColor(x, y, QColor(r, g, b, a));
	}

	return image;
}
