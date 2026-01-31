#include "formats/tga_image.h"

#include <QVector>

namespace {
[[nodiscard]] quint16 read_u16le(const uchar* p) {
	return static_cast<quint16>(static_cast<quint16>(p[0]) |
								static_cast<quint16>(static_cast<quint16>(p[1]) << 8));
}

[[nodiscard]] int safe_int(quint16 v) {
	return static_cast<int>(v);
}

struct Rgba {
	uchar r = 0;
	uchar g = 0;
	uchar b = 0;
	uchar a = 255;
};

[[nodiscard]] uchar expand_5_to_8(int v) {
	return static_cast<uchar>((v * 255 + 15) / 31);
}

[[nodiscard]] Rgba decode_16bpp_5551(quint16 v, bool use_alpha_bit) {
	const int b5 = (v >> 0) & 0x1f;
	const int g5 = (v >> 5) & 0x1f;
	const int r5 = (v >> 10) & 0x1f;
	const bool a1 = (v & 0x8000) != 0;
	Rgba out;
	out.r = expand_5_to_8(r5);
	out.g = expand_5_to_8(g5);
	out.b = expand_5_to_8(b5);
	out.a = use_alpha_bit ? (a1 ? 255 : 0) : 255;
	return out;
}

[[nodiscard]] Rgba decode_palette_entry(const uchar* p, int entry_bits) {
	switch (entry_bits) {
		case 32: {
			return Rgba{p[2], p[1], p[0], p[3]};
		}
		case 24: {
			return Rgba{p[2], p[1], p[0], 255};
		}
		case 16:
		case 15: {
			const quint16 v = read_u16le(p);
			return decode_16bpp_5551(v, false);
		}
		default:
			return Rgba{};
	}
}

[[nodiscard]] bool is_supported_image_type(quint8 image_type) {
	switch (image_type) {
		case 1:   // uncompressed color-mapped
		case 2:   // uncompressed true-color
		case 3:   // uncompressed grayscale
		case 9:   // RLE color-mapped
		case 10:  // RLE true-color
		case 11:  // RLE grayscale
			return true;
		default:
			return false;
	}
}
}  // namespace

QImage decode_tga_image(const QByteArray& bytes, QString* error) {
	if (error) {
		error->clear();
	}
	if (bytes.size() < 18) {
		if (error) {
			*error = "TGA header too small.";
		}
		return {};
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const int size = bytes.size();

	const quint8 id_length = data[0];
	const quint8 color_map_type = data[1];
	const quint8 image_type = data[2];

	const quint16 color_map_first = read_u16le(data + 3);
	const quint16 color_map_length = read_u16le(data + 5);
	const quint8 color_map_entry_bits = data[7];

	const quint16 width_u16 = read_u16le(data + 12);
	const quint16 height_u16 = read_u16le(data + 14);
	const quint8 pixel_depth = data[16];
	const quint8 descriptor = data[17];

	const int width = safe_int(width_u16);
	const int height = safe_int(height_u16);
	if (width <= 0 || height <= 0) {
		if (error) {
			*error = "Invalid TGA dimensions.";
		}
		return {};
	}
	if (!is_supported_image_type(image_type)) {
		if (error) {
			*error = QString("Unsupported TGA image type: %1.").arg(image_type);
		}
		return {};
	}

	const quint64 total_pixels = static_cast<quint64>(width) * static_cast<quint64>(height);
	if (total_pixels > (1ull << 31)) {
		if (error) {
			*error = "TGA image too large.";
		}
		return {};
	}

	int pos = 18;
	if (pos + id_length > size) {
		if (error) {
			*error = "TGA id field exceeds file size.";
		}
		return {};
	}
	pos += id_length;

	QVector<QRgb> palette;
	if (color_map_type == 1) {
		if (color_map_length == 0) {
			if (error) {
				*error = "TGA file declares a color map but it is empty.";
			}
			return {};
		}
		const int entry_bytes = (static_cast<int>(color_map_entry_bits) + 7) / 8;
		if (entry_bytes <= 0 || entry_bytes > 4) {
			if (error) {
				*error = "Unsupported TGA color map entry size.";
			}
			return {};
		}
		const int palette_size = safe_int(color_map_first) + safe_int(color_map_length);
		if (palette_size <= 0 || palette_size > (1 << 20)) {
			if (error) {
				*error = "TGA color map is an invalid size.";
			}
			return {};
		}
		const int palette_bytes = safe_int(color_map_length) * entry_bytes;
		if (pos + palette_bytes > size) {
			if (error) {
				*error = "TGA color map exceeds file size.";
			}
			return {};
		}
		palette.resize(palette_size);
		for (int i = 0; i < safe_int(color_map_length); ++i) {
			const int entry_index = safe_int(color_map_first) + i;
			const Rgba c = decode_palette_entry(data + pos, color_map_entry_bits);
			palette[entry_index] = qRgba(c.r, c.g, c.b, c.a);
			pos += entry_bytes;
		}
	} else if (color_map_type != 0) {
		if (error) {
			*error = "Unsupported TGA color map type.";
		}
		return {};
	}

	const bool rle = (image_type == 9 || image_type == 10 || image_type == 11);
	const bool is_color_mapped = (image_type == 1 || image_type == 9);
	const bool is_true_color = (image_type == 2 || image_type == 10);
	const bool is_grayscale = (image_type == 3 || image_type == 11);

	if (is_color_mapped && color_map_type != 1) {
		if (error) {
			*error = "TGA image is color-mapped but no color map is present.";
		}
		return {};
	}

	const int bytes_per_pixel = (static_cast<int>(pixel_depth) + 7) / 8;
	if (bytes_per_pixel <= 0 || bytes_per_pixel > 4) {
		if (error) {
			*error = "Unsupported TGA pixel depth.";
		}
		return {};
	}

	if (is_true_color && !(pixel_depth == 16 || pixel_depth == 24 || pixel_depth == 32 || pixel_depth == 15)) {
		if (error) {
			*error = "Unsupported TGA true-color pixel depth.";
		}
		return {};
	}
	if (is_grayscale && !(pixel_depth == 8 || pixel_depth == 16)) {
		if (error) {
			*error = "Unsupported TGA grayscale pixel depth.";
		}
		return {};
	}
	if (is_color_mapped && !(pixel_depth == 8 || pixel_depth == 16)) {
		if (error) {
			*error = "Unsupported TGA color-mapped index depth.";
		}
		return {};
	}

	const int alpha_bits = descriptor & 0x0f;
	const bool use_alpha_bit_16 = (alpha_bits >= 1);
	const bool use_alpha_byte_32 = (alpha_bits >= 1);
	const bool use_alpha_byte_gray16 = (alpha_bits >= 1);

	QImage image(width, height, QImage::Format_RGBA8888);
	if (image.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return {};
	}

	uchar* out_bits = image.bits();
	const int out_stride = image.bytesPerLine();

	const bool origin_right = (descriptor & 0x10) != 0;
	const bool origin_top = (descriptor & 0x20) != 0;

	auto set_pixel = [&](quint64 file_index, const Rgba& c) {
		const int file_x = static_cast<int>(file_index % static_cast<quint64>(width));
		const int file_y = static_cast<int>(file_index / static_cast<quint64>(width));
		const int x = origin_right ? (width - 1 - file_x) : file_x;
		const int y = origin_top ? file_y : (height - 1 - file_y);
		uchar* dst = out_bits + y * out_stride + x * 4;
		dst[0] = c.r;
		dst[1] = c.g;
		dst[2] = c.b;
		dst[3] = c.a;
	};

	auto read_pixel = [&](int& inout_pos, Rgba* out) -> bool {
		if (!out) {
			return false;
		}
		if (inout_pos + bytes_per_pixel > size) {
			return false;
		}
		const uchar* p = data + inout_pos;
		inout_pos += bytes_per_pixel;

		if (is_true_color) {
			if (bytes_per_pixel == 4) {
				const uchar a = use_alpha_byte_32 ? p[3] : 255;
				*out = Rgba{p[2], p[1], p[0], a};
				return true;
			}
			if (bytes_per_pixel == 3) {
				*out = Rgba{p[2], p[1], p[0], 255};
				return true;
			}
			if (bytes_per_pixel == 2) {
				const quint16 v = read_u16le(p);
				*out = decode_16bpp_5551(v, use_alpha_bit_16);
				return true;
			}
			return false;
		}

		if (is_grayscale) {
			if (bytes_per_pixel == 1) {
				const uchar g = p[0];
				*out = Rgba{g, g, g, 255};
				return true;
			}
			if (bytes_per_pixel == 2) {
				const uchar g = p[0];
				const uchar a = use_alpha_byte_gray16 ? p[1] : 255;
				*out = Rgba{g, g, g, a};
				return true;
			}
			return false;
		}

		if (is_color_mapped) {
			quint32 idx = 0;
			if (bytes_per_pixel == 1) {
				idx = p[0];
			} else if (bytes_per_pixel == 2) {
				idx = read_u16le(p);
			} else {
				return false;
			}
			if (idx >= static_cast<quint32>(palette.size())) {
				return false;
			}
			const QRgb c = palette[static_cast<int>(idx)];
			*out = Rgba{static_cast<uchar>(qRed(c)),
						static_cast<uchar>(qGreen(c)),
						static_cast<uchar>(qBlue(c)),
						static_cast<uchar>(qAlpha(c))};
			return true;
		}

		return false;
	};

	quint64 pixel_index = 0;
	if (!rle) {
		for (; pixel_index < total_pixels; ++pixel_index) {
			Rgba c;
			if (!read_pixel(pos, &c)) {
				if (error) {
					*error = "TGA image data exceeds file size.";
				}
				return {};
			}
			set_pixel(pixel_index, c);
		}
		return image;
	}

	while (pixel_index < total_pixels) {
		if (pos >= size) {
			if (error) {
				*error = "TGA RLE data exceeds file size.";
			}
			return {};
		}
		const quint8 header = data[pos++];
		const int count = (header & 0x7f) + 1;
		const bool rle_packet = (header & 0x80) != 0;

		if (rle_packet) {
			Rgba c;
			if (!read_pixel(pos, &c)) {
				if (error) {
					*error = "TGA RLE pixel exceeds file size.";
				}
				return {};
			}
			for (int i = 0; i < count && pixel_index < total_pixels; ++i, ++pixel_index) {
				set_pixel(pixel_index, c);
			}
		} else {
			for (int i = 0; i < count && pixel_index < total_pixels; ++i, ++pixel_index) {
				Rgba c;
				if (!read_pixel(pos, &c)) {
					if (error) {
						*error = "TGA RLE pixel exceeds file size.";
					}
					return {};
				}
				set_pixel(pixel_index, c);
			}
		}
	}

	return image;
}

