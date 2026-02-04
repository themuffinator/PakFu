#include "formats/wal_image.h"

#include <algorithm>
#include <limits>

#include <QPainter>
#include <QtGlobal>

namespace {
constexpr int kWalHeaderSize = 100;

[[nodiscard]] quint32 read_u32le(const uchar* p) {
	return (static_cast<quint32>(p[0]) |
			(static_cast<quint32>(p[1]) << 8) |
			(static_cast<quint32>(p[2]) << 16) |
			(static_cast<quint32>(p[3]) << 24));
}

[[nodiscard]] bool decode_wal_mip(const uchar* data,
								 int size,
								 quint32 offset,
								 int width,
								 int height,
								 const QVector<QRgb>& palette,
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

	if (width <= 0 || height <= 0) {
		if (error) {
			*error = "Invalid WAL mip dimensions.";
		}
		return false;
	}
	if (palette.size() != 256) {
		if (error) {
			*error = "WAL decode requires a 256-color palette.";
		}
		return false;
	}

	const quint64 pixel_count = static_cast<quint64>(width) * static_cast<quint64>(height);
	if (pixel_count > static_cast<quint64>(std::numeric_limits<int>::max())) {
		if (error) {
			*error = "WAL mip is too large.";
		}
		return false;
	}

	const quint64 end = static_cast<quint64>(offset) + pixel_count;
	if (end > static_cast<quint64>(size)) {
		if (error) {
			*error = "WAL mip data exceeds file size.";
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

	const uchar* src = data + offset;
	for (quint64 i = 0; i < pixel_count; ++i) {
		const int x = static_cast<int>(i % static_cast<quint64>(width));
		const int y = static_cast<int>(i / static_cast<quint64>(width));
		const uchar idx = src[i];
		const QRgb c = palette[static_cast<int>(idx)];
		uchar* dst = dst_bits + y * dst_stride + x * 4;
		dst[0] = static_cast<uchar>(qRed(c));
		dst[1] = static_cast<uchar>(qGreen(c));
		dst[2] = static_cast<uchar>(qBlue(c));
		dst[3] = (idx == 255) ? 0 : 255;
	}

	*out = std::move(img);
	return true;
}
}  // namespace

QImage decode_wal_image_with_mips(const QByteArray& bytes, const QVector<QRgb>& palette, QString* error) {
	if (error) {
		error->clear();
	}
	if (bytes.size() < kWalHeaderSize) {
		if (error) {
			*error = "WAL header too small.";
		}
		return {};
	}
	if (palette.size() != 256) {
		if (error) {
			*error = "WAL decode requires a 256-color palette.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const int size = bytes.size();

	const quint32 width_u32 = read_u32le(data + 32);
	const quint32 height_u32 = read_u32le(data + 36);
	if (width_u32 == 0 || height_u32 == 0) {
		if (error) {
			*error = "Invalid WAL dimensions.";
		}
		return {};
	}

	constexpr quint32 kMaxDim = 16384;
	if (width_u32 > kMaxDim || height_u32 > kMaxDim) {
		if (error) {
			*error = "WAL dimensions are unreasonably large.";
		}
		return {};
	}

	quint32 offsets[4] = {};
	for (int i = 0; i < 4; ++i) {
		offsets[i] = read_u32le(data + 40 + i * 4);
	}

	const int w0 = static_cast<int>(width_u32);
	const int h0 = static_cast<int>(height_u32);

	const int w1 = std::max(1, w0 / 2);
	const int h1 = std::max(1, h0 / 2);
	const int w2 = std::max(1, w0 / 4);
	const int h2 = std::max(1, h0 / 4);
	const int w3 = std::max(1, w0 / 8);
	const int h3 = std::max(1, h0 / 8);

	QImage mip0, mip1, mip2, mip3;
	QString err;
	if (!decode_wal_mip(data, size, offsets[0], w0, h0, palette, &mip0, &err)) {
		if (error) {
			*error = err.isEmpty() ? "Unable to decode WAL mip 0." : err;
		}
		return {};
	}
	if (!decode_wal_mip(data, size, offsets[1], w1, h1, palette, &mip1, &err)) {
		if (error) {
			*error = err.isEmpty() ? "Unable to decode WAL mip 1." : err;
		}
		return {};
	}
	if (!decode_wal_mip(data, size, offsets[2], w2, h2, palette, &mip2, &err)) {
		if (error) {
			*error = err.isEmpty() ? "Unable to decode WAL mip 2." : err;
		}
		return {};
	}
	if (!decode_wal_mip(data, size, offsets[3], w3, h3, palette, &mip3, &err)) {
		if (error) {
			*error = err.isEmpty() ? "Unable to decode WAL mip 3." : err;
		}
		return {};
	}

	const int pad = 6;
	const int x1 = w0 + pad;
	const int y2 = h0 + pad;
	const int canvas_w = w0 + pad + w1;
	const int canvas_h = h0 + pad + std::max(h2, h3);

	QImage composite(canvas_w, canvas_h, QImage::Format_RGBA8888);
	if (composite.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return {};
	}
	composite.fill(Qt::transparent);

	QPainter p(&composite);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	p.drawImage(0, 0, mip0);
	p.drawImage(x1, 0, mip1);
	p.drawImage(0, y2, mip2);
	p.drawImage(x1, y2, mip3);
	p.end();

	return composite;
}

QImage decode_wal_image(const QByteArray& bytes, const QVector<QRgb>& palette, int mip_level, QString* error) {
	if (error) {
		error->clear();
	}
	if (bytes.size() < kWalHeaderSize) {
		if (error) {
			*error = "WAL header too small.";
		}
		return {};
	}
	if (palette.size() != 256) {
		if (error) {
			*error = "WAL decode requires a 256-color palette.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const int size = bytes.size();

	const quint32 width_u32 = read_u32le(data + 32);
	const quint32 height_u32 = read_u32le(data + 36);
	if (width_u32 == 0 || height_u32 == 0) {
		if (error) {
			*error = "Invalid WAL dimensions.";
		}
		return {};
	}

	constexpr quint32 kMaxDim = 16384;
	if (width_u32 > kMaxDim || height_u32 > kMaxDim) {
		if (error) {
			*error = "WAL dimensions are unreasonably large.";
		}
		return {};
	}

	const int w0 = static_cast<int>(width_u32);
	const int h0 = static_cast<int>(height_u32);

	quint32 offsets[4] = {};
	for (int i = 0; i < 4; ++i) {
		offsets[i] = read_u32le(data + 40 + i * 4);
	}

	const int level = std::clamp(mip_level, 0, 3);
	int w = w0;
	int h = h0;
	for (int i = 0; i < level; ++i) {
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
	}

	QImage mip;
	QString err;
	if (!decode_wal_mip(data, size, offsets[level], w, h, palette, &mip, &err)) {
		if (error) {
			*error = err.isEmpty() ? QString("Unable to decode WAL mip %1.").arg(level) : err;
		}
		return {};
	}

	return mip;
}
