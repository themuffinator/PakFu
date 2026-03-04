#include "formats/image_writer.h"

#include <array>
#include <limits>
#include <cstring>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageWriter>
#include <QList>
#include <QSet>
#include <QSaveFile>
#include <QtGlobal>

namespace {
constexpr int kWalHeaderSize = 100;
constexpr int kSwlHeaderSize = 1236;
constexpr int kSwlPaletteOffset = 72;
constexpr int kSwlOffsetTableOffset = 1100;
constexpr int kMipHeaderSize = 40;
constexpr int kDdsHeaderSize = 124;

constexpr quint32 kDdsdCaps = 0x00000001u;
constexpr quint32 kDdsdHeight = 0x00000002u;
constexpr quint32 kDdsdWidth = 0x00000004u;
constexpr quint32 kDdsdPitch = 0x00000008u;
constexpr quint32 kDdsdPixelFormat = 0x00001000u;
constexpr quint32 kDdsPfRgb = 0x00000040u;
constexpr quint32 kDdsPfAlphaPixels = 0x00000001u;
constexpr quint32 kDdsCapsTexture = 0x00001000u;

struct QuantizedImage {
	int width = 0;
	int height = 0;
	QVector<QRgb> palette;
	QByteArray indices;
};

[[nodiscard]] bool format_is_supported(const QString& format) {
	static const QSet<QString> kSupported = {
		"png", "jpg", "bmp", "gif", "tga", "tiff", "pcx", "wal", "swl", "mip", "lmp", "ftx", "dds"
	};
	return kSupported.contains(format);
}

[[nodiscard]] bool format_is_custom(const QString& format) {
	static const QSet<QString> kCustom = {
		"pcx", "wal", "swl", "mip", "lmp", "ftx", "dds"
	};
	return kCustom.contains(format);
}

void write_u16_le(QByteArray* bytes, int offset, quint16 value) {
	if (!bytes || offset < 0 || (offset + 2) > bytes->size()) {
		return;
	}
	(*bytes)[offset + 0] = static_cast<char>(value & 0xFFu);
	(*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
}

void write_u32_le(QByteArray* bytes, int offset, quint32 value) {
	if (!bytes || offset < 0 || (offset + 4) > bytes->size()) {
		return;
	}
	(*bytes)[offset + 0] = static_cast<char>(value & 0xFFu);
	(*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
	(*bytes)[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
	(*bytes)[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
}

void append_u16_le(QByteArray* bytes, quint16 value) {
	if (!bytes) {
		return;
	}
	bytes->append(static_cast<char>(value & 0xFFu));
	bytes->append(static_cast<char>((value >> 8) & 0xFFu));
}

void append_u32_le(QByteArray* bytes, quint32 value) {
	if (!bytes) {
		return;
	}
	bytes->append(static_cast<char>(value & 0xFFu));
	bytes->append(static_cast<char>((value >> 8) & 0xFFu));
	bytes->append(static_cast<char>((value >> 16) & 0xFFu));
	bytes->append(static_cast<char>((value >> 24) & 0xFFu));
}

[[nodiscard]] bool write_bytes_file(const QString& path, const QByteArray& bytes, QString* error) {
	if (error) {
		error->clear();
	}
	const QFileInfo info(path);
	QDir dir(info.absolutePath());
	if (!dir.exists() && !dir.mkpath(".")) {
		if (error) {
			*error = QString("Unable to create output directory: %1").arg(info.absolutePath());
		}
		return false;
	}

	QSaveFile out(path);
	if (!out.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = QString("Unable to open output file: %1").arg(path);
		}
		return false;
	}
	if (out.write(bytes) != bytes.size()) {
		if (error) {
			*error = QString("Unable to write output file: %1").arg(path);
		}
		return false;
	}
	if (!out.commit()) {
		if (error) {
			*error = QString("Unable to finalize output file: %1").arg(path);
		}
		return false;
	}
	return true;
}

[[nodiscard]] QString texture_name_hint(const QString& hint) {
	QString out = hint.trimmed();
	out.replace('\\', '/');
	const int slash = out.lastIndexOf('/');
	if (slash >= 0) {
		out = out.mid(slash + 1);
	}
	const int dot = out.lastIndexOf('.');
	if (dot > 0) {
		out = out.left(dot);
	}
	return out.trimmed();
}

[[nodiscard]] QVector<QRgb> build_palette_from_image(const QImage& source, bool reserve_index_255, bool dither) {
	const Qt::ImageConversionFlags flags =
		dither ? (Qt::DiffuseDither | Qt::PreferDither) : (Qt::ThresholdDither | Qt::AvoidDither);
	const QImage auto_indexed = source.convertToFormat(QImage::Format_Indexed8, flags);
	const QList<QRgb> auto_table = auto_indexed.colorTable();

	QVector<QRgb> palette;
	if (reserve_index_255) {
		const int count = qMin(255, static_cast<int>(auto_table.size()));
		palette.reserve(256);
		for (int i = 0; i < count; ++i) {
			palette.push_back(auto_table[i]);
		}
		const QRgb fallback = palette.isEmpty() ? qRgba(0, 0, 0, 255) : palette.back();
		while (palette.size() < 255) {
			palette.push_back(fallback);
		}
		palette.push_back(qRgba(0, 0, 0, 255));
	} else {
		const int count = qMin(256, static_cast<int>(auto_table.size()));
		palette.reserve(256);
		for (int i = 0; i < count; ++i) {
			palette.push_back(auto_table[i]);
		}
		const QRgb fallback = palette.isEmpty() ? qRgba(0, 0, 0, 255) : palette.back();
		while (palette.size() < 256) {
			palette.push_back(fallback);
		}
	}
	return palette;
}

[[nodiscard]] int nearest_palette_index(QRgb color, const QVector<QRgb>& palette, int max_index, QHash<quint32, int>* cache) {
	if (palette.isEmpty()) {
		return 0;
	}
	const int capped_max = qBound(0, max_index, palette.size() - 1);
	const quint32 key = (static_cast<quint32>(qRed(color)) << 16u) |
	                    (static_cast<quint32>(qGreen(color)) << 8u) |
	                    static_cast<quint32>(qBlue(color));
	if (cache && cache->contains(key)) {
		return cache->value(key);
	}

	int best = 0;
	int best_dist = std::numeric_limits<int>::max();
	for (int i = 0; i <= capped_max; ++i) {
		const QRgb c = palette[i];
		const int dr = qRed(color) - qRed(c);
		const int dg = qGreen(color) - qGreen(c);
		const int db = qBlue(color) - qBlue(c);
		const int dist = dr * dr + dg * dg + db * db;
		if (dist < best_dist) {
			best_dist = dist;
			best = i;
			if (dist == 0) {
				break;
			}
		}
	}
	if (cache) {
		cache->insert(key, best);
	}
	return best;
}

[[nodiscard]] bool quantize_image(const QImage& source,
                                  const QVector<QRgb>* preferred_palette,
                                  bool reserve_index_255,
                                  bool dither,
                                  int alpha_threshold,
                                  QuantizedImage* out,
                                  QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid quantization output pointer.";
		}
		return false;
	}
	*out = {};

	if (source.isNull()) {
		if (error) {
			*error = "Source image is empty.";
		}
		return false;
	}

	const QImage src = source.convertToFormat(QImage::Format_ARGB32);
	if (src.isNull() || src.width() <= 0 || src.height() <= 0) {
		if (error) {
			*error = "Unable to prepare source image.";
		}
		return false;
	}

	QVector<QRgb> palette;
	if (preferred_palette && preferred_palette->size() == 256) {
		palette = *preferred_palette;
	} else {
		palette = build_palette_from_image(src, reserve_index_255, dither);
	}
	if (palette.size() != 256) {
		if (error) {
			*error = "Quantization palette is invalid.";
		}
		return false;
	}

	const Qt::ImageConversionFlags flags =
		dither ? (Qt::DiffuseDither | Qt::PreferDither) : (Qt::ThresholdDither | Qt::AvoidDither);
	QList<QRgb> palette_list;
	palette_list.reserve(palette.size());
	for (const QRgb c : palette) {
		palette_list.push_back(c);
	}
	const QImage indexed = src.convertToFormat(QImage::Format_Indexed8, palette_list, flags);
	if (indexed.isNull()) {
		if (error) {
			*error = "Unable to quantize image.";
		}
		return false;
	}

	const int width = src.width();
	const int height = src.height();
	const int max_opaque_index = reserve_index_255 ? 254 : 255;
	const int alpha_cutoff = qBound(0, alpha_threshold, 255);

	QByteArray indices;
	indices.resize(width * height);
	QHash<quint32, int> remap_cache;
	remap_cache.reserve(2048);

	for (int y = 0; y < height; ++y) {
		const QRgb* src_row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
		const uchar* idx_row = indexed.constScanLine(y);
		uchar* out_row = reinterpret_cast<uchar*>(indices.data()) + (y * width);
		for (int x = 0; x < width; ++x) {
			uchar idx = idx_row[x];
			const QRgb pixel = src_row[x];
			if (reserve_index_255) {
				if (qAlpha(pixel) <= alpha_cutoff) {
					idx = 255;
				} else if (idx == 255) {
					idx = static_cast<uchar>(nearest_palette_index(pixel, palette, max_opaque_index, &remap_cache));
				}
			}
			out_row[x] = idx;
		}
	}

	out->width = width;
	out->height = height;
	out->palette = std::move(palette);
	out->indices = std::move(indices);
	return true;
}

[[nodiscard]] std::array<int, 4> mip_dims(int base_dim) {
	std::array<int, 4> out = {};
	int value = qMax(1, base_dim);
	for (int i = 0; i < 4; ++i) {
		out[static_cast<size_t>(i)] = value;
		value = qMax(1, value / 2);
	}
	return out;
}

[[nodiscard]] bool build_paletted_mips(const QImage& source,
                                       const ImageWriteOptions& options,
                                       QVector<QRgb>* out_palette,
                                       std::array<QByteArray, 4>* out_mips,
                                       std::array<int, 4>* out_w,
                                       std::array<int, 4>* out_h,
                                       QString* error) {
	if (error) {
		error->clear();
	}
	if (!out_palette || !out_mips || !out_w || !out_h) {
		if (error) {
			*error = "Invalid mip output pointers.";
		}
		return false;
	}
	out_palette->clear();
	*out_mips = {};
	*out_w = {};
	*out_h = {};

	if (source.isNull()) {
		if (error) {
			*error = "Source image is empty.";
		}
		return false;
	}

	*out_w = mip_dims(source.width());
	*out_h = mip_dims(source.height());

	QuantizedImage q0;
	if (!quantize_image(source, options.palette, true, options.dither, options.alpha_threshold, &q0, error)) {
		return false;
	}
	if (q0.width != (*out_w)[0] || q0.height != (*out_h)[0]) {
		if (error) {
			*error = "Unexpected mip dimensions while quantizing base image.";
		}
		return false;
	}

	(*out_mips)[0] = q0.indices;
	*out_palette = q0.palette;

	for (int level = 1; level < 4; ++level) {
		const int w = (*out_w)[static_cast<size_t>(level)];
		const int h = (*out_h)[static_cast<size_t>(level)];
		const QImage mip = source.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		QuantizedImage q;
		if (!quantize_image(mip, out_palette, true, options.dither, options.alpha_threshold, &q, error)) {
			return false;
		}
		(*out_mips)[static_cast<size_t>(level)] = q.indices;
	}

	return true;
}

[[nodiscard]] bool encode_wal(const QImage& source, const ImageWriteOptions& options, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid WAL output buffer.";
		}
		return false;
	}
	*out = {};

	if (!options.palette || options.palette->size() != 256) {
		if (error) {
			*error = "WAL output requires a 256-color palette (Quake II pics/colormap.pcx).";
		}
		return false;
	}

	std::array<QByteArray, 4> mip_bytes;
	std::array<int, 4> mip_w = {};
	std::array<int, 4> mip_h = {};
	QVector<QRgb> palette;
	if (!build_paletted_mips(source, options, &palette, &mip_bytes, &mip_w, &mip_h, error)) {
		return false;
	}

	quint32 off = kWalHeaderSize;
	std::array<quint32, 4> offsets = {};
	for (int i = 0; i < 4; ++i) {
		offsets[static_cast<size_t>(i)] = off;
		off += static_cast<quint32>(mip_bytes[static_cast<size_t>(i)].size());
	}

	QByteArray bytes;
	bytes.resize(kWalHeaderSize);
	bytes.fill('\0');

	const QString name_hint = texture_name_hint(options.texture_name);
	const QByteArray name_latin1 = name_hint.toLatin1();
	const int copy_len = qMin(31, static_cast<int>(name_latin1.size()));
	for (int i = 0; i < copy_len; ++i) {
		bytes[i] = name_latin1[i];
	}

	write_u32_le(&bytes, 32, static_cast<quint32>(mip_w[0]));
	write_u32_le(&bytes, 36, static_cast<quint32>(mip_h[0]));
	for (int i = 0; i < 4; ++i) {
		write_u32_le(&bytes, 40 + (i * 4), offsets[static_cast<size_t>(i)]);
	}

	bytes.reserve(static_cast<int>(off));
	for (int i = 0; i < 4; ++i) {
		bytes.append(mip_bytes[static_cast<size_t>(i)]);
	}

	*out = std::move(bytes);
	return true;
}

[[nodiscard]] bool encode_swl(const QImage& source, const ImageWriteOptions& options, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid SWL output buffer.";
		}
		return false;
	}
	*out = {};

	std::array<QByteArray, 4> mip_bytes;
	std::array<int, 4> mip_w = {};
	std::array<int, 4> mip_h = {};
	QVector<QRgb> palette;
	if (!build_paletted_mips(source, options, &palette, &mip_bytes, &mip_w, &mip_h, error)) {
		return false;
	}

	quint32 off = kSwlHeaderSize;
	std::array<quint32, 4> offsets = {};
	for (int i = 0; i < 4; ++i) {
		offsets[static_cast<size_t>(i)] = off;
		off += static_cast<quint32>(mip_bytes[static_cast<size_t>(i)].size());
	}

	QByteArray bytes;
	bytes.resize(kSwlHeaderSize);
	bytes.fill('\0');

	const QString name_hint = texture_name_hint(options.texture_name);
	const QByteArray name_latin1 = name_hint.toLatin1();
	const int copy_len = qMin(63, static_cast<int>(name_latin1.size()));
	for (int i = 0; i < copy_len; ++i) {
		bytes[i] = name_latin1[i];
	}

	write_u32_le(&bytes, 64, static_cast<quint32>(mip_w[0]));
	write_u32_le(&bytes, 68, static_cast<quint32>(mip_h[0]));

	for (int i = 0; i < 256; ++i) {
		const QRgb c = palette[i];
		const int off_pal = kSwlPaletteOffset + (i * 4);
		bytes[off_pal + 0] = static_cast<char>(qRed(c));
		bytes[off_pal + 1] = static_cast<char>(qGreen(c));
		bytes[off_pal + 2] = static_cast<char>(qBlue(c));
		bytes[off_pal + 3] = static_cast<char>(255);
	}
	for (int i = 0; i < 4; ++i) {
		write_u32_le(&bytes, kSwlOffsetTableOffset + (i * 4), offsets[static_cast<size_t>(i)]);
	}

	bytes.reserve(static_cast<int>(off));
	for (int i = 0; i < 4; ++i) {
		bytes.append(mip_bytes[static_cast<size_t>(i)]);
	}

	*out = std::move(bytes);
	return true;
}

[[nodiscard]] bool encode_mip(const QImage& source, const ImageWriteOptions& options, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid MIP output buffer.";
		}
		return false;
	}
	*out = {};

	std::array<QByteArray, 4> mip_bytes;
	std::array<int, 4> mip_w = {};
	std::array<int, 4> mip_h = {};
	QVector<QRgb> palette;
	if (!build_paletted_mips(source, options, &palette, &mip_bytes, &mip_w, &mip_h, error)) {
		return false;
	}

	quint32 off = kMipHeaderSize;
	std::array<quint32, 4> offsets = {};
	for (int i = 0; i < 4; ++i) {
		offsets[static_cast<size_t>(i)] = off;
		off += static_cast<quint32>(mip_bytes[static_cast<size_t>(i)].size());
	}

	QByteArray bytes;
	bytes.resize(kMipHeaderSize);
	bytes.fill('\0');

	const QString name_hint = texture_name_hint(options.texture_name);
	const QByteArray name_latin1 = name_hint.toLatin1();
	const int copy_len = qMin(15, static_cast<int>(name_latin1.size()));
	for (int i = 0; i < copy_len; ++i) {
		bytes[i] = name_latin1[i];
	}

	write_u32_le(&bytes, 16, static_cast<quint32>(mip_w[0]));
	write_u32_le(&bytes, 20, static_cast<quint32>(mip_h[0]));
	for (int i = 0; i < 4; ++i) {
		write_u32_le(&bytes, 24 + (i * 4), offsets[static_cast<size_t>(i)]);
	}

	bytes.reserve(static_cast<int>(off) + (options.embed_palette ? 2 + (256 * 3) + 2 : 0));
	for (int i = 0; i < 4; ++i) {
		bytes.append(mip_bytes[static_cast<size_t>(i)]);
	}

	if (options.embed_palette) {
		append_u16_le(&bytes, 256);
		for (int i = 0; i < 256; ++i) {
			const QRgb c = palette[i];
			bytes.append(static_cast<char>(qRed(c)));
			bytes.append(static_cast<char>(qGreen(c)));
			bytes.append(static_cast<char>(qBlue(c)));
		}
		append_u16_le(&bytes, 0);
	}

	*out = std::move(bytes);
	return true;
}

[[nodiscard]] bool encode_lmp(const QImage& source, const ImageWriteOptions& options, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid LMP output buffer.";
		}
		return false;
	}
	*out = {};

	QuantizedImage quantized;
	if (!quantize_image(source, options.palette, true, options.dither, options.alpha_threshold, &quantized, error)) {
		return false;
	}

	QByteArray bytes;
	bytes.reserve(8 + quantized.indices.size() + (options.embed_palette ? (2 + (256 * 3) + 2) : 0));
	append_u32_le(&bytes, static_cast<quint32>(quantized.width));
	append_u32_le(&bytes, static_cast<quint32>(quantized.height));
	bytes.append(quantized.indices);

	if (options.embed_palette) {
		append_u16_le(&bytes, 256);
		for (int i = 0; i < 256; ++i) {
			const QRgb c = quantized.palette[i];
			bytes.append(static_cast<char>(qRed(c)));
			bytes.append(static_cast<char>(qGreen(c)));
			bytes.append(static_cast<char>(qBlue(c)));
		}
		append_u16_le(&bytes, 0);
	}

	*out = std::move(bytes);
	return true;
}

void append_pcx_rle_row(const uchar* row, int row_len, QByteArray* out) {
	if (!row || row_len <= 0 || !out) {
		return;
	}
	int pos = 0;
	while (pos < row_len) {
		const uchar value = row[pos];
		int run = 1;
		while ((pos + run) < row_len && run < 63 && row[pos + run] == value) {
			++run;
		}

		if (run > 1 || (value & 0xC0u) == 0xC0u) {
			out->append(static_cast<char>(0xC0u | static_cast<uchar>(run)));
			out->append(static_cast<char>(value));
		} else {
			out->append(static_cast<char>(value));
		}
		pos += run;
	}
}

[[nodiscard]] bool encode_pcx(const QImage& source, const ImageWriteOptions& options, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid PCX output buffer.";
		}
		return false;
	}
	*out = {};

	QuantizedImage quantized;
	if (!quantize_image(source, options.palette, true, options.dither, options.alpha_threshold, &quantized, error)) {
		return false;
	}

	if (quantized.width <= 0 || quantized.height <= 0 || quantized.width > 65535 || quantized.height > 65535) {
		if (error) {
			*error = "PCX dimensions are out of range.";
		}
		return false;
	}

	const int bytes_per_line = (quantized.width + 1) & ~1;

	QByteArray bytes;
	bytes.resize(128);
	bytes.fill('\0');

	bytes[0] = static_cast<char>(0x0A);
	bytes[1] = static_cast<char>(5);
	bytes[2] = static_cast<char>(1);
	bytes[3] = static_cast<char>(8);
	write_u16_le(&bytes, 4, 0);
	write_u16_le(&bytes, 6, 0);
	write_u16_le(&bytes, 8, static_cast<quint16>(quantized.width - 1));
	write_u16_le(&bytes, 10, static_cast<quint16>(quantized.height - 1));
	write_u16_le(&bytes, 12, static_cast<quint16>(qMin(65535, quantized.width)));
	write_u16_le(&bytes, 14, static_cast<quint16>(qMin(65535, quantized.height)));

	for (int i = 0; i < 16; ++i) {
		const QRgb c = quantized.palette[i];
		bytes[16 + (i * 3) + 0] = static_cast<char>(qRed(c));
		bytes[16 + (i * 3) + 1] = static_cast<char>(qGreen(c));
		bytes[16 + (i * 3) + 2] = static_cast<char>(qBlue(c));
	}

	bytes[65] = static_cast<char>(1);
	write_u16_le(&bytes, 66, static_cast<quint16>(bytes_per_line));
	write_u16_le(&bytes, 68, static_cast<quint16>(1));

	QByteArray row;
	row.resize(bytes_per_line);
	row.fill('\0');
	for (int y = 0; y < quantized.height; ++y) {
		const uchar* src_row = reinterpret_cast<const uchar*>(quantized.indices.constData()) + (y * quantized.width);
		std::memcpy(row.data(), src_row, static_cast<size_t>(quantized.width));
		if (bytes_per_line > quantized.width) {
			row[bytes_per_line - 1] = 0;
		}
		append_pcx_rle_row(reinterpret_cast<const uchar*>(row.constData()), row.size(), &bytes);
	}

	bytes.append(static_cast<char>(0x0C));
	for (int i = 0; i < 256; ++i) {
		const QRgb c = quantized.palette[i];
		bytes.append(static_cast<char>(qRed(c)));
		bytes.append(static_cast<char>(qGreen(c)));
		bytes.append(static_cast<char>(qBlue(c)));
	}

	*out = std::move(bytes);
	return true;
}

[[nodiscard]] bool encode_ftx(const QImage& source, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid FTX output buffer.";
		}
		return false;
	}
	*out = {};

	const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
	if (rgba.isNull() || rgba.width() <= 0 || rgba.height() <= 0) {
		if (error) {
			*error = "Unable to prepare source image for FTX.";
		}
		return false;
	}

	bool has_alpha = false;
	for (int y = 0; y < rgba.height() && !has_alpha; ++y) {
		const uchar* row = rgba.constScanLine(y);
		for (int x = 0; x < rgba.width(); ++x) {
			if (row[x * 4 + 3] < 255) {
				has_alpha = true;
				break;
			}
		}
	}

	QByteArray bytes;
	bytes.reserve(12 + (rgba.width() * rgba.height() * 4));
	append_u32_le(&bytes, static_cast<quint32>(rgba.width()));
	append_u32_le(&bytes, static_cast<quint32>(rgba.height()));
	append_u32_le(&bytes, has_alpha ? 1u : 0u);

	for (int y = 0; y < rgba.height(); ++y) {
		const uchar* row = rgba.constScanLine(y);
		for (int x = 0; x < rgba.width(); ++x) {
			bytes.append(static_cast<char>(row[x * 4 + 0]));
			bytes.append(static_cast<char>(row[x * 4 + 1]));
			bytes.append(static_cast<char>(row[x * 4 + 2]));
			bytes.append(static_cast<char>(has_alpha ? row[x * 4 + 3] : 255));
		}
	}

	*out = std::move(bytes);
	return true;
}

[[nodiscard]] bool encode_dds(const QImage& source, QByteArray* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Invalid DDS output buffer.";
		}
		return false;
	}
	*out = {};

	const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
	if (argb.isNull() || argb.width() <= 0 || argb.height() <= 0) {
		if (error) {
			*error = "Unable to prepare source image for DDS.";
		}
		return false;
	}

	QByteArray bytes;
	bytes.reserve(4 + kDdsHeaderSize + (argb.width() * argb.height() * 4));
	bytes.append("DDS ", 4);

	QByteArray header;
	header.resize(kDdsHeaderSize);
	header.fill('\0');
	write_u32_le(&header, 0, kDdsHeaderSize);
	write_u32_le(&header, 4, kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPitch | kDdsdPixelFormat);
	write_u32_le(&header, 8, static_cast<quint32>(argb.height()));
	write_u32_le(&header, 12, static_cast<quint32>(argb.width()));
	write_u32_le(&header, 16, static_cast<quint32>(argb.width() * 4));
	write_u32_le(&header, 72, 32u);
	write_u32_le(&header, 76, kDdsPfRgb | kDdsPfAlphaPixels);
	write_u32_le(&header, 80, 0u);
	write_u32_le(&header, 84, 32u);
	write_u32_le(&header, 88, 0x00FF0000u);
	write_u32_le(&header, 92, 0x0000FF00u);
	write_u32_le(&header, 96, 0x000000FFu);
	write_u32_le(&header, 100, 0xFF000000u);
	write_u32_le(&header, 104, kDdsCapsTexture);
	bytes.append(header);

	for (int y = 0; y < argb.height(); ++y) {
		const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
		for (int x = 0; x < argb.width(); ++x) {
			append_u32_le(&bytes, static_cast<quint32>(row[x]));
		}
	}

	*out = std::move(bytes);
	return true;
}

[[nodiscard]] bool write_with_qt_writer(const QImage& source,
                                        const QString& file_path,
                                        const ImageWriteOptions& options,
                                        const QString& normalized_format,
                                        QString* error) {
	if (error) {
		error->clear();
	}
	QByteArray fmt = normalized_format.toLatin1();
	if (normalized_format == "jpg") {
		fmt = "jpeg";
	}

	const QFileInfo info(file_path);
	QDir dir(info.absolutePath());
	if (!dir.exists() && !dir.mkpath(".")) {
		if (error) {
			*error = QString("Unable to create output directory: %1").arg(info.absolutePath());
		}
		return false;
	}

	QImageWriter writer(file_path, fmt);
	if (options.quality >= 0) {
		writer.setQuality(qBound(1, options.quality, 100));
	}
	if (options.compression >= 0) {
		writer.setCompression(options.compression);
	}
	if (!writer.write(source)) {
		if (error) {
			*error = writer.errorString().isEmpty() ? "Unable to write image." : writer.errorString();
		}
		return false;
	}
	return true;
}
}  // namespace

QString normalize_image_write_format(const QString& format) {
	QString out = format.trimmed().toLower();
	if (out.startsWith('.')) {
		out.remove(0, 1);
	}
	if (out == "jpeg") {
		return "jpg";
	}
	if (out == "tif") {
		return "tiff";
	}
	return out;
}

bool image_write_format_is_paletted(const QString& format) {
	const QString normalized = normalize_image_write_format(format);
	static const QSet<QString> kPaletted = {"pcx", "wal", "swl", "mip", "lmp"};
	return kPaletted.contains(normalized);
}

bool image_write_format_supports_embedded_palette(const QString& format) {
	const QString normalized = normalize_image_write_format(format);
	return normalized == "mip" || normalized == "lmp";
}

QStringList supported_image_write_formats() {
	return QStringList{
		"png", "jpg", "bmp", "gif", "tga", "tiff", "pcx", "wal", "swl", "mip", "lmp", "ftx", "dds"
	};
}

bool write_image_file(const QImage& image, const QString& file_path, const ImageWriteOptions& options, QString* error) {
	if (error) {
		error->clear();
	}
	if (image.isNull()) {
		if (error) {
			*error = "Image data is empty.";
		}
		return false;
	}
	if (file_path.trimmed().isEmpty()) {
		if (error) {
			*error = "Output path is empty.";
		}
		return false;
	}

	const QString format = normalize_image_write_format(options.format);
	if (format.isEmpty()) {
		if (error) {
			*error = "Output format is empty.";
		}
		return false;
	}
	if (!format_is_supported(format)) {
		if (error) {
			*error = QString("Unsupported image output format: %1").arg(format);
		}
		return false;
	}

	if (!format_is_custom(format)) {
		return write_with_qt_writer(image, file_path, options, format, error);
	}

	ImageWriteOptions custom = options;
	custom.format = format;
	custom.texture_name = texture_name_hint(custom.texture_name);

	QByteArray encoded;
	bool ok = false;
	if (format == "wal") {
		ok = encode_wal(image, custom, &encoded, error);
	} else if (format == "swl") {
		ok = encode_swl(image, custom, &encoded, error);
	} else if (format == "mip") {
		ok = encode_mip(image, custom, &encoded, error);
	} else if (format == "lmp") {
		ok = encode_lmp(image, custom, &encoded, error);
	} else if (format == "pcx") {
		ok = encode_pcx(image, custom, &encoded, error);
	} else if (format == "ftx") {
		ok = encode_ftx(image, &encoded, error);
	} else if (format == "dds") {
		ok = encode_dds(image, &encoded, error);
	}
	if (!ok) {
		if (error && error->isEmpty()) {
			*error = QString("Unable to encode image as %1.").arg(format);
		}
		return false;
	}

	return write_bytes_file(file_path, encoded, error);
}
