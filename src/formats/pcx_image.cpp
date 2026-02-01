#include "formats/pcx_image.h"

#include <limits>
#include <cstring>

#include <QtGlobal>

namespace {
constexpr int kPcxHeaderSize = 128;

[[nodiscard]] quint16 read_u16le(const uchar* p) {
	return static_cast<quint16>(static_cast<quint16>(p[0]) |
								static_cast<quint16>(static_cast<quint16>(p[1]) << 8));
}

[[nodiscard]] int safe_int(quint16 v) {
	return static_cast<int>(v);
}

[[nodiscard]] bool decode_pcx_rle(const uchar* data,
								 int size,
								 int pos,
								 QByteArray* out,
								 int out_bytes_needed,
								 QString* error) {
	if (error) {
		error->clear();
	}
	if (!out || !data || size <= 0 || pos < 0 || pos > size || out_bytes_needed < 0) {
		if (error) {
			*error = "PCX decode called with invalid arguments.";
		}
		return false;
	}

	out->resize(out_bytes_needed);
	uchar* dst = reinterpret_cast<uchar*>(out->data());
	int out_pos = 0;

	while (out_pos < out_bytes_needed) {
		if (pos >= size) {
			if (error) {
				*error = "PCX image data exceeds file size.";
			}
			return false;
		}
		uchar v = data[pos++];
		int run = 1;
		if ((v & 0xC0) == 0xC0) {
			run = (v & 0x3F);
			if (run <= 0) {
				run = 1;
			}
			if (pos >= size) {
				if (error) {
					*error = "PCX RLE run is truncated.";
				}
				return false;
			}
			v = data[pos++];
		}

		const int to_copy = qMin(run, out_bytes_needed - out_pos);
		if (to_copy > 0) {
			std::memset(dst + out_pos, v, static_cast<size_t>(to_copy));
			out_pos += to_copy;
		}
	}

	return true;
}

[[nodiscard]] QVector<QRgb> palette16_from_header(const uchar* data) {
	QVector<QRgb> pal(16);
	for (int i = 0; i < 16; ++i) {
		const int off = 16 + i * 3;
		const uchar r = data[off + 0];
		const uchar g = data[off + 1];
		const uchar b = data[off + 2];
		pal[i] = qRgba(r, g, b, 255);
	}
	return pal;
}
}  // namespace

bool extract_pcx_palette_256(const QByteArray& bytes, QVector<QRgb>* out_palette, QString* error) {
	if (error) {
		error->clear();
	}
	if (out_palette) {
		out_palette->clear();
	}
	if (bytes.size() < 769) {
		if (error) {
			*error = "PCX file is too small to contain a 256-color palette.";
		}
		return false;
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const int size = bytes.size();
	const int pal_marker = size - 769;
	if (data[pal_marker] != 0x0C) {
		if (error) {
			*error = "PCX 256-color palette marker not found.";
		}
		return false;
	}

	if (!out_palette) {
		return true;
	}

	out_palette->resize(256);
	const int pal_off = pal_marker + 1;
	for (int i = 0; i < 256; ++i) {
		const uchar r = data[pal_off + i * 3 + 0];
		const uchar g = data[pal_off + i * 3 + 1];
		const uchar b = data[pal_off + i * 3 + 2];
		(*out_palette)[i] = qRgba(r, g, b, 255);
	}

	return true;
}

QImage decode_pcx_image(const QByteArray& bytes, QString* error) {
	if (error) {
		error->clear();
	}
	if (bytes.size() < kPcxHeaderSize) {
		if (error) {
			*error = "PCX header too small.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const int size = bytes.size();

	const quint8 manufacturer = data[0];
	const quint8 version = data[1];
	const quint8 encoding = data[2];
	const quint8 bits_per_pixel = data[3];

	const quint16 xmin = read_u16le(data + 4);
	const quint16 ymin = read_u16le(data + 6);
	const quint16 xmax = read_u16le(data + 8);
	const quint16 ymax = read_u16le(data + 10);

	const quint8 planes = data[65];
	const quint16 bytes_per_line_u16 = read_u16le(data + 66);

	if (manufacturer != 0x0A) {
		if (error) {
			*error = "Not a PCX file (invalid manufacturer byte).";
		}
		return {};
	}
	if (encoding != 1) {
		if (error) {
			*error = "Unsupported PCX encoding (expected RLE).";
		}
		return {};
	}
	if (planes == 0) {
		if (error) {
			*error = "Invalid PCX color plane count.";
		}
		return {};
	}

	const int width = safe_int(xmax) - safe_int(xmin) + 1;
	const int height = safe_int(ymax) - safe_int(ymin) + 1;
	if (width <= 0 || height <= 0) {
		if (error) {
			*error = "Invalid PCX dimensions.";
		}
		return {};
	}

	const int bytes_per_line = safe_int(bytes_per_line_u16);
	if (bytes_per_line <= 0 || bytes_per_line > (1 << 20)) {
		if (error) {
			*error = "Invalid PCX bytes-per-line field.";
		}
		return {};
	}

	const quint64 decoded_bytes_needed_u64 =
		static_cast<quint64>(bytes_per_line) *
		static_cast<quint64>(planes) *
		static_cast<quint64>(height);
	if (decoded_bytes_needed_u64 > static_cast<quint64>(std::numeric_limits<int>::max())) {
		if (error) {
			*error = "PCX image is too large.";
		}
		return {};
	}
	const int decoded_bytes_needed = static_cast<int>(decoded_bytes_needed_u64);

	// Determine supported formats.
	const bool is_256_paletted = (bits_per_pixel == 8 && planes == 1);
	const bool is_rgb24 = (bits_per_pixel == 8 && planes == 3);
	const bool is_16_color = (bits_per_pixel == 1 && planes == 4);
	const bool is_mono = (bits_per_pixel == 1 && planes == 1);

	if (!(is_256_paletted || is_rgb24 || is_16_color || is_mono)) {
		if (error) {
			*error = QString("Unsupported PCX format (bpp=%1 planes=%2).").arg(bits_per_pixel).arg(planes);
		}
		return {};
	}

	// Basic sanity checks around scanline padding.
	if (bits_per_pixel == 8) {
		if (bytes_per_line < width) {
			if (error) {
				*error = "PCX bytes-per-line is smaller than image width.";
			}
			return {};
		}
	} else if (bits_per_pixel == 1) {
		if (bytes_per_line * 8 < width) {
			if (error) {
				*error = "PCX bytes-per-line is too small for image width.";
			}
			return {};
		}
	}

	// Decompress scanline data.
	QByteArray decoded;
	QString rle_error;
	if (!decode_pcx_rle(data, size, kPcxHeaderSize, &decoded, decoded_bytes_needed, &rle_error)) {
		if (error) {
			*error = rle_error.isEmpty() ? "Unable to decode PCX RLE data." : rle_error;
		}
		return {};
	}
	const auto* decoded_bytes = reinterpret_cast<const uchar*>(decoded.constData());

	QImage image(width, height, QImage::Format_RGBA8888);
	if (image.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return {};
	}
	uchar* out_bits = image.bits();
	const int out_stride = image.bytesPerLine();

	if (is_256_paletted) {
		QVector<QRgb> palette;
		QString pal_err;
		if (!extract_pcx_palette_256(bytes, &palette, &pal_err) || palette.size() != 256) {
			if (error) {
				*error = pal_err.isEmpty() ? "PCX palette missing or invalid." : pal_err;
			}
			return {};
		}

		for (int y = 0; y < height; ++y) {
			const uchar* src = decoded_bytes + y * bytes_per_line;
			uchar* dst = out_bits + y * out_stride;
			for (int x = 0; x < width; ++x) {
				const uchar idx = src[x];
				const QRgb c = palette[static_cast<int>(idx)];
				dst[x * 4 + 0] = static_cast<uchar>(qRed(c));
				dst[x * 4 + 1] = static_cast<uchar>(qGreen(c));
				dst[x * 4 + 2] = static_cast<uchar>(qBlue(c));
				dst[x * 4 + 3] = 255;
			}
		}
		return image;
	}

	if (is_rgb24) {
		for (int y = 0; y < height; ++y) {
			const uchar* row = decoded_bytes + y * bytes_per_line * 3;
			const uchar* r = row + bytes_per_line * 0;
			const uchar* g = row + bytes_per_line * 1;
			const uchar* b = row + bytes_per_line * 2;
			uchar* dst = out_bits + y * out_stride;
			for (int x = 0; x < width; ++x) {
				dst[x * 4 + 0] = r[x];
				dst[x * 4 + 1] = g[x];
				dst[x * 4 + 2] = b[x];
				dst[x * 4 + 3] = 255;
			}
		}
		return image;
	}

	// 1bpp formats: reconstruct color index from bitplanes.
	const QVector<QRgb> pal16 = palette16_from_header(data);
	const int plane_stride = bytes_per_line;
	for (int y = 0; y < height; ++y) {
		const uchar* row = decoded_bytes + y * bytes_per_line * planes;
		uchar* dst = out_bits + y * out_stride;
		for (int x = 0; x < width; ++x) {
			const int byte_index = x / 8;
			const int bit = 7 - (x % 8);
			int idx = 0;
			for (int p = 0; p < planes; ++p) {
				const uchar* plane = row + p * plane_stride;
				const int bit_val = (plane[byte_index] >> bit) & 1;
				idx |= (bit_val << p);
			}
			if (idx < 0) {
				idx = 0;
			}
			if (idx >= pal16.size()) {
				idx = pal16.size() - 1;
			}
			const QRgb c = pal16[idx];
			dst[x * 4 + 0] = static_cast<uchar>(qRed(c));
			dst[x * 4 + 1] = static_cast<uchar>(qGreen(c));
			dst[x * 4 + 2] = static_cast<uchar>(qBlue(c));
			dst[x * 4 + 3] = 255;
		}
	}

	(void)version;  // reserved for future checks
	return image;
}
