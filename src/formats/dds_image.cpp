#include "formats/dds_image.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include <QtGlobal>

namespace {
constexpr quint32 kDdsMagic = 0x20534444u;  // "DDS "
constexpr int kDdsHeaderSize = 124;
constexpr int kDdsPixelFormatSize = 32;
constexpr int kDdsMinFileSize = 4 + kDdsHeaderSize;

constexpr quint32 kDdpfAlphaPixels = 0x00000001u;
constexpr quint32 kDdpfFourCc = 0x00000004u;
constexpr quint32 kDdpfRgb = 0x00000040u;
constexpr quint32 kDdpfLuminance = 0x00020000u;

constexpr quint32 kDdsdPitch = 0x00000008u;

[[nodiscard]] constexpr quint32 fourcc(char a, char b, char c, char d) {
	return (static_cast<quint32>(static_cast<quint8>(a)) |
			(static_cast<quint32>(static_cast<quint8>(b)) << 8) |
			(static_cast<quint32>(static_cast<quint8>(c)) << 16) |
			(static_cast<quint32>(static_cast<quint8>(d)) << 24));
}

constexpr quint32 kFourCcDxt1 = fourcc('D', 'X', 'T', '1');
constexpr quint32 kFourCcDxt3 = fourcc('D', 'X', 'T', '3');
constexpr quint32 kFourCcDxt5 = fourcc('D', 'X', 'T', '5');
constexpr quint32 kFourCcAti1 = fourcc('A', 'T', 'I', '1');
constexpr quint32 kFourCcAti2 = fourcc('A', 'T', 'I', '2');
constexpr quint32 kFourCcBc4u = fourcc('B', 'C', '4', 'U');
constexpr quint32 kFourCcBc4s = fourcc('B', 'C', '4', 'S');
constexpr quint32 kFourCcBc5u = fourcc('B', 'C', '5', 'U');
constexpr quint32 kFourCcBc5s = fourcc('B', 'C', '5', 'S');
constexpr quint32 kFourCcDx10 = fourcc('D', 'X', '1', '0');

// DXGI formats (DDS_HEADER_DXT10).
constexpr quint32 kDxgiR8G8B8A8Unorm = 28;
constexpr quint32 kDxgiR8Unorm = 61;
constexpr quint32 kDxgiR8G8Unorm = 49;
constexpr quint32 kDxgiB8G8R8A8Unorm = 87;
constexpr quint32 kDxgiB8G8R8X8Unorm = 88;
constexpr quint32 kDxgiBc1Unorm = 71;
constexpr quint32 kDxgiBc1UnormSrgb = 72;
constexpr quint32 kDxgiBc2Unorm = 74;
constexpr quint32 kDxgiBc2UnormSrgb = 75;
constexpr quint32 kDxgiBc3Unorm = 77;
constexpr quint32 kDxgiBc3UnormSrgb = 78;
constexpr quint32 kDxgiBc4Unorm = 80;
constexpr quint32 kDxgiBc4Snorm = 81;
constexpr quint32 kDxgiBc5Unorm = 83;
constexpr quint32 kDxgiBc5Snorm = 84;

[[nodiscard]] quint16 read_u16le(const uchar* p) {
	return static_cast<quint16>(static_cast<quint16>(p[0]) |
								static_cast<quint16>(static_cast<quint16>(p[1]) << 8));
}

[[nodiscard]] quint32 read_u32le(const uchar* p) {
	return (static_cast<quint32>(p[0]) |
			(static_cast<quint32>(p[1]) << 8) |
			(static_cast<quint32>(p[2]) << 16) |
			(static_cast<quint32>(p[3]) << 24));
}

[[nodiscard]] quint64 read_u64le(const uchar* p) {
	const quint64 lo = read_u32le(p);
	const quint64 hi = read_u32le(p + 4);
	return lo | (hi << 32);
}

[[nodiscard]] uchar expand_5_to_8(int v) {
	return static_cast<uchar>((v * 255 + 15) / 31);
}

[[nodiscard]] uchar expand_6_to_8(int v) {
	return static_cast<uchar>((v * 255 + 31) / 63);
}

struct Rgba {
	uchar r = 0;
	uchar g = 0;
	uchar b = 0;
	uchar a = 255;
};

[[nodiscard]] Rgba decode_rgb565(quint16 c) {
	const int b5 = (c >> 0) & 0x1f;
	const int g6 = (c >> 5) & 0x3f;
	const int r5 = (c >> 11) & 0x1f;
	return Rgba{expand_5_to_8(r5), expand_6_to_8(g6), expand_5_to_8(b5), 255};
}

[[nodiscard]] uchar lerp_u8(int a, int b, int num, int den) {
	const int v = (a * num + b * (den - num) + (den / 2)) / den;
	return static_cast<uchar>(std::clamp(v, 0, 255));
}

void decode_bc1_color_table(quint16 c0, quint16 c1, bool allow_1bit_alpha, Rgba out[4]) {
	const Rgba p0 = decode_rgb565(c0);
	const Rgba p1 = decode_rgb565(c1);
	out[0] = p0;
	out[1] = p1;

	if (allow_1bit_alpha && c0 <= c1) {
		out[2] = Rgba{
			static_cast<uchar>((static_cast<int>(p0.r) + static_cast<int>(p1.r) + 1) / 2),
			static_cast<uchar>((static_cast<int>(p0.g) + static_cast<int>(p1.g) + 1) / 2),
			static_cast<uchar>((static_cast<int>(p0.b) + static_cast<int>(p1.b) + 1) / 2),
			255};
		out[3] = Rgba{0, 0, 0, 0};
		return;
	}

	out[2] = Rgba{lerp_u8(p1.r, p0.r, 1, 3), lerp_u8(p1.g, p0.g, 1, 3), lerp_u8(p1.b, p0.b, 1, 3), 255};
	out[3] = Rgba{lerp_u8(p1.r, p0.r, 2, 3), lerp_u8(p1.g, p0.g, 2, 3), lerp_u8(p1.b, p0.b, 2, 3), 255};
}

void decode_bc4_table_unorm(uchar a0, uchar a1, uchar out[8]) {
	out[0] = a0;
	out[1] = a1;
	if (a0 > a1) {
		out[2] = lerp_u8(a1, a0, 1, 7);
		out[3] = lerp_u8(a1, a0, 2, 7);
		out[4] = lerp_u8(a1, a0, 3, 7);
		out[5] = lerp_u8(a1, a0, 4, 7);
		out[6] = lerp_u8(a1, a0, 5, 7);
		out[7] = lerp_u8(a1, a0, 6, 7);
		return;
	}

	out[2] = lerp_u8(a1, a0, 1, 5);
	out[3] = lerp_u8(a1, a0, 2, 5);
	out[4] = lerp_u8(a1, a0, 3, 5);
	out[5] = lerp_u8(a1, a0, 4, 5);
	out[6] = 0;
	out[7] = 255;
}

void decode_bc4_table_snorm(qint8 a0, qint8 a1, qint16 out[8]) {
	out[0] = a0;
	out[1] = a1;
	if (a0 > a1) {
		for (int i = 2; i < 8; ++i) {
			const int num = 7 - (i - 1);
			const int den = 7;
			const int v = (static_cast<int>(a0) * num + static_cast<int>(a1) * (den - num) + (den / 2)) / den;
			out[i] = static_cast<qint16>(std::clamp(v, -128, 127));
		}
		return;
	}

	for (int i = 2; i < 6; ++i) {
		const int num = 5 - (i - 1);
		const int den = 5;
		const int v = (static_cast<int>(a0) * num + static_cast<int>(a1) * (den - num) + (den / 2)) / den;
		out[i] = static_cast<qint16>(std::clamp(v, -128, 127));
	}
	out[6] = -128;
	out[7] = 127;
}

[[nodiscard]] float snorm8_to_float(qint16 v) {
	const float f = static_cast<float>(v) / 127.0f;
	return std::clamp(f, -1.0f, 1.0f);
}

[[nodiscard]] uchar float01_to_u8(float f) {
	const float c = std::clamp(f, 0.0f, 1.0f);
	return static_cast<uchar>(std::clamp(static_cast<int>(c * 255.0f + 0.5f), 0, 255));
}

[[nodiscard]] uchar float11_to_u8(float f) {
	return float01_to_u8(f * 0.5f + 0.5f);
}

enum class DdsFormat {
	Unknown,
	UncompressedMasks,
	Bc1,
	Bc2,
	Bc3,
	Bc4Unorm,
	Bc4Snorm,
	Bc5Unorm,
	Bc5Snorm,
};

struct DdsInfo {
	int width = 0;
	int height = 0;
	DdsFormat format = DdsFormat::Unknown;
	int data_offset = 0;
	quint32 pitch_bytes = 0;  // for uncompressed formats
	quint32 rgb_bit_count = 0;
	quint32 r_mask = 0;
	quint32 g_mask = 0;
	quint32 b_mask = 0;
	quint32 a_mask = 0;
};

[[nodiscard]] bool parse_dds_header(const QByteArray& bytes, DdsInfo* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	if (bytes.size() < kDdsMinFileSize) {
		if (error) {
			*error = "DDS header too small.";
		}
		return false;
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const int size = bytes.size();

	const quint32 magic = read_u32le(data);
	if (magic != kDdsMagic) {
		if (error) {
			*error = "Not a DDS file (missing DDS magic).";
		}
		return false;
	}

	const quint32 header_size = read_u32le(data + 4);
	if (header_size != kDdsHeaderSize) {
		if (error) {
			*error = "DDS header size is invalid.";
		}
		return false;
	}

	const quint32 header_flags = read_u32le(data + 8);
	const quint32 height_u32 = read_u32le(data + 12);
	const quint32 width_u32 = read_u32le(data + 16);
	const quint32 pitch_or_linear = read_u32le(data + 20);
	if (width_u32 == 0 || height_u32 == 0) {
		if (error) {
			*error = "DDS dimensions are invalid.";
		}
		return false;
	}

	constexpr quint32 kMaxDim = 16384;
	if (width_u32 > kMaxDim || height_u32 > kMaxDim) {
		if (error) {
			*error = "DDS dimensions are unreasonably large.";
		}
		return false;
	}

	const quint32 pf_size = read_u32le(data + 4 + 72);
	if (pf_size != kDdsPixelFormatSize) {
		if (error) {
			*error = "DDS pixel format size is invalid.";
		}
		return false;
	}

	const quint32 pf_flags = read_u32le(data + 4 + 76);
	const quint32 pf_fourcc = read_u32le(data + 4 + 80);
	quint32 rgb_bit_count = read_u32le(data + 4 + 84);
	quint32 r_mask = read_u32le(data + 4 + 88);
	quint32 g_mask = read_u32le(data + 4 + 92);
	quint32 b_mask = read_u32le(data + 4 + 96);
	quint32 a_mask = read_u32le(data + 4 + 100);

	int offset = 4 + kDdsHeaderSize;
	DdsFormat fmt = DdsFormat::Unknown;

	auto map_dx10 = [&](quint32 dxgi) -> DdsFormat {
		switch (dxgi) {
			case kDxgiBc1Unorm:
			case kDxgiBc1UnormSrgb:
				return DdsFormat::Bc1;
			case kDxgiBc2Unorm:
			case kDxgiBc2UnormSrgb:
				return DdsFormat::Bc2;
			case kDxgiBc3Unorm:
			case kDxgiBc3UnormSrgb:
				return DdsFormat::Bc3;
			case kDxgiBc4Unorm:
				return DdsFormat::Bc4Unorm;
			case kDxgiBc4Snorm:
				return DdsFormat::Bc4Snorm;
			case kDxgiBc5Unorm:
				return DdsFormat::Bc5Unorm;
			case kDxgiBc5Snorm:
				return DdsFormat::Bc5Snorm;
			case kDxgiR8G8B8A8Unorm:
			case kDxgiB8G8R8A8Unorm:
			case kDxgiB8G8R8X8Unorm:
			case kDxgiR8Unorm:
			case kDxgiR8G8Unorm:
				return DdsFormat::UncompressedMasks;
			default:
				break;
		}
		return DdsFormat::Unknown;
	};

	if ((pf_flags & kDdpfFourCc) != 0) {
		if (pf_fourcc == kFourCcDx10) {
			if (size < offset + 20) {
				if (error) {
					*error = "DDS DX10 header is truncated.";
				}
				return false;
			}
			const quint32 dxgi_format = read_u32le(data + offset + 0);
			fmt = map_dx10(dxgi_format);
			offset += 20;

			// For common uncompressed DX10 formats, provide masks if the legacy header doesn't.
			if (fmt == DdsFormat::UncompressedMasks) {
				switch (dxgi_format) {
					case kDxgiR8G8B8A8Unorm:
						rgb_bit_count = 32;
						r_mask = 0x000000FFu;
						g_mask = 0x0000FF00u;
						b_mask = 0x00FF0000u;
						a_mask = 0xFF000000u;
						break;
					case kDxgiB8G8R8A8Unorm:
						rgb_bit_count = 32;
						b_mask = 0x000000FFu;
						g_mask = 0x0000FF00u;
						r_mask = 0x00FF0000u;
						a_mask = 0xFF000000u;
						break;
					case kDxgiB8G8R8X8Unorm:
						rgb_bit_count = 32;
						b_mask = 0x000000FFu;
						g_mask = 0x0000FF00u;
						r_mask = 0x00FF0000u;
						a_mask = 0x00000000u;
						break;
					case kDxgiR8Unorm:
						rgb_bit_count = 8;
						r_mask = 0x000000FFu;
						g_mask = 0;
						b_mask = 0;
						a_mask = 0;
						break;
					case kDxgiR8G8Unorm:
						rgb_bit_count = 16;
						r_mask = 0x000000FFu;
						g_mask = 0x0000FF00u;
						b_mask = 0;
						a_mask = 0;
						break;
					default:
						break;
				}
			}
		} else if (pf_fourcc == kFourCcDxt1) {
			fmt = DdsFormat::Bc1;
		} else if (pf_fourcc == kFourCcDxt3) {
			fmt = DdsFormat::Bc2;
		} else if (pf_fourcc == kFourCcDxt5) {
			fmt = DdsFormat::Bc3;
		} else if (pf_fourcc == kFourCcAti1 || pf_fourcc == kFourCcBc4u) {
			fmt = DdsFormat::Bc4Unorm;
		} else if (pf_fourcc == kFourCcBc4s) {
			fmt = DdsFormat::Bc4Snorm;
		} else if (pf_fourcc == kFourCcAti2 || pf_fourcc == kFourCcBc5u) {
			fmt = DdsFormat::Bc5Unorm;
		} else if (pf_fourcc == kFourCcBc5s) {
			fmt = DdsFormat::Bc5Snorm;
		} else {
			fmt = DdsFormat::Unknown;
		}
	} else if ((pf_flags & (kDdpfRgb | kDdpfLuminance | kDdpfAlphaPixels)) != 0) {
		fmt = DdsFormat::UncompressedMasks;
	} else {
		fmt = DdsFormat::Unknown;
	}

	if (fmt == DdsFormat::Unknown) {
		if (error) {
			*error = "Unsupported DDS pixel format.";
		}
		return false;
	}

	if (offset < 0 || offset > size) {
		if (error) {
			*error = "DDS data offset is invalid.";
		}
		return false;
	}

	DdsInfo info;
	info.width = static_cast<int>(width_u32);
	info.height = static_cast<int>(height_u32);
	info.format = fmt;
	info.data_offset = offset;
	info.pitch_bytes = ((header_flags & kDdsdPitch) != 0) ? pitch_or_linear : 0u;
	info.rgb_bit_count = rgb_bit_count;
	info.r_mask = r_mask;
	info.g_mask = g_mask;
	info.b_mask = b_mask;
	info.a_mask = a_mask;

	if (out) {
		*out = info;
	}
	return true;
}

[[nodiscard]] int bits_in_mask(quint32 mask) {
	if (mask == 0) {
		return 0;
	}
	const int shift = std::countr_zero(mask);
	const quint32 shifted = mask >> shift;
	int bits = 0;
	while (((shifted >> bits) & 1u) != 0u && bits < 32) {
		++bits;
	}
	return bits;
}

[[nodiscard]] int shift_for_mask(quint32 mask) {
	return mask ? static_cast<int>(std::countr_zero(mask)) : 0;
}

[[nodiscard]] uchar extract_masked_u8(quint32 pixel, quint32 mask) {
	if (mask == 0) {
		return 0;
	}
	const int shift = shift_for_mask(mask);
	const int bits = bits_in_mask(mask);
	if (bits <= 0) {
		return 0;
	}
	const quint32 v = (pixel & mask) >> shift;
	const quint32 maxv = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
	if (maxv == 0) {
		return 0;
	}
	const quint32 out = (v * 255u + (maxv / 2u)) / maxv;
	return static_cast<uchar>(std::clamp<int>(static_cast<int>(out), 0, 255));
}

[[nodiscard]] bool decode_uncompressed_masks(const QByteArray& bytes, const DdsInfo& info, QImage* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	const int w = info.width;
	const int h = info.height;
	if (w <= 0 || h <= 0) {
		if (error) {
			*error = "DDS dimensions are invalid.";
		}
		return false;
	}

	const quint32 bit_count = info.rgb_bit_count;
	if (bit_count == 0 || bit_count > 32) {
		if (error) {
			*error = "Unsupported DDS bit depth.";
		}
		return false;
	}
	const int bytes_per_pixel = static_cast<int>((bit_count + 7) / 8);
	if (bytes_per_pixel <= 0 || bytes_per_pixel > 4) {
		if (error) {
			*error = "Unsupported DDS pixel size.";
		}
		return false;
	}

	const quint32 pitch = (info.pitch_bytes != 0) ? info.pitch_bytes : static_cast<quint32>(w * bytes_per_pixel);
	if (pitch < static_cast<quint32>(w * bytes_per_pixel) || pitch > (1u << 30)) {
		if (error) {
			*error = "DDS pitch is invalid.";
		}
		return false;
	}

	const quint64 need_bytes = (h <= 0)
								? 0ull
								: (static_cast<quint64>(h - 1) * static_cast<quint64>(pitch) +
								   static_cast<quint64>(w * bytes_per_pixel));
	const quint64 start = static_cast<quint64>(info.data_offset);
	if (start + need_bytes > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = "DDS pixel data exceeds file size.";
		}
		return false;
	}

	QImage img(w, h, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return false;
	}

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData()) + info.data_offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();

	const bool luminance = (info.g_mask == 0 && info.b_mask == 0 && info.r_mask != 0);

	for (int y = 0; y < h; ++y) {
		uchar* row = dst_bits + y * dst_stride;
		for (int x = 0; x < w; ++x) {
			quint32 px = 0;
			const quint64 off = static_cast<quint64>(y) * static_cast<quint64>(pitch) +
								static_cast<quint64>(x * bytes_per_pixel);
			for (int i = 0; i < bytes_per_pixel; ++i) {
				px |= static_cast<quint32>(data[off + static_cast<quint64>(i)]) << (8 * i);
			}

			uchar r = extract_masked_u8(px, info.r_mask);
			uchar g = extract_masked_u8(px, info.g_mask);
			uchar b = extract_masked_u8(px, info.b_mask);
			uchar a = (info.a_mask != 0) ? extract_masked_u8(px, info.a_mask) : 255;
			if (luminance) {
				g = r;
				b = r;
			}

			uchar* dst = row + x * 4;
			dst[0] = r;
			dst[1] = g;
			dst[2] = b;
			dst[3] = a;
		}
	}

	if (out) {
		*out = std::move(img);
	}
	return true;
}

[[nodiscard]] bool decode_bc1(const QByteArray& bytes, const DdsInfo& info, QImage* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	const int w = info.width;
	const int h = info.height;
	const int block_w = (w + 3) / 4;
	const int block_h = (h + 3) / 4;

	const quint64 blocks = static_cast<quint64>(block_w) * static_cast<quint64>(block_h);
	const quint64 need = blocks * 8ull;
	const quint64 start = static_cast<quint64>(info.data_offset);
	if (start + need > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = "DDS BC1 data exceeds file size.";
		}
		return false;
	}

	QImage img(w, h, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return false;
	}

	const auto* src = reinterpret_cast<const uchar*>(bytes.constData()) + info.data_offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();

	auto set_px = [&](int x, int y, const Rgba& c) {
		if (x < 0 || y < 0 || x >= w || y >= h) {
			return;
		}
		uchar* d = dst_bits + y * dst_stride + x * 4;
		d[0] = c.r;
		d[1] = c.g;
		d[2] = c.b;
		d[3] = c.a;
	};

	for (int by = 0; by < block_h; ++by) {
		for (int bx = 0; bx < block_w; ++bx) {
			const quint64 block_index = static_cast<quint64>(by) * static_cast<quint64>(block_w) + static_cast<quint64>(bx);
			const uchar* b = src + block_index * 8;

			const quint16 c0 = read_u16le(b + 0);
			const quint16 c1 = read_u16le(b + 2);
			const quint32 idx = read_u32le(b + 4);

			Rgba table[4];
			decode_bc1_color_table(c0, c1, true, table);

			for (int i = 0; i < 16; ++i) {
				const int sel = static_cast<int>((idx >> (2 * i)) & 0x3u);
				const int x = bx * 4 + (i % 4);
				const int y = by * 4 + (i / 4);
				set_px(x, y, table[sel]);
			}
		}
	}

	if (out) {
		*out = std::move(img);
	}
	return true;
}

[[nodiscard]] bool decode_bc2(const QByteArray& bytes, const DdsInfo& info, QImage* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	const int w = info.width;
	const int h = info.height;
	const int block_w = (w + 3) / 4;
	const int block_h = (h + 3) / 4;

	const quint64 blocks = static_cast<quint64>(block_w) * static_cast<quint64>(block_h);
	const quint64 need = blocks * 16ull;
	const quint64 start = static_cast<quint64>(info.data_offset);
	if (start + need > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = "DDS BC2 data exceeds file size.";
		}
		return false;
	}

	QImage img(w, h, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return false;
	}

	const auto* src = reinterpret_cast<const uchar*>(bytes.constData()) + info.data_offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();

	auto set_px = [&](int x, int y, const Rgba& c) {
		if (x < 0 || y < 0 || x >= w || y >= h) {
			return;
		}
		uchar* d = dst_bits + y * dst_stride + x * 4;
		d[0] = c.r;
		d[1] = c.g;
		d[2] = c.b;
		d[3] = c.a;
	};

	for (int by = 0; by < block_h; ++by) {
		for (int bx = 0; bx < block_w; ++bx) {
			const quint64 block_index = static_cast<quint64>(by) * static_cast<quint64>(block_w) + static_cast<quint64>(bx);
			const uchar* b = src + block_index * 16;

			const quint64 alpha64 = read_u64le(b + 0);
			const quint16 c0 = read_u16le(b + 8);
			const quint16 c1 = read_u16le(b + 10);
			const quint32 idx = read_u32le(b + 12);

			Rgba table[4];
			decode_bc1_color_table(c0, c1, false, table);  // DXT3 uses 4-color mode.

			for (int i = 0; i < 16; ++i) {
				const int sel = static_cast<int>((idx >> (2 * i)) & 0x3u);
				const int a4 = static_cast<int>((alpha64 >> (4 * i)) & 0xFu);
				Rgba c = table[sel];
				c.a = static_cast<uchar>(a4 * 17);
				const int x = bx * 4 + (i % 4);
				const int y = by * 4 + (i / 4);
				set_px(x, y, c);
			}
		}
	}

	if (out) {
		*out = std::move(img);
	}
	return true;
}

[[nodiscard]] bool decode_bc3(const QByteArray& bytes, const DdsInfo& info, QImage* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	const int w = info.width;
	const int h = info.height;
	const int block_w = (w + 3) / 4;
	const int block_h = (h + 3) / 4;

	const quint64 blocks = static_cast<quint64>(block_w) * static_cast<quint64>(block_h);
	const quint64 need = blocks * 16ull;
	const quint64 start = static_cast<quint64>(info.data_offset);
	if (start + need > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = "DDS BC3 data exceeds file size.";
		}
		return false;
	}

	QImage img(w, h, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return false;
	}

	const auto* src = reinterpret_cast<const uchar*>(bytes.constData()) + info.data_offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();

	auto set_px = [&](int x, int y, const Rgba& c) {
		if (x < 0 || y < 0 || x >= w || y >= h) {
			return;
		}
		uchar* d = dst_bits + y * dst_stride + x * 4;
		d[0] = c.r;
		d[1] = c.g;
		d[2] = c.b;
		d[3] = c.a;
	};

	for (int by = 0; by < block_h; ++by) {
		for (int bx = 0; bx < block_w; ++bx) {
			const quint64 block_index = static_cast<quint64>(by) * static_cast<quint64>(block_w) + static_cast<quint64>(bx);
			const uchar* b = src + block_index * 16;

			const uchar a0 = b[0];
			const uchar a1 = b[1];
			uchar a_table[8];
			decode_bc4_table_unorm(a0, a1, a_table);
			quint64 a_bits = 0;
			for (int i = 0; i < 6; ++i) {
				a_bits |= static_cast<quint64>(b[2 + i]) << (8 * i);
			}

			const quint16 c0 = read_u16le(b + 8);
			const quint16 c1 = read_u16le(b + 10);
			const quint32 idx = read_u32le(b + 12);

			Rgba table[4];
			decode_bc1_color_table(c0, c1, false, table);  // DXT5 uses 4-color mode.

			for (int i = 0; i < 16; ++i) {
				const int sel = static_cast<int>((idx >> (2 * i)) & 0x3u);
				const int a_sel = static_cast<int>((a_bits >> (3 * i)) & 0x7u);
				Rgba c = table[sel];
				c.a = a_table[a_sel];
				const int x = bx * 4 + (i % 4);
				const int y = by * 4 + (i / 4);
				set_px(x, y, c);
			}
		}
	}

	if (out) {
		*out = std::move(img);
	}
	return true;
}

template <typename DecodeSampleFn>
void decode_bc4_generic_block(const uchar* b, bool snorm, uchar out[16], DecodeSampleFn&& decode_sample) {
	if (!snorm) {
		const uchar a0 = b[0];
		const uchar a1 = b[1];
		uchar table[8];
		decode_bc4_table_unorm(a0, a1, table);

		quint64 bits = 0;
		for (int i = 0; i < 6; ++i) {
			bits |= static_cast<quint64>(b[2 + i]) << (8 * i);
		}
		for (int i = 0; i < 16; ++i) {
			const int sel = static_cast<int>((bits >> (3 * i)) & 0x7u);
			out[i] = decode_sample(table[sel], false);
		}
		return;
	}

	const qint8 a0 = static_cast<qint8>(b[0]);
	const qint8 a1 = static_cast<qint8>(b[1]);
	qint16 table[8];
	decode_bc4_table_snorm(a0, a1, table);

	quint64 bits = 0;
	for (int i = 0; i < 6; ++i) {
		bits |= static_cast<quint64>(b[2 + i]) << (8 * i);
	}
	for (int i = 0; i < 16; ++i) {
		const int sel = static_cast<int>((bits >> (3 * i)) & 0x7u);
		out[i] = decode_sample(table[sel], true);
	}
}

[[nodiscard]] bool decode_bc4(const QByteArray& bytes, const DdsInfo& info, bool snorm, QImage* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	const int w = info.width;
	const int h = info.height;
	const int block_w = (w + 3) / 4;
	const int block_h = (h + 3) / 4;

	const quint64 blocks = static_cast<quint64>(block_w) * static_cast<quint64>(block_h);
	const quint64 need = blocks * 8ull;
	const quint64 start = static_cast<quint64>(info.data_offset);
	if (start + need > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = "DDS BC4 data exceeds file size.";
		}
		return false;
	}

	QImage img(w, h, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return false;
	}

	const auto* src = reinterpret_cast<const uchar*>(bytes.constData()) + info.data_offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();

	auto set_px = [&](int x, int y, uchar v) {
		if (x < 0 || y < 0 || x >= w || y >= h) {
			return;
		}
		uchar* d = dst_bits + y * dst_stride + x * 4;
		d[0] = v;
		d[1] = v;
		d[2] = v;
		d[3] = 255;
	};

	const auto to_u8 = [&](auto sample, bool is_snorm) -> uchar {
		if (!is_snorm) {
			return static_cast<uchar>(sample);
		}
		const float f = snorm8_to_float(sample);
		return float11_to_u8(f);
	};

	for (int by = 0; by < block_h; ++by) {
		for (int bx = 0; bx < block_w; ++bx) {
			const quint64 block_index = static_cast<quint64>(by) * static_cast<quint64>(block_w) + static_cast<quint64>(bx);
			const uchar* b = src + block_index * 8;

			uchar block_vals[16]{};
			decode_bc4_generic_block(b, snorm, block_vals, to_u8);

			for (int i = 0; i < 16; ++i) {
				const int x = bx * 4 + (i % 4);
				const int y = by * 4 + (i / 4);
				set_px(x, y, block_vals[i]);
			}
		}
	}

	if (out) {
		*out = std::move(img);
	}
	return true;
}

[[nodiscard]] bool decode_bc5(const QByteArray& bytes, const DdsInfo& info, bool snorm, QImage* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		*out = {};
	}
	const int w = info.width;
	const int h = info.height;
	const int block_w = (w + 3) / 4;
	const int block_h = (h + 3) / 4;

	const quint64 blocks = static_cast<quint64>(block_w) * static_cast<quint64>(block_h);
	const quint64 need = blocks * 16ull;
	const quint64 start = static_cast<quint64>(info.data_offset);
	if (start + need > static_cast<quint64>(bytes.size())) {
		if (error) {
			*error = "DDS BC5 data exceeds file size.";
		}
		return false;
	}

	QImage img(w, h, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate image.";
		}
		return false;
	}

	const auto* src = reinterpret_cast<const uchar*>(bytes.constData()) + info.data_offset;
	uchar* dst_bits = img.bits();
	const int dst_stride = img.bytesPerLine();

	auto set_px = [&](int x, int y, uchar r, uchar g, uchar b) {
		if (x < 0 || y < 0 || x >= w || y >= h) {
			return;
		}
		uchar* d = dst_bits + y * dst_stride + x * 4;
		d[0] = r;
		d[1] = g;
		d[2] = b;
		d[3] = 255;
	};

	const auto decode_channel_u8 = [&](auto sample, bool is_snorm) -> uchar {
		if (!is_snorm) {
			return static_cast<uchar>(sample);
		}
		const float f = snorm8_to_float(sample);
		return float11_to_u8(f);
	};

	for (int by = 0; by < block_h; ++by) {
		for (int bx = 0; bx < block_w; ++bx) {
			const quint64 block_index = static_cast<quint64>(by) * static_cast<quint64>(block_w) + static_cast<quint64>(bx);
			const uchar* b = src + block_index * 16;

			uchar r_vals[16]{};
			uchar g_vals[16]{};
			decode_bc4_generic_block(b + 0, snorm, r_vals, decode_channel_u8);
			decode_bc4_generic_block(b + 8, snorm, g_vals, decode_channel_u8);

			for (int i = 0; i < 16; ++i) {
				const int x = bx * 4 + (i % 4);
				const int y = by * 4 + (i / 4);

				// Reconstruct Z like a normal map (common BC5 usage).
				const float fx = (static_cast<float>(r_vals[i]) / 255.0f) * 2.0f - 1.0f;
				const float fy = (static_cast<float>(g_vals[i]) / 255.0f) * 2.0f - 1.0f;
				const float fz = std::sqrt(std::max(0.0f, 1.0f - fx * fx - fy * fy));
				const uchar bz = float11_to_u8(fz);
				set_px(x, y, r_vals[i], g_vals[i], bz);
			}
		}
	}

	if (out) {
		*out = std::move(img);
	}
	return true;
}
}  // namespace

QImage decode_dds_image(const QByteArray& bytes, QString* error) {
	if (error) {
		error->clear();
	}

	DdsInfo info;
	QString err;
	if (!parse_dds_header(bytes, &info, &err)) {
		if (error) {
			*error = err.isEmpty() ? "Unable to parse DDS header." : err;
		}
		return {};
	}

	QImage img;
	bool ok = false;
	switch (info.format) {
		case DdsFormat::UncompressedMasks:
			ok = decode_uncompressed_masks(bytes, info, &img, &err);
			break;
		case DdsFormat::Bc1:
			ok = decode_bc1(bytes, info, &img, &err);
			break;
		case DdsFormat::Bc2:
			ok = decode_bc2(bytes, info, &img, &err);
			break;
		case DdsFormat::Bc3:
			ok = decode_bc3(bytes, info, &img, &err);
			break;
		case DdsFormat::Bc4Unorm:
			ok = decode_bc4(bytes, info, false, &img, &err);
			break;
		case DdsFormat::Bc4Snorm:
			ok = decode_bc4(bytes, info, true, &img, &err);
			break;
		case DdsFormat::Bc5Unorm:
			ok = decode_bc5(bytes, info, false, &img, &err);
			break;
		case DdsFormat::Bc5Snorm:
			ok = decode_bc5(bytes, info, true, &img, &err);
			break;
		case DdsFormat::Unknown:
			ok = false;
			break;
	}

	if (!ok || img.isNull()) {
		if (error) {
			*error = err.isEmpty() ? "Unable to decode DDS image." : err;
		}
		return {};
	}

	return img;
}
