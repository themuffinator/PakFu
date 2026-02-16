#include "formats/idtech_asset_loader.h"

#include <algorithm>
#include <cstring>

#include <QStringList>
#include <QTextStream>

namespace {
QString file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return dot >= 0 ? lower.mid(dot + 1) : QString();
}

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

[[nodiscard]] QString fourcc_text(quint32 v) {
	char s[5] = {
		static_cast<char>(v & 0xFF),
		static_cast<char>((v >> 8) & 0xFF),
		static_cast<char>((v >> 16) & 0xFF),
		static_cast<char>((v >> 24) & 0xFF),
		'\0'
	};
	for (int i = 0; i < 4; ++i) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		if (c < 32 || c > 126) {
			s[i] = '.';
		}
	}
	return QString::fromLatin1(s, 4);
}

[[nodiscard]] QString spr_sprite_type_name(qint32 v) {
	switch (v) {
		case 0:
			return "VP_PARALLEL_UPRIGHT";
		case 1:
			return "FACING_UPRIGHT";
		case 2:
			return "VP_PARALLEL";
		case 3:
			return "ORIENTED";
		case 4:
			return "VP_PARALLEL_ORIENTED";
		default:
			return "UNKNOWN";
	}
}

[[nodiscard]] QString spr_synctype_name(qint32 v) {
	switch (v) {
		case 0:
			return "SYNC";
		case 1:
			return "RAND";
		default:
			return "UNKNOWN";
	}
}

[[nodiscard]] QString spr_tex_format_name(qint32 v) {
	switch (v) {
		case 0:
			return "NORMAL";
		case 1:
			return "ADDITIVE";
		case 2:
			return "INDEXALPHA";
		case 3:
			return "ALPHATEST";
		default:
			return "UNKNOWN";
	}
}

IdTechAssetDecodeResult decode_spr(const QByteArray& bytes) {
	constexpr quint32 kSprIdent = 0x50534449u;  // "IDSP"
	constexpr qint32 kSprV1 = 1;
	constexpr qint32 kSprV2 = 2;
	constexpr int kSprHeaderV1 = 36;
	constexpr int kSprHeaderV2 = 40;
	constexpr int kSprSingleFrameHeader = 16;
	constexpr int kSprMaxFrames = 8192;
	constexpr int kSprMaxGroupFrames = 4096;
	constexpr int kSprMaxTotalImages = 200000;
	constexpr int kSprMaxDimension = 16384;

	if (bytes.size() < 12) {
		return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "SPR file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version)) {
		return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "Unable to read SPR header."};
	}
	if (ident != kSprIdent) {
		return IdTechAssetDecodeResult{
			"Quake Sprite (SPR)",
			{},
			QString("Invalid SPR magic: expected IDSP, got %1.").arg(fourcc_text(ident))
		};
	}
	if (version != kSprV1 && version != kSprV2) {
		return IdTechAssetDecodeResult{
			"Quake Sprite (SPR)",
			{},
			QString("Unsupported SPR version: %1 (expected 1 or 2).").arg(version)
		};
	}

	const bool has_tex_format = (version == kSprV2);
	const int header_size = has_tex_format ? kSprHeaderV2 : kSprHeaderV1;
	if (bytes.size() < header_size) {
		return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "SPR header is truncated."};
	}

	qint32 sprite_type = 0;
	qint32 tex_format = 0;
	float bounding_radius = 0.0f;
	qint32 width = 0;
	qint32 height = 0;
	qint32 num_frames = 0;
	float beam_length = 0.0f;
	qint32 synctype = 0;

	if (!read_i32_le(bytes, 8, &sprite_type)) {
		return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "Unable to parse SPR type."};
	}
	if (has_tex_format) {
		if (!read_i32_le(bytes, 12, &tex_format) || !read_f32_le(bytes, 16, &bounding_radius) ||
		    !read_i32_le(bytes, 20, &width) || !read_i32_le(bytes, 24, &height) || !read_i32_le(bytes, 28, &num_frames) ||
		    !read_f32_le(bytes, 32, &beam_length) || !read_i32_le(bytes, 36, &synctype)) {
			return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "Unable to parse SPR v2 header."};
		}
	} else {
		if (!read_f32_le(bytes, 12, &bounding_radius) || !read_i32_le(bytes, 16, &width) || !read_i32_le(bytes, 20, &height) ||
		    !read_i32_le(bytes, 24, &num_frames) || !read_f32_le(bytes, 28, &beam_length) || !read_i32_le(bytes, 32, &synctype)) {
			return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "Unable to parse SPR v1 header."};
		}
	}

	if (num_frames <= 0 || num_frames > kSprMaxFrames) {
		return IdTechAssetDecodeResult{
			"Quake Sprite (SPR)",
			{},
			QString("Invalid SPR frame count: %1.").arg(num_frames)
		};
	}

	const auto parse_single_frame = [&](int ofs,
	                                    qint32* out_origin_x,
	                                    qint32* out_origin_y,
	                                    qint32* out_w,
	                                    qint32* out_h,
	                                    int* out_next,
	                                    QString* out_err) -> bool {
		if (out_err) {
			out_err->clear();
		}
		if (ofs < 0 || ofs + kSprSingleFrameHeader > bytes.size()) {
			if (out_err) {
				*out_err = "SPR frame header is truncated.";
			}
			return false;
		}
		qint32 origin_x = 0;
		qint32 origin_y = 0;
		qint32 fw = 0;
		qint32 fh = 0;
		if (!read_i32_le(bytes, ofs + 0, &origin_x) || !read_i32_le(bytes, ofs + 4, &origin_y) || !read_i32_le(bytes, ofs + 8, &fw) ||
		    !read_i32_le(bytes, ofs + 12, &fh)) {
			if (out_err) {
				*out_err = "Unable to parse SPR frame header.";
			}
			return false;
		}
		if (fw <= 0 || fh <= 0 || fw > kSprMaxDimension || fh > kSprMaxDimension) {
			if (out_err) {
				*out_err = QString("Invalid SPR frame dimensions: %1x%2.").arg(fw).arg(fh);
			}
			return false;
		}
		const qint64 pixels = static_cast<qint64>(fw) * static_cast<qint64>(fh);
		const qint64 next64 = static_cast<qint64>(ofs) + kSprSingleFrameHeader + pixels;
		if (pixels <= 0 || next64 > bytes.size()) {
			if (out_err) {
				*out_err = "SPR frame pixel data is truncated.";
			}
			return false;
		}

		if (out_origin_x) {
			*out_origin_x = origin_x;
		}
		if (out_origin_y) {
			*out_origin_y = origin_y;
		}
		if (out_w) {
			*out_w = fw;
		}
		if (out_h) {
			*out_h = fh;
		}
		if (out_next) {
			*out_next = static_cast<int>(next64);
		}
		return true;
	};

	int offset = header_size;
	int singles = 0;
	int groups = 0;
	int total_images = 0;
	int max_w = 0;
	int max_h = 0;
	int non_positive_intervals = 0;

	const int preview_count = std::min<int>(num_frames, 12);
	QStringList frame_lines;
	frame_lines.reserve(preview_count + 4);

	for (int i = 0; i < num_frames; ++i) {
		qint32 frame_type = 0;
		if (!read_i32_le(bytes, offset, &frame_type)) {
			return IdTechAssetDecodeResult{
				"Quake Sprite (SPR)",
				{},
				QString("Unable to parse SPR frame type at entry %1.").arg(i)
			};
		}
		offset += 4;

		if (frame_type == 0) {
			qint32 origin_x = 0;
			qint32 origin_y = 0;
			qint32 fw = 0;
			qint32 fh = 0;
			QString frame_err;
			if (!parse_single_frame(offset, &origin_x, &origin_y, &fw, &fh, &offset, &frame_err)) {
				return IdTechAssetDecodeResult{
					"Quake Sprite (SPR)",
					{},
					QString("SPR single frame %1 is invalid: %2").arg(i).arg(frame_err)
				};
			}
			++singles;
			++total_images;
			max_w = std::max(max_w, static_cast<int>(fw));
			max_h = std::max(max_h, static_cast<int>(fh));
			if (i < preview_count) {
				frame_lines.push_back(
				  QString("[%1] SINGLE  (%2x%3, origin %4,%5)").arg(i).arg(fw).arg(fh).arg(origin_x).arg(origin_y));
			}
			continue;
		}

		if (frame_type == 1) {
			qint32 group_count = 0;
			if (!read_i32_le(bytes, offset, &group_count)) {
				return IdTechAssetDecodeResult{
					"Quake Sprite (SPR)",
					{},
					QString("Unable to parse SPR group header at entry %1.").arg(i)
				};
			}
			offset += 4;
			if (group_count <= 0 || group_count > kSprMaxGroupFrames) {
				return IdTechAssetDecodeResult{
					"Quake Sprite (SPR)",
					{},
					QString("Invalid SPR group frame count at entry %1: %2.").arg(i).arg(group_count)
				};
			}
			if (total_images + group_count > kSprMaxTotalImages) {
				return IdTechAssetDecodeResult{"Quake Sprite (SPR)", {}, "SPR group image count exceeds safe limits."};
			}

			const qint64 interval_bytes = static_cast<qint64>(group_count) * sizeof(float);
			const qint64 intervals_end = static_cast<qint64>(offset) + interval_bytes;
			if (intervals_end > bytes.size()) {
				return IdTechAssetDecodeResult{
					"Quake Sprite (SPR)",
					{},
					QString("SPR frame intervals are truncated at group entry %1.").arg(i)
				};
			}

			float first_interval = 0.0f;
			float last_interval = 0.0f;
			for (int j = 0; j < group_count; ++j) {
				float interval = 0.0f;
				if (!read_f32_le(bytes, offset + j * static_cast<int>(sizeof(float)), &interval)) {
					return IdTechAssetDecodeResult{
						"Quake Sprite (SPR)",
						{},
						QString("Unable to parse SPR frame interval at group %1 index %2.").arg(i).arg(j)
					};
				}
				if (j == 0) {
					first_interval = interval;
				}
				if (j == group_count - 1) {
					last_interval = interval;
				}
				if (!(interval > 0.0f)) {
					++non_positive_intervals;
				}
			}
			offset = static_cast<int>(intervals_end);

			++groups;
			total_images += group_count;

			for (int j = 0; j < group_count; ++j) {
				qint32 origin_x = 0;
				qint32 origin_y = 0;
				qint32 fw = 0;
				qint32 fh = 0;
				QString frame_err;
				if (!parse_single_frame(offset, &origin_x, &origin_y, &fw, &fh, &offset, &frame_err)) {
					return IdTechAssetDecodeResult{
						"Quake Sprite (SPR)",
						{},
						QString("SPR group frame %1.%2 is invalid: %3").arg(i).arg(j).arg(frame_err)
					};
				}
				max_w = std::max(max_w, static_cast<int>(fw));
				max_h = std::max(max_h, static_cast<int>(fh));
			}

			if (i < preview_count) {
				frame_lines.push_back(QString("[%1] GROUP  (%2 frames, intervals %3 .. %4)")
				                        .arg(i)
				                        .arg(group_count)
				                        .arg(first_interval, 0, 'f', 3)
				                        .arg(last_interval, 0, 'f', 3));
			}
			continue;
		}

		return IdTechAssetDecodeResult{
			"Quake Sprite (SPR)",
			{},
			QString("Unsupported SPR frame type at entry %1: %2.").arg(i).arg(frame_type)
		};
	}

	const int trailing_bytes = (bytes.size() > offset) ? static_cast<int>(bytes.size() - offset) : 0;

	QString summary;
	QTextStream s(&summary);
	s << "Type: Quake sprite\n";
	s << "Format: SPR (IDSP)\n";
	s << "Version: " << version << "\n";
	s << "Sprite type: " << spr_sprite_type_name(sprite_type) << " (" << sprite_type << ")\n";
	if (has_tex_format) {
		s << "Texture format: " << spr_tex_format_name(tex_format) << " (" << tex_format << ")\n";
	}
	s << "Synctype: " << spr_synctype_name(synctype) << " (" << synctype << ")\n";
	s << "Nominal size: " << width << " x " << height << "\n";
	s << "Bounding radius: " << bounding_radius << "\n";
	s << "Beam length: " << beam_length << "\n";
	s << "Frames: " << num_frames << "\n";
	s << "Single entries: " << singles << "\n";
	s << "Group entries: " << groups << "\n";
	s << "Total frame images: " << total_images << "\n";
	s << "Largest frame: " << max_w << " x " << max_h << "\n";
	if (non_positive_intervals > 0) {
		s << "Non-positive frame intervals: " << non_positive_intervals << "\n";
	}
	s << "Frame table preview:\n";
	for (const QString& line : frame_lines) {
		s << "  " << line << "\n";
	}
	if (num_frames > preview_count) {
		s << "  ... (" << (num_frames - preview_count) << " more frame entries)\n";
	}
	if (trailing_bytes > 0) {
		s << "Trailing bytes: " << trailing_bytes << "\n";
	}

	return IdTechAssetDecodeResult{"Quake Sprite (SPR)", summary, {}};
}

IdTechAssetDecodeResult decode_sp2(const QByteArray& bytes) {
	constexpr quint32 kSp2Ident = 0x32534449u;  // "IDS2"
	constexpr qint32 kSp2Version = 2;
	constexpr int kSp2HeaderSize = 12;
	constexpr int kSp2FrameSize = 80;

	if (bytes.size() < kSp2HeaderSize) {
		return IdTechAssetDecodeResult{"Quake II Sprite (SP2)", {}, "SP2 file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	qint32 num_frames = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version) || !read_i32_le(bytes, 8, &num_frames)) {
		return IdTechAssetDecodeResult{"Quake II Sprite (SP2)", {}, "Unable to read SP2 header."};
	}

	if (ident != kSp2Ident) {
		return IdTechAssetDecodeResult{
			"Quake II Sprite (SP2)",
			{},
			QString("Invalid SP2 magic: expected IDS2, got %1.").arg(fourcc_text(ident))
		};
	}
	if (version != kSp2Version) {
		return IdTechAssetDecodeResult{
			"Quake II Sprite (SP2)",
			{},
			QString("Unsupported SP2 version: %1 (expected %2).").arg(version).arg(kSp2Version)
		};
	}
	if (num_frames <= 0 || num_frames > 8192) {
		return IdTechAssetDecodeResult{
			"Quake II Sprite (SP2)",
			{},
			QString("Invalid SP2 frame count: %1.").arg(num_frames)
		};
	}

	const qint64 required = static_cast<qint64>(kSp2HeaderSize) + static_cast<qint64>(num_frames) * kSp2FrameSize;
	if (required > bytes.size()) {
		return IdTechAssetDecodeResult{
			"Quake II Sprite (SP2)",
			{},
			QString("SP2 frame table is truncated (%1 bytes required, %2 bytes available).").arg(required).arg(bytes.size())
		};
	}

	int max_w = 0;
	int max_h = 0;
	int invalid_frames = 0;
	const int preview_count = std::min<int>(num_frames, 12);
	QStringList frame_lines;
	frame_lines.reserve(preview_count);

	for (int i = 0; i < num_frames; ++i) {
		const int off = kSp2HeaderSize + i * kSp2FrameSize;
		qint32 w = 0;
		qint32 h = 0;
		qint32 org_x = 0;
		qint32 org_y = 0;
		if (!read_i32_le(bytes, off + 0, &w) || !read_i32_le(bytes, off + 4, &h) || !read_i32_le(bytes, off + 8, &org_x) ||
		    !read_i32_le(bytes, off + 12, &org_y)) {
			return IdTechAssetDecodeResult{"Quake II Sprite (SP2)", {}, "Unable to parse SP2 frame table."};
		}

		if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
			++invalid_frames;
		} else {
			max_w = std::max(max_w, static_cast<int>(w));
			max_h = std::max(max_h, static_cast<int>(h));
		}

		if (i < preview_count) {
			const QString frame_name = fixed_c_string(bytes.constData() + off + 16, 64);
			const QString safe_name = frame_name.isEmpty() ? QString("<unnamed>") : frame_name;
			frame_lines.push_back(QString("[%1] %2  (%3x%4, origin %5,%6)")
			                        .arg(i)
			                        .arg(safe_name)
			                        .arg(w)
			                        .arg(h)
			                        .arg(org_x)
			                        .arg(org_y));
		}
	}

	QString summary;
	QTextStream s(&summary);
	s << "Type: Quake II sprite\n";
	s << "Format: SP2\n";
	s << "Version: " << version << "\n";
	s << "Frames: " << num_frames << "\n";
	s << "Largest frame: " << max_w << " x " << max_h << "\n";
	if (invalid_frames > 0) {
		s << "Frames with suspicious dimensions: " << invalid_frames << "\n";
	}
	s << "Frame table preview:\n";
	for (const QString& line : frame_lines) {
		s << "  " << line << "\n";
	}
	if (num_frames > preview_count) {
		s << "  ... (" << (num_frames - preview_count) << " more frame entries)\n";
	}

	return IdTechAssetDecodeResult{"Quake II Sprite (SP2)", summary, {}};
}

IdTechAssetDecodeResult decode_dm2(const QByteArray& bytes) {
	if (bytes.size() < 4) {
		return IdTechAssetDecodeResult{"Quake II Demo (DM2)", {}, "DM2 file is too small."};
	}

	constexpr int kMaxPackets = 2'000'000;
	int packet_count = 0;
	qint64 payload_bytes = 0;
	int max_packet = 0;
	int offset = 0;
	bool saw_end_marker = false;

	while (offset + 4 <= bytes.size()) {
		qint32 block_len = 0;
		if (!read_i32_le(bytes, offset, &block_len)) {
			return IdTechAssetDecodeResult{"Quake II Demo (DM2)", {}, "Unable to parse DM2 block header."};
		}
		offset += 4;

		if (block_len == -1) {
			saw_end_marker = true;
			break;
		}
		if (block_len < 0) {
			return IdTechAssetDecodeResult{
				"Quake II Demo (DM2)",
				{},
				QString("Invalid DM2 block length at packet %1: %2.").arg(packet_count).arg(block_len)
			};
		}
		if (offset + block_len > bytes.size()) {
			return IdTechAssetDecodeResult{
				"Quake II Demo (DM2)",
				{},
				QString("DM2 payload is truncated at packet %1.").arg(packet_count)
			};
		}

		++packet_count;
		payload_bytes += block_len;
		max_packet = std::max(max_packet, static_cast<int>(block_len));
		offset += block_len;

		if (packet_count > kMaxPackets) {
			return IdTechAssetDecodeResult{"Quake II Demo (DM2)", {}, "DM2 packet count is unreasonable."};
		}
	}

	const int trailing_bytes = (bytes.size() > offset) ? static_cast<int>(bytes.size() - offset) : 0;

	QString summary;
	QTextStream s(&summary);
	s << "Type: Quake II demo\n";
	s << "Format: DM2\n";
	s << "Packets: " << packet_count << "\n";
	s << "Payload bytes: " << payload_bytes << "\n";
	s << "Largest packet: " << max_packet << " bytes\n";
	s << "Terminated by -1 marker: " << (saw_end_marker ? "yes" : "no") << "\n";
	if (trailing_bytes > 0) {
		s << "Trailing bytes: " << trailing_bytes << "\n";
	}

	return IdTechAssetDecodeResult{"Quake II Demo (DM2)", summary, {}};
}

IdTechAssetDecodeResult decode_aas(const QByteArray& bytes) {
	constexpr quint32 kAasIdent = 0x53414145u;  // "EAAS"
	constexpr int kAasHeaderSize = 12;
	constexpr int kAasLumpSize = 8;
	constexpr int kAasLumpCount = 16;

	if (bytes.size() < kAasHeaderSize) {
		return IdTechAssetDecodeResult{"Quake III Bot Navigation (AAS)", {}, "AAS file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	qint32 checksum = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version) || !read_i32_le(bytes, 8, &checksum)) {
		return IdTechAssetDecodeResult{"Quake III Bot Navigation (AAS)", {}, "Unable to read AAS header."};
	}
	if (ident != kAasIdent) {
		return IdTechAssetDecodeResult{
			"Quake III Bot Navigation (AAS)",
			{},
			QString("Invalid AAS magic: expected EAAS, got %1.").arg(fourcc_text(ident))
		};
	}

	const qint64 raw_lump_slots = (bytes.size() > kAasHeaderSize) ? ((bytes.size() - kAasHeaderSize) / kAasLumpSize) : 0;
	const int possible_lumps = static_cast<int>(std::clamp<qint64>(raw_lump_slots, 0, kAasLumpCount));
	const int lump_count = std::min(kAasLumpCount, possible_lumps);

	int non_empty_lumps = 0;
	int invalid_lumps = 0;
	int largest_lump_index = -1;
	qint32 largest_lump_size = 0;

	for (int i = 0; i < lump_count; ++i) {
		const int off = kAasHeaderSize + i * kAasLumpSize;
		qint32 lump_ofs = 0;
		qint32 lump_len = 0;
		if (!read_i32_le(bytes, off + 0, &lump_ofs) || !read_i32_le(bytes, off + 4, &lump_len)) {
			++invalid_lumps;
			continue;
		}
		if (lump_len > 0) {
			++non_empty_lumps;
			if (lump_len > largest_lump_size) {
				largest_lump_size = lump_len;
				largest_lump_index = i;
			}
		}

		if (lump_ofs < 0 || lump_len < 0) {
			++invalid_lumps;
			continue;
		}

		const qint64 end = static_cast<qint64>(lump_ofs) + static_cast<qint64>(lump_len);
		if (end > bytes.size()) {
			++invalid_lumps;
		}
	}

	QString summary;
	QTextStream s(&summary);
	s << "Type: Quake III bot navigation mesh\n";
	s << "Format: AAS\n";
	s << "Version: " << version << "\n";
	s << "BSP checksum: " << checksum << "\n";
	s << "Lumps parsed: " << lump_count << " / " << kAasLumpCount << "\n";
	s << "Non-empty lumps: " << non_empty_lumps << "\n";
	if (largest_lump_index >= 0) {
		s << "Largest lump: #" << largest_lump_index << " (" << largest_lump_size << " bytes)\n";
	}
	if (invalid_lumps > 0) {
		s << "Lumps with invalid offsets/lengths: " << invalid_lumps << "\n";
	}

	return IdTechAssetDecodeResult{"Quake III Bot Navigation (AAS)", summary, {}};
}

IdTechAssetDecodeResult decode_qvm(const QByteArray& bytes) {
	constexpr quint32 kQvmMagic = 0x12721444u;
	constexpr int kQvmHeaderSize = 32;

	if (bytes.size() < kQvmHeaderSize) {
		return IdTechAssetDecodeResult{"Quake III Virtual Machine (QVM)", {}, "QVM file is too small."};
	}

	quint32 magic = 0;
	qint32 instruction_count = 0;
	qint32 code_offset = 0;
	qint32 code_length = 0;
	qint32 data_offset = 0;
	qint32 data_length = 0;
	qint32 lit_length = 0;
	qint32 bss_length = 0;
	if (!read_u32_le(bytes, 0, &magic) || !read_i32_le(bytes, 4, &instruction_count) || !read_i32_le(bytes, 8, &code_offset) ||
	    !read_i32_le(bytes, 12, &code_length) || !read_i32_le(bytes, 16, &data_offset) || !read_i32_le(bytes, 20, &data_length) ||
	    !read_i32_le(bytes, 24, &lit_length) || !read_i32_le(bytes, 28, &bss_length)) {
		return IdTechAssetDecodeResult{"Quake III Virtual Machine (QVM)", {}, "Unable to parse QVM header."};
	}

	if (magic != kQvmMagic) {
		return IdTechAssetDecodeResult{
			"Quake III Virtual Machine (QVM)",
			{},
			QString("Invalid QVM magic: expected 0x%1, got 0x%2.")
			  .arg(QString::number(kQvmMagic, 16))
			  .arg(QString::number(magic, 16))
		};
	}

	const auto span_fits_file = [&](qint32 ofs, qint32 len) -> bool {
		if (ofs < 0 || len < 0) {
			return false;
		}
		const qint64 end = static_cast<qint64>(ofs) + static_cast<qint64>(len);
		return end <= bytes.size();
	};

	const bool code_ok = span_fits_file(code_offset, code_length);
	const bool data_ok = span_fits_file(data_offset, data_length);
	const qint64 lit_offset = static_cast<qint64>(data_offset) + static_cast<qint64>(data_length);
	const bool lit_ok = (lit_length >= 0 && lit_offset >= 0 &&
	                     lit_offset + static_cast<qint64>(lit_length) <= static_cast<qint64>(bytes.size()));

	QString summary;
	QTextStream s(&summary);
	s << "Type: Quake III virtual machine bytecode\n";
	s << "Format: QVM\n";
	s << "Instructions: " << instruction_count << "\n";
	s << "Code segment: offset " << code_offset << ", size " << code_length << " bytes (" << (code_ok ? "ok" : "invalid") << ")\n";
	s << "Data segment: offset " << data_offset << ", size " << data_length << " bytes (" << (data_ok ? "ok" : "invalid") << ")\n";
	s << "Literal segment: offset " << lit_offset << ", size " << lit_length << " bytes (" << (lit_ok ? "ok" : "invalid") << ")\n";
	s << "BSS size: " << bss_length << " bytes\n";

	return IdTechAssetDecodeResult{"Quake III Virtual Machine (QVM)", summary, {}};
}
}  // namespace

bool is_supported_idtech_asset_file(const QString& file_name) {
	const QString ext = file_ext_lower(file_name);
	return ext == "spr" || ext == "sp2" || ext == "spr2" || ext == "dm2" || ext == "aas" || ext == "qvm";
}

IdTechAssetDecodeResult decode_idtech_asset_bytes(const QByteArray& bytes, const QString& file_name) {
	if (bytes.isEmpty()) {
		return IdTechAssetDecodeResult{{}, {}, "Empty input data."};
	}

	const QString ext = file_ext_lower(file_name);
	if (ext == "spr") {
		return decode_spr(bytes);
	}
	if (ext == "sp2" || ext == "spr2") {
		return decode_sp2(bytes);
	}
	if (ext == "dm2") {
		return decode_dm2(bytes);
	}
	if (ext == "aas") {
		return decode_aas(bytes);
	}
	if (ext == "qvm") {
		return decode_qvm(bytes);
	}

	return IdTechAssetDecodeResult{{}, {}, "Unsupported idTech asset type."};
}
