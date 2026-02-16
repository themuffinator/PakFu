#include "formats/sprite_loader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
[[nodiscard]] bool read_u32_le(const QByteArray& bytes, int offset, quint32* out) {
	if (!out || offset < 0 || offset + 4 > bytes.size()) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = static_cast<quint32>(p[0]) |
	       (static_cast<quint32>(p[1]) << 8) |
	       (static_cast<quint32>(p[2]) << 16) |
	       (static_cast<quint32>(p[3]) << 24);
	return true;
}

[[nodiscard]] bool read_i32_le(const QByteArray& bytes, int offset, qint32* out) {
	quint32 u = 0;
	if (!read_u32_le(bytes, offset, &u) || !out) {
		return false;
	}
	*out = static_cast<qint32>(u);
	return true;
}

[[nodiscard]] bool read_f32_le(const QByteArray& bytes, int offset, float* out) {
	quint32 u = 0;
	if (!read_u32_le(bytes, offset, &u) || !out) {
		return false;
	}
	static_assert(sizeof(float) == sizeof(quint32), "Unexpected float size");
	std::memcpy(out, &u, sizeof(float));
	return true;
}

[[nodiscard]] QString fixed_c_string(const char* data, int len) {
	if (!data || len <= 0) {
		return {};
	}
	int n = 0;
	while (n < len && data[n] != '\0') {
		++n;
	}
	return QString::fromLatin1(data, n).trimmed();
}

[[nodiscard]] int interval_to_ms(float seconds) {
	if (!std::isfinite(seconds) || seconds <= 0.0f) {
		return 100;
	}
	const int ms = static_cast<int>(std::lround(static_cast<double>(seconds) * 1000.0));
	return std::clamp(ms, 30, 2000);
}

[[nodiscard]] bool decode_spr_frame_image(const QByteArray& bytes,
                                          int pixel_offset,
                                          int width,
                                          int height,
                                          const QVector<QRgb>* palette,
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
	if (!palette || palette->size() != 256) {
		if (error) {
			*error = "SPR decode requires a 256-color palette.";
		}
		return false;
	}
	if (width <= 0 || height <= 0) {
		if (error) {
			*error = "Invalid SPR frame dimensions.";
		}
		return false;
	}

	const qint64 pixel_count = static_cast<qint64>(width) * static_cast<qint64>(height);
	if (pixel_count <= 0 || pixel_count > (1LL << 30)) {
		if (error) {
			*error = "SPR frame dimensions are out of range.";
		}
		return false;
	}
	if (pixel_offset < 0 || pixel_offset + pixel_count > bytes.size()) {
		if (error) {
			*error = "SPR frame pixel data is truncated.";
		}
		return false;
	}

	QImage img(width, height, QImage::Format_RGBA8888);
	if (img.isNull()) {
		if (error) {
			*error = "Unable to allocate SPR frame image.";
		}
		return false;
	}

	const auto* src = reinterpret_cast<const quint8*>(bytes.constData() + pixel_offset);
	for (int y = 0; y < height; ++y) {
		uchar* dst = img.scanLine(y);
		const qint64 row = static_cast<qint64>(y) * static_cast<qint64>(width);
		for (int x = 0; x < width; ++x) {
			const int idx = static_cast<int>(src[row + x]);
			const QRgb c = (*palette)[idx];
			uchar* px = dst + x * 4;
			px[0] = static_cast<uchar>(qRed(c));
			px[1] = static_cast<uchar>(qGreen(c));
			px[2] = static_cast<uchar>(qBlue(c));
			px[3] = (idx == 255) ? 0 : 255;
		}
	}

	*out = std::move(img);
	return true;
}
}  // namespace

SpriteDecodeResult decode_spr_sprite(const QByteArray& bytes, const QVector<QRgb>* palette) {
	constexpr quint32 kSprIdent = 0x50534449u;  // "IDSP"
	constexpr qint32 kSprV1 = 1;
	constexpr qint32 kSprV2 = 2;
	constexpr int kSprHeaderV1 = 36;
	constexpr int kSprHeaderV2 = 40;
	constexpr int kSprSingleFrameHeader = 16;
	constexpr int kSprMaxFrames = 8192;
	constexpr int kSprMaxGroupFrames = 4096;
	constexpr int kSprMaxTotalImages = 20000;
	constexpr int kSprMaxDimension = 16384;

	SpriteDecodeResult out;
	out.format = "SPR";

	if (bytes.size() < 12) {
		out.error = "SPR file is too small.";
		return out;
	}
	if (!palette || palette->size() != 256) {
		out.error = "Quake palette is required to decode SPR sprites.";
		return out;
	}

	quint32 ident = 0;
	qint32 version = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version)) {
		out.error = "Unable to parse SPR header.";
		return out;
	}
	if (ident != kSprIdent) {
		out.error = "Invalid SPR header magic.";
		return out;
	}
	if (version != kSprV1 && version != kSprV2) {
		out.error = QString("Unsupported SPR version: %1.").arg(version);
		return out;
	}

	const bool has_tex_format = (version == kSprV2);
	const int header_size = has_tex_format ? kSprHeaderV2 : kSprHeaderV1;
	if (bytes.size() < header_size) {
		out.error = "SPR header is truncated.";
		return out;
	}

	qint32 width = 0;
	qint32 height = 0;
	qint32 num_frames = 0;
	if (has_tex_format) {
		if (!read_i32_le(bytes, 20, &width) || !read_i32_le(bytes, 24, &height) || !read_i32_le(bytes, 28, &num_frames)) {
			out.error = "Unable to parse SPR v2 header.";
			return out;
		}
	} else {
		if (!read_i32_le(bytes, 16, &width) || !read_i32_le(bytes, 20, &height) || !read_i32_le(bytes, 24, &num_frames)) {
			out.error = "Unable to parse SPR v1 header.";
			return out;
		}
	}

	if (num_frames <= 0 || num_frames > kSprMaxFrames) {
		out.error = QString("Invalid SPR frame count: %1.").arg(num_frames);
		return out;
	}

	out.nominal_width = width;
	out.nominal_height = height;
	out.frames.reserve(num_frames);

	const auto parse_single = [&](int offset, int duration_ms, SpriteFrame* frame_out, int* next_out, QString* error) -> bool {
		if (error) {
			error->clear();
		}
		if (!frame_out || !next_out) {
			if (error) {
				*error = "Invalid SPR frame output pointers.";
			}
			return false;
		}
		if (offset < 0 || offset + kSprSingleFrameHeader > bytes.size()) {
			if (error) {
				*error = "SPR frame header is truncated.";
			}
			return false;
		}

		qint32 origin_x = 0;
		qint32 origin_y = 0;
		qint32 fw = 0;
		qint32 fh = 0;
		if (!read_i32_le(bytes, offset + 0, &origin_x) || !read_i32_le(bytes, offset + 4, &origin_y) ||
		    !read_i32_le(bytes, offset + 8, &fw) || !read_i32_le(bytes, offset + 12, &fh)) {
			if (error) {
				*error = "Unable to parse SPR frame header.";
			}
			return false;
		}
		if (fw <= 0 || fh <= 0 || fw > kSprMaxDimension || fh > kSprMaxDimension) {
			if (error) {
				*error = QString("Invalid SPR frame dimensions: %1x%2.").arg(fw).arg(fh);
			}
			return false;
		}

		const qint64 pixels = static_cast<qint64>(fw) * static_cast<qint64>(fh);
		const qint64 next64 = static_cast<qint64>(offset) + kSprSingleFrameHeader + pixels;
		if (pixels <= 0 || next64 > bytes.size()) {
			if (error) {
				*error = "SPR frame pixel data is truncated.";
			}
			return false;
		}

		QImage image;
		QString image_err;
		if (!decode_spr_frame_image(bytes, offset + kSprSingleFrameHeader, fw, fh, palette, &image, &image_err)) {
			if (error) {
				*error = image_err.isEmpty() ? "Unable to decode SPR frame image." : image_err;
			}
			return false;
		}

		frame_out->image = std::move(image);
		frame_out->duration_ms = std::max(30, duration_ms);
		frame_out->origin_x = origin_x;
		frame_out->origin_y = origin_y;
		*next_out = static_cast<int>(next64);
		return true;
	};

	int offset = header_size;
	for (int i = 0; i < num_frames; ++i) {
		qint32 frame_type = 0;
		if (!read_i32_le(bytes, offset, &frame_type)) {
			out.error = QString("Unable to parse SPR frame type at entry %1.").arg(i);
			return out;
		}
		offset += 4;

		if (frame_type == 0) {
			if (out.frames.size() >= kSprMaxTotalImages) {
				out.error = "SPR frame image count exceeds safe limits.";
				return out;
			}
			SpriteFrame frame;
			frame.name = QString("frame_%1").arg(out.frames.size());
			QString frame_err;
			if (!parse_single(offset, 100, &frame, &offset, &frame_err)) {
				out.error = QString("SPR frame %1 is invalid: %2").arg(i).arg(frame_err);
				return out;
			}
			out.frames.push_back(std::move(frame));
			continue;
		}

		if (frame_type == 1) {
			qint32 group_count = 0;
			if (!read_i32_le(bytes, offset, &group_count)) {
				out.error = QString("Unable to parse SPR frame group at entry %1.").arg(i);
				return out;
			}
			offset += 4;
			if (group_count <= 0 || group_count > kSprMaxGroupFrames) {
				out.error = QString("Invalid SPR group frame count at entry %1: %2.").arg(i).arg(group_count);
				return out;
			}
			if (out.frames.size() + group_count > kSprMaxTotalImages) {
				out.error = "SPR frame image count exceeds safe limits.";
				return out;
			}

			const qint64 interval_bytes = static_cast<qint64>(group_count) * static_cast<qint64>(sizeof(float));
			if (static_cast<qint64>(offset) + interval_bytes > bytes.size()) {
				out.error = QString("SPR group frame intervals are truncated at entry %1.").arg(i);
				return out;
			}

			QVector<int> durations;
			durations.reserve(group_count);
			for (int j = 0; j < group_count; ++j) {
				float interval = 0.0f;
				if (!read_f32_le(bytes, offset + j * static_cast<int>(sizeof(float)), &interval)) {
					out.error = QString("Unable to parse SPR frame interval at group %1 index %2.").arg(i).arg(j);
					return out;
				}
				durations.push_back(interval_to_ms(interval));
			}
			offset += static_cast<int>(interval_bytes);

			for (int j = 0; j < group_count; ++j) {
				SpriteFrame frame;
				frame.name = QString("group_%1_frame_%2").arg(i).arg(j);
				QString frame_err;
				if (!parse_single(offset, durations[j], &frame, &offset, &frame_err)) {
					out.error = QString("SPR group frame %1.%2 is invalid: %3").arg(i).arg(j).arg(frame_err);
					return out;
				}
				out.frames.push_back(std::move(frame));
			}
			continue;
		}

		out.error = QString("Unsupported SPR frame type at entry %1: %2.").arg(i).arg(frame_type);
		return out;
	}

	if (out.frames.isEmpty()) {
		out.error = "SPR does not contain any frames.";
	}
	return out;
}

SpriteDecodeResult decode_sp2_sprite(const QByteArray& bytes, const Sp2FrameLoader& frame_loader) {
	constexpr quint32 kSp2Ident = 0x32534449u;  // "IDS2"
	constexpr qint32 kSp2Version = 2;
	constexpr int kSp2HeaderSize = 12;
	constexpr int kSp2FrameSize = 80;
	constexpr int kSp2MaxFrames = 8192;

	SpriteDecodeResult out;
	out.format = "SP2";

	if (bytes.size() < kSp2HeaderSize) {
		out.error = "SP2 file is too small.";
		return out;
	}
	if (!frame_loader) {
		out.error = "No SP2 frame loader is available.";
		return out;
	}

	quint32 ident = 0;
	qint32 version = 0;
	qint32 num_frames = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version) || !read_i32_le(bytes, 8, &num_frames)) {
		out.error = "Unable to parse SP2 header.";
		return out;
	}
	if (ident != kSp2Ident) {
		out.error = "Invalid SP2 header magic.";
		return out;
	}
	if (version != kSp2Version) {
		out.error = QString("Unsupported SP2 version: %1.").arg(version);
		return out;
	}
	if (num_frames <= 0 || num_frames > kSp2MaxFrames) {
		out.error = QString("Invalid SP2 frame count: %1.").arg(num_frames);
		return out;
	}

	const qint64 required = static_cast<qint64>(kSp2HeaderSize) + static_cast<qint64>(num_frames) * kSp2FrameSize;
	if (required > bytes.size()) {
		out.error = QString("SP2 frame table is truncated (%1 bytes required, %2 available).").arg(required).arg(bytes.size());
		return out;
	}

	int max_w = 0;
	int max_h = 0;
	int missing = 0;

	out.frames.reserve(num_frames);
	for (int i = 0; i < num_frames; ++i) {
		const int off = kSp2HeaderSize + i * kSp2FrameSize;
		qint32 w = 0;
		qint32 h = 0;
		qint32 org_x = 0;
		qint32 org_y = 0;
		if (!read_i32_le(bytes, off + 0, &w) || !read_i32_le(bytes, off + 4, &h) || !read_i32_le(bytes, off + 8, &org_x) ||
		    !read_i32_le(bytes, off + 12, &org_y)) {
			out.error = "Unable to parse SP2 frame table.";
			out.frames.clear();
			return out;
		}

		const QString frame_name = fixed_c_string(bytes.constData() + off + 16, 64);
		if (frame_name.isEmpty()) {
			++missing;
			continue;
		}

		const ImageDecodeResult decoded = frame_loader(frame_name);
		if (!decoded.ok() || decoded.image.isNull()) {
			++missing;
			continue;
		}

		SpriteFrame frame;
		frame.image = decoded.image;
		frame.duration_ms = 100;
		frame.name = frame_name;
		frame.origin_x = org_x;
		frame.origin_y = org_y;
		out.frames.push_back(std::move(frame));

		if (w > 0 && h > 0) {
			max_w = std::max(max_w, static_cast<int>(w));
			max_h = std::max(max_h, static_cast<int>(h));
		}
	}

	out.nominal_width = max_w;
	out.nominal_height = max_h;

	if (out.frames.isEmpty()) {
		out.error = (missing > 0)
		              ? QString("Unable to resolve SP2 frame images (%1 frames missing).").arg(missing)
		              : "SP2 has no decodable frames.";
	}
	return out;
}
