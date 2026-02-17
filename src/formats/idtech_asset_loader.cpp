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

QString file_leaf_lower(const QString& name) {
	QString lower = name.toLower();
	const int slash = std::max(lower.lastIndexOf('/'), lower.lastIndexOf('\\'));
	if (slash >= 0) {
		lower = lower.mid(slash + 1);
	}
	return lower;
}

bool is_quake_progs_dat_file(const QString& name) {
	return file_leaf_lower(name) == "progs.dat";
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

[[nodiscard]] bool read_u16_le(const QByteArray& bytes, int offset, quint16* out) {
	if (!out || offset < 0 || offset + 2 > bytes.size()) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8);
	return true;
}

[[nodiscard]] bool read_u32_be(const QByteArray& bytes, int offset, quint32* out) {
	if (!out || offset < 0 || offset + 4 > bytes.size()) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = (static_cast<quint32>(p[0]) << 24) |
	       (static_cast<quint32>(p[1]) << 16) |
	       (static_cast<quint32>(p[2]) << 8) |
	       static_cast<quint32>(p[3]);
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

[[nodiscard]] bool span_fits(const QByteArray& bytes, qint64 offset, qint64 length) {
	if (offset < 0 || length < 0) {
		return false;
	}
	const qint64 size = bytes.size();
	if (offset > size) {
		return false;
	}
	return length <= (size - offset);
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

[[nodiscard]] quint32 crc32_table_entry(quint32 i) {
	quint32 c = i;
	for (int k = 0; k < 8; ++k) {
		if (c & 1u) {
			c = 0xEDB88320u ^ (c >> 1);
		} else {
			c >>= 1;
		}
	}
	return c;
}

[[nodiscard]] const quint32* crc32_table() {
	static quint32 table[256];
	static bool init = false;
	if (!init) {
		for (quint32 i = 0; i < 256; ++i) {
			table[i] = crc32_table_entry(i);
		}
		init = true;
	}
	return table;
}

void crc32_update(quint32* crc, const void* data, int len) {
	if (!crc || !data || len <= 0) {
		return;
	}
	const quint32* table = crc32_table();
	quint32 c = *crc;
	const auto* p = reinterpret_cast<const unsigned char*>(data);
	for (int i = 0; i < len; ++i) {
		c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
	}
	*crc = c;
}

[[nodiscard]] quint32 crc32_block(const void* data, int len) {
	quint32 c = 0xFFFFFFFFu;
	crc32_update(&c, data, len);
	return c ^ 0xFFFFFFFFu;
}

IdTechAssetDecodeResult decode_crc(const QByteArray& bytes) {
	constexpr quint32 kCrcMagic = 0xCC00CC00u;
	constexpr quint32 kKnownVersion = 1u;
	constexpr int kHeaderSize = 16;

	if (bytes.size() < kHeaderSize) {
		return IdTechAssetDecodeResult{"Doom 3 BFG CRC Manifest", {}, "CRC file is too small."};
	}

	quint32 magic = 0;
	quint32 version = 0;
	quint32 total_crc = 0;
	quint32 num_entries = 0;
	if (!read_u32_be(bytes, 0, &magic) || !read_u32_be(bytes, 4, &version) || !read_u32_be(bytes, 8, &total_crc) ||
	    !read_u32_be(bytes, 12, &num_entries)) {
		return IdTechAssetDecodeResult{"Doom 3 BFG CRC Manifest", {}, "Unable to parse CRC header."};
	}

	if (magic != kCrcMagic) {
		return IdTechAssetDecodeResult{
			"Doom 3 BFG CRC Manifest",
			{},
			QString("Invalid CRC magic: expected 0x%1, got 0x%2.")
			  .arg(QString::number(kCrcMagic, 16))
			  .arg(QString::number(magic, 16))
		};
	}

	const qint64 table_bytes = static_cast<qint64>(num_entries) * 4;
	const qint64 expected_size = static_cast<qint64>(kHeaderSize) + table_bytes;
	if (table_bytes < 0 || expected_size < kHeaderSize || expected_size > bytes.size()) {
		return IdTechAssetDecodeResult{
			"Doom 3 BFG CRC Manifest",
			{},
			QString("CRC table is truncated (expected %1 bytes, got %2).").arg(expected_size).arg(bytes.size())
		};
	}

	const int sample_count = std::min<int>(static_cast<int>(num_entries), 12);
	QStringList sample_lines;
	sample_lines.reserve(sample_count);

	quint32 rolling_le = 0xFFFFFFFFu;
	quint32 min_crc = 0xFFFFFFFFu;
	quint32 max_crc = 0u;
	int zero_entries = 0;

	for (quint32 i = 0; i < num_entries; ++i) {
		const int off = kHeaderSize + static_cast<int>(i) * 4;
		quint32 entry_crc = 0;
		if (!read_u32_be(bytes, off, &entry_crc)) {
			return IdTechAssetDecodeResult{
				"Doom 3 BFG CRC Manifest",
				{},
				QString("CRC table entry %1 is out of bounds.").arg(i)
			};
		}

		const unsigned char le_bytes[4] = {
			static_cast<unsigned char>(entry_crc & 0xFFu),
			static_cast<unsigned char>((entry_crc >> 8) & 0xFFu),
			static_cast<unsigned char>((entry_crc >> 16) & 0xFFu),
			static_cast<unsigned char>((entry_crc >> 24) & 0xFFu),
		};
		crc32_update(&rolling_le, le_bytes, 4);

		min_crc = std::min(min_crc, entry_crc);
		max_crc = std::max(max_crc, entry_crc);
		if (entry_crc == 0u) {
			++zero_entries;
		}

		if (static_cast<int>(i) < sample_count) {
			sample_lines.push_back(QString("[%1] 0x%2").arg(i).arg(QString::number(entry_crc, 16).rightJustified(8, QLatin1Char('0'))));
		}
	}

	const quint32 computed_total_le = rolling_le ^ 0xFFFFFFFFu;
	const quint32 computed_total_be = crc32_block(bytes.constData() + kHeaderSize, static_cast<int>(table_bytes));
	const bool total_matches_le = (computed_total_le == total_crc);
	const bool total_matches_be = (computed_total_be == total_crc);
	const qint64 trailing = bytes.size() - expected_size;

	QString summary;
	QTextStream s(&summary);
	s << "Type: Doom 3 BFG resource CRC manifest\n";
	s << "Format: CRC\n";
	s << "Magic: 0x" << QString::number(magic, 16).rightJustified(8, QLatin1Char('0')) << "\n";
	s << "Version: " << version;
	if (version != kKnownVersion) {
		s << " (unexpected)";
	}
	s << "\n";
	s << "Entry count: " << num_entries << "\n";
	s << "Stored aggregate CRC: 0x" << QString::number(total_crc, 16).rightJustified(8, QLatin1Char('0')) << "\n";
	s << "Computed aggregate CRC (LE-serialized entries): 0x"
	  << QString::number(computed_total_le, 16).rightJustified(8, QLatin1Char('0'))
	  << (total_matches_le ? " (match)" : " (mismatch)") << "\n";
	s << "Computed aggregate CRC (raw BE table bytes): 0x"
	  << QString::number(computed_total_be, 16).rightJustified(8, QLatin1Char('0'))
	  << (total_matches_be ? " (match)" : " (mismatch)") << "\n";
	s << "Table size: " << table_bytes << " bytes\n";
	s << "Entry CRC range: 0x" << QString::number(min_crc, 16).rightJustified(8, QLatin1Char('0'))
	  << " .. 0x" << QString::number(max_crc, 16).rightJustified(8, QLatin1Char('0')) << "\n";
	if (zero_entries > 0) {
		s << "Zero CRC entries: " << zero_entries << "\n";
	}
	if (trailing > 0) {
		s << "Trailing bytes: " << trailing << "\n";
	}
	if (sample_count > 0) {
		s << "CRC table preview:\n";
		for (const QString& line : sample_lines) {
			s << "  " << line << "\n";
		}
		if (num_entries > static_cast<quint32>(sample_count)) {
			s << "  ... (" << (num_entries - static_cast<quint32>(sample_count)) << " more entries)\n";
		}
	}

	return IdTechAssetDecodeResult{"Doom 3 BFG CRC Manifest", summary, {}};
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
	constexpr int kSprMaxPaletteEntries = 1024;

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

	bool has_embedded_palette = false;
	qint32 embedded_palette_entries = 0;
	int offset = header_size;
	if (has_tex_format) {
		quint16 palette_entries = 0;
		if (read_u16_le(bytes, offset, &palette_entries) && palette_entries > 0 && palette_entries <= kSprMaxPaletteEntries) {
			const qint64 palette_data_start = static_cast<qint64>(offset) + 2;
			const qint64 palette_data_size = static_cast<qint64>(palette_entries) * 3;
			const qint64 after_palette = palette_data_start + palette_data_size;
			qint32 direct_frame_type = -1;
			qint32 post_palette_frame_type = -1;
			const bool direct_is_frame = read_i32_le(bytes, offset, &direct_frame_type) &&
			                             (direct_frame_type == 0 || direct_frame_type == 1);
			const bool post_palette_is_frame = read_i32_le(bytes, static_cast<int>(after_palette), &post_palette_frame_type) &&
			                                   (post_palette_frame_type == 0 || post_palette_frame_type == 1);

			// GoldSrc SPR v2 stores a palette directly after the header.
			if (post_palette_is_frame && (!direct_is_frame || palette_entries == 256)) {
				has_embedded_palette = true;
				embedded_palette_entries = static_cast<qint32>(palette_entries);
				offset = static_cast<int>(after_palette);
			}
		}
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
	s << "Type: " << (has_embedded_palette ? "Half-Life / GoldSrc sprite" : "Quake sprite") << "\n";
	s << "Format: SPR (IDSP)\n";
	s << "Version: " << version << "\n";
	s << "Sprite type: " << spr_sprite_type_name(sprite_type) << " (" << sprite_type << ")\n";
	if (has_tex_format) {
		s << "Texture format: " << spr_tex_format_name(tex_format) << " (" << tex_format << ")\n";
	}
	if (has_embedded_palette) {
		s << "Embedded palette entries: " << embedded_palette_entries << "\n";
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

IdTechAssetDecodeResult decode_tag(const QByteArray& bytes) {
	constexpr quint32 kTagIdent = static_cast<quint32>('T') | (static_cast<quint32>('A') << 8) |
	                              (static_cast<quint32>('G') << 16) | (static_cast<quint32>('1') << 24);
	constexpr qint32 kTagVersion = 1;
	constexpr int kTagHeaderSize = 16;
	constexpr int kTagHeaderExtSize = 72;
	constexpr int kPreviewCount = 12;

	if (bytes.size() < kTagHeaderSize) {
		return IdTechAssetDecodeResult{"RtCW/ET Tag Table (TAG)", {}, "TAG file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	qint32 num_tags = 0;
	qint32 ofs_end = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version) || !read_i32_le(bytes, 8, &num_tags) ||
	    !read_i32_le(bytes, 12, &ofs_end)) {
		return IdTechAssetDecodeResult{"RtCW/ET Tag Table (TAG)", {}, "Unable to parse TAG header."};
	}

	if (ident != kTagIdent) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Tag Table (TAG)",
			{},
			QString("Invalid TAG magic: expected %1, got %2.")
			  .arg(fourcc_text(kTagIdent))
			  .arg(fourcc_text(ident))
		};
	}
	if (version != kTagVersion) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Tag Table (TAG)",
			{},
			QString("Unsupported TAG version: %1 (expected %2).").arg(version).arg(kTagVersion)
		};
	}
	if (num_tags < 0 || num_tags > 2000000) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Tag Table (TAG)",
			{},
			QString("Invalid TAG count: %1.").arg(num_tags)
		};
	}
	if (ofs_end <= 0 || ofs_end > bytes.size()) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Tag Table (TAG)",
			{},
			QString("Invalid TAG end offset: %1.").arg(ofs_end)
		};
	}

	const qint64 ext_table_bytes = static_cast<qint64>(num_tags) * kTagHeaderExtSize;
	const bool ext_table_fits = (num_tags == 0) || span_fits(bytes, kTagHeaderSize, ext_table_bytes);
	QStringList tag_preview;
	tag_preview.reserve(std::min(num_tags, kPreviewCount));
	if (ext_table_fits) {
		for (int i = 0; i < num_tags && i < kPreviewCount; ++i) {
			const int base = kTagHeaderSize + i * kTagHeaderExtSize;
			if (!span_fits(bytes, base, kTagHeaderExtSize)) {
				break;
			}
			const QString file_name = fixed_c_string(bytes.constData() + base + 0, 64);
			qint32 start = 0;
			qint32 count = 0;
			if (!read_i32_le(bytes, base + 64, &start) || !read_i32_le(bytes, base + 68, &count)) {
				break;
			}
			tag_preview.push_back(QString("[%1] %2  (start %3, count %4)")
			                        .arg(i)
			                        .arg(file_name.isEmpty() ? QString("<unnamed>") : file_name)
			                        .arg(start)
			                        .arg(count));
		}
	}

	QString summary;
	QTextStream s(&summary);
	s << "Type: RtCW/ET tag table\n";
	s << "Format: TAG\n";
	s << "Version: " << version << "\n";
	s << "Tag entries: " << num_tags << "\n";
	s << "End offset: " << ofs_end << "\n";
	if (ext_table_fits) {
		s << "Entry table layout: tagHeaderExt_t (" << kTagHeaderExtSize << " bytes each)\n";
		if (!tag_preview.isEmpty()) {
			s << "Tag entry preview:\n";
			for (const QString& line : tag_preview) {
				s << "  " << line << "\n";
			}
			if (num_tags > kPreviewCount) {
				s << "  ... (" << (num_tags - kPreviewCount) << " more entries)\n";
			}
		}
	} else if (num_tags > 0) {
		s << "Entry table: not present in this file (header-only TAG variant)\n";
	}
	if (ofs_end < bytes.size()) {
		s << "Trailing bytes: " << (bytes.size() - ofs_end) << "\n";
	}

	return IdTechAssetDecodeResult{"RtCW/ET Tag Table (TAG)", summary, {}};
}

IdTechAssetDecodeResult decode_mdx(const QByteArray& bytes) {
	constexpr quint32 kMdxIdent = static_cast<quint32>('M') | (static_cast<quint32>('D') << 8) |
	                              (static_cast<quint32>('X') << 16) | (static_cast<quint32>('W') << 24);
	constexpr qint32 kMdxVersion = 2;
	constexpr int kMdxHeaderSize = 96;
	constexpr int kMdxBoneInfoSize = 80;
	constexpr int kMdxFrameFixedSize = 52;
	constexpr int kMdxBoneFrameCompressedSize = 12;
	constexpr int kPreviewCount = 12;

	if (bytes.size() < kMdxHeaderSize) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Data (MDX)", {}, "MDX file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	qint32 num_frames = 0;
	qint32 num_bones = 0;
	qint32 ofs_frames = 0;
	qint32 ofs_bones = 0;
	qint32 torso_parent = 0;
	qint32 ofs_end = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version) || !read_i32_le(bytes, 72, &num_frames) ||
	    !read_i32_le(bytes, 76, &num_bones) || !read_i32_le(bytes, 80, &ofs_frames) || !read_i32_le(bytes, 84, &ofs_bones) ||
	    !read_i32_le(bytes, 88, &torso_parent) || !read_i32_le(bytes, 92, &ofs_end)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Data (MDX)", {}, "Unable to parse MDX header."};
	}
	if (ident != kMdxIdent) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Data (MDX)",
			{},
			QString("Invalid MDX magic: expected %1, got %2.")
			  .arg(fourcc_text(kMdxIdent))
			  .arg(fourcc_text(ident))
		};
	}
	if (version != kMdxVersion) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Data (MDX)",
			{},
			QString("Unsupported MDX version: %1 (expected %2).").arg(version).arg(kMdxVersion)
		};
	}
	if (num_frames <= 0 || num_frames > 100000 || num_bones <= 0 || num_bones > 8192) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Data (MDX)",
			{},
			QString("Invalid MDX frame/bone counts (frames=%1, bones=%2).").arg(num_frames).arg(num_bones)
		};
	}
	if (ofs_end <= 0 || ofs_end > bytes.size()) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Data (MDX)",
			{},
			QString("Invalid MDX end offset: %1.").arg(ofs_end)
		};
	}

	const qint64 bone_info_bytes = static_cast<qint64>(num_bones) * kMdxBoneInfoSize;
	const qint64 frame_stride = static_cast<qint64>(kMdxFrameFixedSize) + static_cast<qint64>(num_bones) * kMdxBoneFrameCompressedSize;
	const qint64 frame_bytes = static_cast<qint64>(num_frames) * frame_stride;
	if (!span_fits(bytes, ofs_bones, bone_info_bytes)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Data (MDX)", {}, "MDX bone info table is out of bounds."};
	}
	if (!span_fits(bytes, ofs_frames, frame_bytes)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Data (MDX)", {}, "MDX frame table is out of bounds."};
	}

	const QString name = fixed_c_string(bytes.constData() + 8, 64);
	QStringList bone_preview;
	bone_preview.reserve(std::min(num_bones, kPreviewCount));
	for (int i = 0; i < num_bones && i < kPreviewCount; ++i) {
		const int base = ofs_bones + i * kMdxBoneInfoSize;
		if (!span_fits(bytes, base, kMdxBoneInfoSize)) {
			break;
		}
		const QString bone_name = fixed_c_string(bytes.constData() + base + 0, 64);
		qint32 parent = -1;
		qint32 flags = 0;
		float parent_dist = 0.0f;
		if (!read_i32_le(bytes, base + 64, &parent) || !read_f32_le(bytes, base + 72, &parent_dist) || !read_i32_le(bytes, base + 76, &flags)) {
			break;
		}
		bone_preview.push_back(QString("[%1] %2  (parent %3, dist %4, flags 0x%5)")
		                         .arg(i)
		                         .arg(bone_name.isEmpty() ? QString("<unnamed>") : bone_name)
		                         .arg(parent)
		                         .arg(parent_dist, 0, 'f', 3)
		                         .arg(QString::number(flags, 16)));
	}

	float mins_x = 0.0f, mins_y = 0.0f, mins_z = 0.0f;
	float maxs_x = 0.0f, maxs_y = 0.0f, maxs_z = 0.0f;
	float radius = 0.0f;
	float parent_ofs_x = 0.0f, parent_ofs_y = 0.0f, parent_ofs_z = 0.0f;
	const bool frame0_ok =
	  read_f32_le(bytes, ofs_frames + 0, &mins_x) && read_f32_le(bytes, ofs_frames + 4, &mins_y) && read_f32_le(bytes, ofs_frames + 8, &mins_z) &&
	  read_f32_le(bytes, ofs_frames + 12, &maxs_x) && read_f32_le(bytes, ofs_frames + 16, &maxs_y) && read_f32_le(bytes, ofs_frames + 20, &maxs_z) &&
	  read_f32_le(bytes, ofs_frames + 36, &radius) && read_f32_le(bytes, ofs_frames + 40, &parent_ofs_x) &&
	  read_f32_le(bytes, ofs_frames + 44, &parent_ofs_y) && read_f32_le(bytes, ofs_frames + 48, &parent_ofs_z);

	QString summary;
	QTextStream s(&summary);
	s << "Type: RtCW/ET skeletal data\n";
	s << "Format: MDX\n";
	s << "Version: " << version << "\n";
	s << "Name: " << (name.isEmpty() ? "<unnamed>" : name) << "\n";
	s << "Frames: " << num_frames << "\n";
	s << "Bones: " << num_bones << "\n";
	s << "Torso parent index: " << torso_parent << "\n";
	s << "Frame table offset: " << ofs_frames << " (stride " << frame_stride << " bytes)\n";
	s << "Bone info offset: " << ofs_bones << " (" << bone_info_bytes << " bytes)\n";
	if (frame0_ok) {
		s << "Frame 0 bounds: mins(" << mins_x << ", " << mins_y << ", " << mins_z << "), maxs(" << maxs_x << ", " << maxs_y << ", " << maxs_z
		  << "), radius " << radius << "\n";
		s << "Frame 0 parent offset: (" << parent_ofs_x << ", " << parent_ofs_y << ", " << parent_ofs_z << ")\n";
	}
	if (!bone_preview.isEmpty()) {
		s << "Bone preview:\n";
		for (const QString& line : bone_preview) {
			s << "  " << line << "\n";
		}
		if (num_bones > kPreviewCount) {
			s << "  ... (" << (num_bones - kPreviewCount) << " more bones)\n";
		}
	}
	if (ofs_end < bytes.size()) {
		s << "Trailing bytes: " << (bytes.size() - ofs_end) << "\n";
	}

	return IdTechAssetDecodeResult{"RtCW/ET Skeletal Data (MDX)", summary, {}};
}

IdTechAssetDecodeResult decode_mds(const QByteArray& bytes) {
	constexpr quint32 kMdsIdent = static_cast<quint32>('M') | (static_cast<quint32>('D') << 8) |
	                              (static_cast<quint32>('S') << 16) | (static_cast<quint32>('W') << 24);
	constexpr qint32 kMdsVersion = 4;
	constexpr int kMdsHeaderSize = 120;
	constexpr int kMdsSurfaceHeaderSize = 176;
	constexpr int kMdsTagSize = 72;
	constexpr int kMdsFrameFixedSize = 52;
	constexpr int kMdsBoneFrameCompressedSize = 12;
	constexpr int kPreviewCount = 10;

	if (bytes.size() < kMdsHeaderSize) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Model (MDS)", {}, "MDS file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	qint32 num_frames = 0;
	qint32 num_bones = 0;
	qint32 ofs_frames = 0;
	qint32 ofs_bones = 0;
	qint32 torso_parent = 0;
	qint32 num_surfaces = 0;
	qint32 ofs_surfaces = 0;
	qint32 num_tags = 0;
	qint32 ofs_tags = 0;
	qint32 ofs_end = 0;
	float lod_scale = 0.0f;
	float lod_bias = 0.0f;

	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version) || !read_f32_le(bytes, 72, &lod_scale) ||
	    !read_f32_le(bytes, 76, &lod_bias) || !read_i32_le(bytes, 80, &num_frames) || !read_i32_le(bytes, 84, &num_bones) ||
	    !read_i32_le(bytes, 88, &ofs_frames) || !read_i32_le(bytes, 92, &ofs_bones) || !read_i32_le(bytes, 96, &torso_parent) ||
	    !read_i32_le(bytes, 100, &num_surfaces) || !read_i32_le(bytes, 104, &ofs_surfaces) || !read_i32_le(bytes, 108, &num_tags) ||
	    !read_i32_le(bytes, 112, &ofs_tags) || !read_i32_le(bytes, 116, &ofs_end)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Model (MDS)", {}, "Unable to parse MDS header."};
	}

	if (ident != kMdsIdent) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Model (MDS)",
			{},
			QString("Invalid MDS magic: expected %1, got %2.")
			  .arg(fourcc_text(kMdsIdent))
			  .arg(fourcc_text(ident))
		};
	}
	if (version != kMdsVersion) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Model (MDS)",
			{},
			QString("Unsupported MDS version: %1 (expected %2).").arg(version).arg(kMdsVersion)
		};
	}
	if (num_frames <= 0 || num_frames > 100000 || num_bones <= 0 || num_bones > 8192 || num_surfaces < 0 || num_surfaces > 32768 ||
	    num_tags < 0 || num_tags > 32768) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Model (MDS)",
			{},
			QString("Invalid MDS counts (frames=%1, bones=%2, surfaces=%3, tags=%4).")
			  .arg(num_frames)
			  .arg(num_bones)
			  .arg(num_surfaces)
			  .arg(num_tags)
		};
	}
	if (ofs_end <= 0 || ofs_end > bytes.size()) {
		return IdTechAssetDecodeResult{
			"RtCW/ET Skeletal Model (MDS)",
			{},
			QString("Invalid MDS end offset: %1.").arg(ofs_end)
		};
	}

	const qint64 bone_info_bytes = static_cast<qint64>(num_bones) * 80;
	const qint64 frame_stride = static_cast<qint64>(kMdsFrameFixedSize) + static_cast<qint64>(num_bones) * kMdsBoneFrameCompressedSize;
	const qint64 frame_table_bytes = static_cast<qint64>(num_frames) * frame_stride;
	if (!span_fits(bytes, ofs_bones, bone_info_bytes)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Model (MDS)", {}, "MDS bone info table is out of bounds."};
	}
	if (!span_fits(bytes, ofs_frames, frame_table_bytes)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Model (MDS)", {}, "MDS frame table is out of bounds."};
	}
	const qint64 tag_bytes = static_cast<qint64>(num_tags) * kMdsTagSize;
	if (num_tags > 0 && !span_fits(bytes, ofs_tags, tag_bytes)) {
		return IdTechAssetDecodeResult{"RtCW/ET Skeletal Model (MDS)", {}, "MDS tag table is out of bounds."};
	}

	qint64 total_triangles = 0;
	qint64 total_vertices = 0;
	qint64 total_bone_refs = 0;
	QStringList surface_preview;
	surface_preview.reserve(std::min(num_surfaces, kPreviewCount));

	qint64 surf_ofs = ofs_surfaces;
	for (int i = 0; i < num_surfaces; ++i) {
		if (!span_fits(bytes, surf_ofs, kMdsSurfaceHeaderSize)) {
			return IdTechAssetDecodeResult{
				"RtCW/ET Skeletal Model (MDS)",
				{},
				QString("MDS surface %1 header is out of bounds.").arg(i)
			};
		}

		const int base = static_cast<int>(surf_ofs);
		quint32 surf_ident = 0;
		qint32 min_lod = 0;
		qint32 num_verts = 0;
		qint32 num_tris = 0;
		qint32 num_bone_refs = 0;
		qint32 ofs_surf_end = 0;
		if (!read_u32_le(bytes, base + 0, &surf_ident) || !read_i32_le(bytes, base + 136, &min_lod) || !read_i32_le(bytes, base + 144, &num_verts) ||
		    !read_i32_le(bytes, base + 152, &num_tris) || !read_i32_le(bytes, base + 164, &num_bone_refs) ||
		    !read_i32_le(bytes, base + 172, &ofs_surf_end)) {
			return IdTechAssetDecodeResult{
				"RtCW/ET Skeletal Model (MDS)",
				{},
				QString("MDS surface %1 header is truncated.").arg(i)
			};
		}
		if (num_verts < 0 || num_verts > 10000000 || num_tris < 0 || num_tris > 10000000 || num_bone_refs < 0 || num_bone_refs > 10000000 ||
		    ofs_surf_end <= kMdsSurfaceHeaderSize) {
			return IdTechAssetDecodeResult{
				"RtCW/ET Skeletal Model (MDS)",
				{},
				QString("MDS surface %1 has invalid counts/offsets.").arg(i)
			};
		}

		const qint64 surf_end = surf_ofs + ofs_surf_end;
		if (surf_end <= surf_ofs || surf_end > bytes.size() || surf_end > ofs_end) {
			return IdTechAssetDecodeResult{
				"RtCW/ET Skeletal Model (MDS)",
				{},
				QString("MDS surface %1 exceeds file bounds.").arg(i)
			};
		}

		total_vertices += num_verts;
		total_triangles += num_tris;
		total_bone_refs += num_bone_refs;

		if (i < kPreviewCount) {
			const QString surf_name = fixed_c_string(bytes.constData() + base + 4, 64);
			const QString surf_shader = fixed_c_string(bytes.constData() + base + 68, 64);
			surface_preview.push_back(QString("[%1] %2  (%3 tris, %4 verts, bone refs %5, min LOD %6, shader %7, ident %8)")
			                            .arg(i)
			                            .arg(surf_name.isEmpty() ? QString("<surface>") : surf_name)
			                            .arg(num_tris)
			                            .arg(num_verts)
			                            .arg(num_bone_refs)
			                            .arg(min_lod)
			                            .arg(surf_shader.isEmpty() ? QString("<none>") : surf_shader)
			                            .arg(fourcc_text(surf_ident)));
		}

		surf_ofs = surf_end;
	}

	QStringList tag_preview;
	tag_preview.reserve(std::min(num_tags, kPreviewCount));
	for (int i = 0; i < num_tags && i < kPreviewCount; ++i) {
		const int base = ofs_tags + i * kMdsTagSize;
		if (!span_fits(bytes, base, kMdsTagSize)) {
			break;
		}
		const QString tag_name = fixed_c_string(bytes.constData() + base + 0, 64);
		float torso_weight = 0.0f;
		qint32 bone_index = -1;
		if (!read_f32_le(bytes, base + 64, &torso_weight) || !read_i32_le(bytes, base + 68, &bone_index)) {
			break;
		}
		tag_preview.push_back(QString("[%1] %2  (bone %3, torso weight %4)")
		                       .arg(i)
		                       .arg(tag_name.isEmpty() ? QString("<tag>") : tag_name)
		                       .arg(bone_index)
		                       .arg(torso_weight, 0, 'f', 3));
	}

	float mins_x = 0.0f, mins_y = 0.0f, mins_z = 0.0f;
	float maxs_x = 0.0f, maxs_y = 0.0f, maxs_z = 0.0f;
	float radius = 0.0f;
	float parent_ofs_x = 0.0f, parent_ofs_y = 0.0f, parent_ofs_z = 0.0f;
	const bool frame0_ok =
	  read_f32_le(bytes, ofs_frames + 0, &mins_x) && read_f32_le(bytes, ofs_frames + 4, &mins_y) && read_f32_le(bytes, ofs_frames + 8, &mins_z) &&
	  read_f32_le(bytes, ofs_frames + 12, &maxs_x) && read_f32_le(bytes, ofs_frames + 16, &maxs_y) && read_f32_le(bytes, ofs_frames + 20, &maxs_z) &&
	  read_f32_le(bytes, ofs_frames + 36, &radius) && read_f32_le(bytes, ofs_frames + 40, &parent_ofs_x) &&
	  read_f32_le(bytes, ofs_frames + 44, &parent_ofs_y) && read_f32_le(bytes, ofs_frames + 48, &parent_ofs_z);

	const QString model_name = fixed_c_string(bytes.constData() + 8, 64);
	QString summary;
	QTextStream s(&summary);
	s << "Type: RtCW/ET skeletal model\n";
	s << "Format: MDS\n";
	s << "Version: " << version << "\n";
	s << "Name: " << (model_name.isEmpty() ? "<unnamed>" : model_name) << "\n";
	s << "LOD scale/bias: " << lod_scale << " / " << lod_bias << "\n";
	s << "Frames: " << num_frames << "\n";
	s << "Bones: " << num_bones << "\n";
	s << "Torso parent index: " << torso_parent << "\n";
	s << "Surfaces: " << num_surfaces << "\n";
	s << "Tags: " << num_tags << "\n";
	s << "Triangles: " << total_triangles << "\n";
	s << "Vertices: " << total_vertices << "\n";
	s << "Bone references (sum): " << total_bone_refs << "\n";
	s << "Frame table offset: " << ofs_frames << " (stride " << frame_stride << " bytes)\n";
	s << "Bone info offset: " << ofs_bones << " (" << bone_info_bytes << " bytes)\n";
	s << "Surface table offset: " << ofs_surfaces << "\n";
	s << "Tag table offset: " << ofs_tags << "\n";
	if (frame0_ok) {
		s << "Frame 0 bounds: mins(" << mins_x << ", " << mins_y << ", " << mins_z << "), maxs(" << maxs_x << ", " << maxs_y << ", " << maxs_z
		  << "), radius " << radius << "\n";
		s << "Frame 0 parent offset: (" << parent_ofs_x << ", " << parent_ofs_y << ", " << parent_ofs_z << ")\n";
	}
	if (!surface_preview.isEmpty()) {
		s << "Surface preview:\n";
		for (const QString& line : surface_preview) {
			s << "  " << line << "\n";
		}
		if (num_surfaces > kPreviewCount) {
			s << "  ... (" << (num_surfaces - kPreviewCount) << " more surfaces)\n";
		}
	}
	if (!tag_preview.isEmpty()) {
		s << "Tag preview:\n";
		for (const QString& line : tag_preview) {
			s << "  " << line << "\n";
		}
		if (num_tags > kPreviewCount) {
			s << "  ... (" << (num_tags - kPreviewCount) << " more tags)\n";
		}
	}
	if (ofs_end < bytes.size()) {
		s << "Trailing bytes: " << (bytes.size() - ofs_end) << "\n";
	}

	return IdTechAssetDecodeResult{"RtCW/ET Skeletal Model (MDS)", summary, {}};
}

IdTechAssetDecodeResult decode_progs_dat(const QByteArray& bytes) {
	constexpr int kHeaderSize = 15 * static_cast<int>(sizeof(qint32));
	constexpr int kStatementSize = 8;
	constexpr int kDefSize = 8;
	constexpr int kFunctionSize = 36;
	constexpr qint32 kKnownVersion = 6;
	constexpr int kPreviewCount = 12;

	if (bytes.size() < kHeaderSize) {
		return IdTechAssetDecodeResult{"QuakeC Program (progs.dat)", {}, "progs.dat is too small."};
	}

	qint32 version = 0;
	qint32 crc = 0;
	qint32 ofs_statements = 0;
	qint32 num_statements = 0;
	qint32 ofs_globaldefs = 0;
	qint32 num_globaldefs = 0;
	qint32 ofs_fielddefs = 0;
	qint32 num_fielddefs = 0;
	qint32 ofs_functions = 0;
	qint32 num_functions = 0;
	qint32 ofs_strings = 0;
	qint32 num_strings = 0;
	qint32 ofs_globals = 0;
	qint32 num_globals = 0;
	qint32 entity_fields = 0;

	if (!read_i32_le(bytes, 0, &version) || !read_i32_le(bytes, 4, &crc) || !read_i32_le(bytes, 8, &ofs_statements) ||
	    !read_i32_le(bytes, 12, &num_statements) || !read_i32_le(bytes, 16, &ofs_globaldefs) ||
	    !read_i32_le(bytes, 20, &num_globaldefs) || !read_i32_le(bytes, 24, &ofs_fielddefs) ||
	    !read_i32_le(bytes, 28, &num_fielddefs) || !read_i32_le(bytes, 32, &ofs_functions) ||
	    !read_i32_le(bytes, 36, &num_functions) || !read_i32_le(bytes, 40, &ofs_strings) ||
	    !read_i32_le(bytes, 44, &num_strings) || !read_i32_le(bytes, 48, &ofs_globals) ||
	    !read_i32_le(bytes, 52, &num_globals) || !read_i32_le(bytes, 56, &entity_fields)) {
		return IdTechAssetDecodeResult{"QuakeC Program (progs.dat)", {}, "Unable to parse progs.dat header."};
	}

	const auto span_fits = [&](qint32 ofs, qint32 count, int stride, const QString& section_name, QString* reason) -> bool {
		if (reason) {
			reason->clear();
		}
		if (ofs < 0 || count < 0 || stride <= 0) {
			if (reason) {
				*reason = QString("%1 has negative offset/count.").arg(section_name);
			}
			return false;
		}
		const qint64 size = static_cast<qint64>(count) * static_cast<qint64>(stride);
		const qint64 end = static_cast<qint64>(ofs) + size;
		if (size < 0 || end < 0 || end > bytes.size()) {
			if (reason) {
				*reason = QString("%1 exceeds file bounds (offset=%2, count=%3, stride=%4).").arg(section_name).arg(ofs).arg(count).arg(stride);
			}
			return false;
		}
		return true;
	};

	QStringList invalid_sections;
	QString span_reason;
	if (!span_fits(ofs_statements, num_statements, kStatementSize, "Statements", &span_reason)) {
		invalid_sections.push_back(span_reason);
	}
	if (!span_fits(ofs_globaldefs, num_globaldefs, kDefSize, "Global defs", &span_reason)) {
		invalid_sections.push_back(span_reason);
	}
	if (!span_fits(ofs_fielddefs, num_fielddefs, kDefSize, "Field defs", &span_reason)) {
		invalid_sections.push_back(span_reason);
	}
	if (!span_fits(ofs_functions, num_functions, kFunctionSize, "Functions", &span_reason)) {
		invalid_sections.push_back(span_reason);
	}
	if (!span_fits(ofs_strings, num_strings, 1, "String table", &span_reason)) {
		invalid_sections.push_back(span_reason);
	}
	if (!span_fits(ofs_globals, num_globals, static_cast<int>(sizeof(float)), "Globals", &span_reason)) {
		invalid_sections.push_back(span_reason);
	}
	if (!invalid_sections.isEmpty()) {
		return IdTechAssetDecodeResult{
			"QuakeC Program (progs.dat)",
			{},
			QString("Invalid progs.dat section table:\n- %1").arg(invalid_sections.join("\n- "))
		};
	}

	const auto qc_string_at = [&](qint32 string_ofs) -> QString {
		if (string_ofs < 0 || string_ofs >= num_strings || num_strings <= 0) {
			return {};
		}
		const qint64 base = static_cast<qint64>(ofs_strings) + static_cast<qint64>(string_ofs);
		const qint64 end = static_cast<qint64>(ofs_strings) + static_cast<qint64>(num_strings);
		if (base < 0 || base >= end || end > bytes.size()) {
			return {};
		}
		const char* start = bytes.constData() + static_cast<int>(base);
		const int max_len = static_cast<int>(end - base);
		int len = 0;
		while (len < max_len && start[len] != '\0') {
			++len;
		}
		return QString::fromLatin1(start, len);
	};

	int builtin_functions = 0;
	int bytecode_functions = 0;
	int suspicious_param_counts = 0;
	int invalid_name_offsets = 0;
	qint64 local_slots_total = 0;
	int max_local_slots = 0;
	QStringList function_preview;
	function_preview.reserve(kPreviewCount);
	QStringList source_files_preview;
	source_files_preview.reserve(8);

	for (int i = 0; i < num_functions; ++i) {
		const int base = ofs_functions + i * kFunctionSize;
		qint32 first_statement = 0;
		qint32 local_slots = 0;
		qint32 name_ofs = 0;
		qint32 file_ofs = 0;
		qint32 num_parms = 0;
		if (!read_i32_le(bytes, base + 0, &first_statement) || !read_i32_le(bytes, base + 8, &local_slots) ||
		    !read_i32_le(bytes, base + 16, &name_ofs) || !read_i32_le(bytes, base + 20, &file_ofs) ||
		    !read_i32_le(bytes, base + 24, &num_parms)) {
			return IdTechAssetDecodeResult{
				"QuakeC Program (progs.dat)",
				{},
				QString("Unable to parse function entry %1.").arg(i)
			};
		}

		if (first_statement < 0) {
			++builtin_functions;
		} else {
			++bytecode_functions;
		}
		if (num_parms < 0 || num_parms > 8) {
			++suspicious_param_counts;
		}
		if (local_slots > 0) {
			local_slots_total += local_slots;
			max_local_slots = std::max(max_local_slots, static_cast<int>(local_slots));
		}

		const QString fn_name = qc_string_at(name_ofs);
		if (fn_name.isEmpty() && (name_ofs < 0 || name_ofs >= num_strings)) {
			++invalid_name_offsets;
		}

		if (function_preview.size() < kPreviewCount) {
			const QString safe_name = fn_name.isEmpty() ? QString("<unnamed_%1>").arg(i) : fn_name;
			if (first_statement < 0) {
				function_preview.push_back(QString("[%1] %2  (builtin #%3, params %4)")
				                             .arg(i)
				                             .arg(safe_name)
				                             .arg(-first_statement)
				                             .arg(num_parms));
			} else {
				function_preview.push_back(QString("[%1] %2  (stmt %3, locals %4, params %5)")
				                             .arg(i)
				                             .arg(safe_name)
				                             .arg(first_statement)
				                             .arg(local_slots)
				                             .arg(num_parms));
			}
		}

		const QString src_file = qc_string_at(file_ofs).trimmed();
		if (!src_file.isEmpty() && source_files_preview.size() < 8 && !source_files_preview.contains(src_file)) {
			source_files_preview.push_back(src_file);
		}
	}

	const auto section_bytes = [](qint32 count, int stride) -> qint64 {
		if (count <= 0 || stride <= 0) {
			return 0;
		}
		return static_cast<qint64>(count) * static_cast<qint64>(stride);
	};

	QString summary;
	QTextStream s(&summary);
	s << "Type: QuakeC virtual machine program\n";
	s << "Format: progs.dat\n";
	s << "Version: " << version;
	if (version != kKnownVersion) {
		s << " (unexpected; Quake usually uses " << kKnownVersion << ")";
	}
	s << "\n";
	s << "CRC: " << crc << "\n";
	s << "Entity fields per edict: " << entity_fields << "\n";
	s << "Statements: " << num_statements << " (offset " << ofs_statements << ", " << section_bytes(num_statements, kStatementSize) << " bytes)\n";
	s << "Global defs: " << num_globaldefs << " (offset " << ofs_globaldefs << ", " << section_bytes(num_globaldefs, kDefSize) << " bytes)\n";
	s << "Field defs: " << num_fielddefs << " (offset " << ofs_fielddefs << ", " << section_bytes(num_fielddefs, kDefSize) << " bytes)\n";
	s << "Functions: " << num_functions << " (offset " << ofs_functions << ", " << section_bytes(num_functions, kFunctionSize) << " bytes)\n";
	s << "  Built-ins: " << builtin_functions << "\n";
	s << "  Bytecode functions: " << bytecode_functions << "\n";
	s << "String table bytes: " << num_strings << " (offset " << ofs_strings << ")\n";
	s << "Globals: " << num_globals << " (offset " << ofs_globals << ", " << section_bytes(num_globals, static_cast<int>(sizeof(float))) << " bytes)\n";
	s << "Function local slots total: " << local_slots_total << "\n";
	s << "Largest function local slot count: " << max_local_slots << "\n";
	if (suspicious_param_counts > 0) {
		s << "Functions with unusual parameter counts: " << suspicious_param_counts << "\n";
	}
	if (invalid_name_offsets > 0) {
		s << "Functions with invalid name offsets: " << invalid_name_offsets << "\n";
	}
	if (!source_files_preview.isEmpty()) {
		s << "Source file preview:\n";
		for (const QString& line : source_files_preview) {
			s << "  " << line << "\n";
		}
	}
	if (!function_preview.isEmpty()) {
		s << "Function table preview:\n";
		for (const QString& line : function_preview) {
			s << "  " << line << "\n";
		}
		if (num_functions > kPreviewCount) {
			s << "  ... (" << (num_functions - kPreviewCount) << " more function entries)\n";
		}
	}

	return IdTechAssetDecodeResult{"QuakeC Program (progs.dat)", summary, {}};
}

IdTechAssetDecodeResult decode_skel_mesh(const QByteArray& bytes,
                                         quint32 expected_ident,
                                         qint32 expected_version_a,
                                         qint32 expected_version_b,
                                         const QString& format_label) {
	const QString title = QString("FAKK2/MOHAA Skeletal Mesh (%1)").arg(format_label);
	constexpr int kHeaderSizeNoScale = 148;
	constexpr int kSurfaceHeaderSize = 100;
	constexpr qint32 kMaxSurfaces = 32768;
	constexpr qint32 kMaxBones = 262144;
	constexpr qint32 kMaxSurfaceTriangles = 10000000;
	constexpr qint32 kMaxSurfaceVerts = 10000000;
	constexpr int kPreviewCount = 10;

	if (bytes.size() < kHeaderSizeNoScale) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("%1 file is too small.").arg(format_label)
		};
	}

	quint32 ident = 0;
	qint32 version = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version)) {
		return IdTechAssetDecodeResult{title, {}, QString("Unable to parse %1 header.").arg(format_label)};
	}
	if (ident != expected_ident) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Invalid %1 magic: expected %2, got %3.")
			  .arg(format_label)
			  .arg(fourcc_text(expected_ident))
			  .arg(fourcc_text(ident))
		};
	}
	if (version != expected_version_a && version != expected_version_b) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Unsupported %1 version: %2 (expected %3 or %4).")
			  .arg(format_label)
			  .arg(version)
			  .arg(expected_version_a)
			  .arg(expected_version_b)
		};
	}

	const QString mesh_name = fixed_c_string(bytes.constData() + 8, 64);

	qint32 num_surfaces = 0;
	qint32 num_bones = 0;
	qint32 ofs_bones = 0;
	qint32 ofs_surfaces = 0;
	qint32 ofs_end = 0;
	qint32 num_boxes = 0;
	qint32 ofs_boxes = 0;
	qint32 num_morph_targets = 0;
	qint32 ofs_morph_targets = 0;
	if (!read_i32_le(bytes, 72, &num_surfaces) || !read_i32_le(bytes, 76, &num_bones) || !read_i32_le(bytes, 80, &ofs_bones) ||
	    !read_i32_le(bytes, 84, &ofs_surfaces) || !read_i32_le(bytes, 88, &ofs_end) || !read_i32_le(bytes, 132, &num_boxes) ||
	    !read_i32_le(bytes, 136, &ofs_boxes) || !read_i32_le(bytes, 140, &num_morph_targets) ||
	    !read_i32_le(bytes, 144, &ofs_morph_targets)) {
		return IdTechAssetDecodeResult{title, {}, QString("Unable to parse %1 header fields.").arg(format_label)};
	}

	float scale = 1.0f;
	const bool has_scale_field = read_f32_le(bytes, 148, &scale);

	if (num_surfaces < 0 || num_surfaces > kMaxSurfaces) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Invalid %1 surface count: %2.").arg(format_label).arg(num_surfaces)
		};
	}
	if (num_bones < 0 || num_bones > kMaxBones) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Invalid %1 bone count: %2.").arg(format_label).arg(num_bones)
		};
	}
	if (ofs_surfaces < kHeaderSizeNoScale || ofs_surfaces >= bytes.size()) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Invalid %1 surface table offset: %2.").arg(format_label).arg(ofs_surfaces)
		};
	}
	if (ofs_end <= 0 || ofs_end > bytes.size()) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Invalid %1 end offset: %2.").arg(format_label).arg(ofs_end)
		};
	}
	if (ofs_end < ofs_surfaces) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("%1 end offset precedes surface table.").arg(format_label)
		};
	}
	if (num_boxes < 0 || num_morph_targets < 0) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("%1 header has negative box/morph counts.").arg(format_label)
		};
	}
	if ((num_boxes > 0 && ofs_boxes <= 0) || (num_morph_targets > 0 && ofs_morph_targets <= 0)) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("%1 header has invalid box/morph offsets.").arg(format_label)
		};
	}

	QStringList lod_preview;
	lod_preview.reserve(10);
	for (int i = 0; i < 10; ++i) {
		qint32 lod = -1;
		if (read_i32_le(bytes, 92 + i * 4, &lod) && lod >= 0) {
			lod_preview.push_back(QString::number(lod));
		}
	}

	qint64 total_triangles = 0;
	qint64 total_vertices = 0;
	int largest_surface = -1;
	qint32 largest_surface_tris = 0;
	QStringList surface_preview;
	surface_preview.reserve(std::min<int>(num_surfaces, kPreviewCount));

	qint64 surface_offset = ofs_surfaces;
	for (int i = 0; i < num_surfaces; ++i) {
		if (surface_offset < 0 || surface_offset + kSurfaceHeaderSize > bytes.size()) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 header is out of bounds.").arg(format_label).arg(i)
			};
		}
		const int base = static_cast<int>(surface_offset);
		quint32 surf_ident = 0;
		qint32 surf_num_triangles = 0;
		qint32 surf_num_verts = 0;
		qint32 surf_ofs_triangles = 0;
		qint32 surf_ofs_verts = 0;
		qint32 surf_ofs_collapse = 0;
		qint32 surf_ofs_end = 0;
		qint32 surf_ofs_collapse_index = 0;
		if (!read_u32_le(bytes, base + 0, &surf_ident) || !read_i32_le(bytes, base + 68, &surf_num_triangles) ||
		    !read_i32_le(bytes, base + 72, &surf_num_verts) || !read_i32_le(bytes, base + 80, &surf_ofs_triangles) ||
		    !read_i32_le(bytes, base + 84, &surf_ofs_verts) || !read_i32_le(bytes, base + 88, &surf_ofs_collapse) ||
		    !read_i32_le(bytes, base + 92, &surf_ofs_end) || !read_i32_le(bytes, base + 96, &surf_ofs_collapse_index)) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 header is truncated.").arg(format_label).arg(i)
			};
		}

		if (surf_num_triangles < 0 || surf_num_triangles > kMaxSurfaceTriangles || surf_num_verts < 0 || surf_num_verts > kMaxSurfaceVerts) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 has unreasonable triangle/vertex counts.").arg(format_label).arg(i)
			};
		}
		if (surf_ofs_end <= kSurfaceHeaderSize) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 has invalid end offset.").arg(format_label).arg(i)
			};
		}

		const qint64 surface_end = surface_offset + surf_ofs_end;
		if (surface_end <= surface_offset || surface_end > bytes.size() || surface_end > ofs_end) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 exceeds file bounds.").arg(format_label).arg(i)
			};
		}

		const auto span_within_surface = [&](qint32 rel_ofs, qint64 len) -> bool {
			if (rel_ofs < 0 || len < 0) {
				return false;
			}
			const qint64 begin = surface_offset + rel_ofs;
			const qint64 end = begin + len;
			return begin >= surface_offset && end >= begin && end <= surface_end;
		};

		const qint64 tri_bytes = static_cast<qint64>(surf_num_triangles) * 3LL * 4LL;
		const qint64 collapse_bytes = static_cast<qint64>(surf_num_verts) * 4LL;
		if (surf_num_triangles > 0 && !span_within_surface(surf_ofs_triangles, tri_bytes)) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 triangle index span is invalid.").arg(format_label).arg(i)
			};
		}
		if (surf_num_verts > 0 && surf_ofs_collapse > 0 && !span_within_surface(surf_ofs_collapse, collapse_bytes)) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 collapse map span is invalid.").arg(format_label).arg(i)
			};
		}
		if (surf_num_verts > 0 && surf_ofs_collapse_index > 0 && !span_within_surface(surf_ofs_collapse_index, collapse_bytes)) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface %2 collapse-index span is invalid.").arg(format_label).arg(i)
			};
		}

		total_triangles += surf_num_triangles;
		total_vertices += surf_num_verts;
		if (surf_num_triangles > largest_surface_tris) {
			largest_surface_tris = surf_num_triangles;
			largest_surface = i;
		}

		if (i < kPreviewCount) {
			const QString surf_name = fixed_c_string(bytes.constData() + base + 4, 64);
			const QString safe_name = surf_name.isEmpty() ? QString("<surface_%1>").arg(i) : surf_name;
			surface_preview.push_back(QString("[%1] %2  (%3 tris, %4 verts, ident %5)")
			                            .arg(i)
			                            .arg(safe_name)
			                            .arg(surf_num_triangles)
			                            .arg(surf_num_verts)
			                            .arg(fourcc_text(surf_ident)));
		}

		if (surface_end <= surface_offset) {
			return IdTechAssetDecodeResult{
				title,
				{},
				QString("%1 surface table made no progress at entry %2.").arg(format_label).arg(i)
			};
		}
		surface_offset = surface_end;
	}

	QString summary;
	QTextStream s(&summary);
	s << "Type: FAKK2/MOHAA skeletal mesh\n";
	s << "Format: " << format_label << "\n";
	s << "Version: " << version << "\n";
	s << "Name: " << (mesh_name.isEmpty() ? "<unnamed>" : mesh_name) << "\n";
	s << "Surfaces: " << num_surfaces << "\n";
	s << "Bones: " << num_bones << "\n";
	s << "Triangles: " << total_triangles << "\n";
	s << "Vertices: " << total_vertices << "\n";
	if (largest_surface >= 0) {
		s << "Largest surface: #" << largest_surface << " (" << largest_surface_tris << " triangles)\n";
	}
	s << "Surface table offset: " << ofs_surfaces << "\n";
	s << "Bones offset: " << ofs_bones << "\n";
	s << "Boxes: " << num_boxes;
	if (num_boxes > 0) {
		s << " (offset " << ofs_boxes << ")";
	}
	s << "\n";
	s << "Morph targets: " << num_morph_targets;
	if (num_morph_targets > 0) {
		s << " (offset " << ofs_morph_targets << ")";
	}
	s << "\n";
	if (!lod_preview.isEmpty()) {
		s << "LOD indices: " << lod_preview.join(", ") << "\n";
	}
	if (has_scale_field) {
		s << "Scale: " << scale << "\n";
	}
	if (!surface_preview.isEmpty()) {
		s << "Surface preview:\n";
		for (const QString& line : surface_preview) {
			s << "  " << line << "\n";
		}
		if (num_surfaces > kPreviewCount) {
			s << "  ... (" << (num_surfaces - kPreviewCount) << " more surfaces)\n";
		}
	}
	if (ofs_end < bytes.size()) {
		s << "Trailing bytes: " << (bytes.size() - ofs_end) << "\n";
	}

	return IdTechAssetDecodeResult{title, summary, {}};
}

IdTechAssetDecodeResult decode_skb(const QByteArray& bytes) {
	constexpr quint32 kSkbIdent = static_cast<quint32>('S') | (static_cast<quint32>('K') << 8) |
	                              (static_cast<quint32>('L') << 16) | (static_cast<quint32>(' ') << 24);
	return decode_skel_mesh(bytes, kSkbIdent, 3, 4, "SKB");
}

IdTechAssetDecodeResult decode_skd(const QByteArray& bytes) {
	constexpr quint32 kSkdIdent = static_cast<quint32>('S') | (static_cast<quint32>('K') << 8) |
	                              (static_cast<quint32>('M') << 16) | (static_cast<quint32>('D') << 24);
	return decode_skel_mesh(bytes, kSkdIdent, 5, 6, "SKD");
}

IdTechAssetDecodeResult decode_skan(const QByteArray& bytes) {
	const QString title = "FAKK2/MOHAA Skeletal Animation (SKAN)";
	constexpr quint32 kSkanIdent = static_cast<quint32>('S') | (static_cast<quint32>('K') << 8) |
	                               (static_cast<quint32>('A') << 16) | (static_cast<quint32>('N') << 24);
	constexpr qint32 kKnownVersionOld = 13;
	constexpr qint32 kKnownVersionProcessed = 14;
	constexpr int kAnimHeaderSize = 108;

	if (bytes.size() < 8) {
		return IdTechAssetDecodeResult{title, {}, "SKAN file is too small."};
	}

	quint32 ident = 0;
	qint32 version = 0;
	if (!read_u32_le(bytes, 0, &ident) || !read_i32_le(bytes, 4, &version)) {
		return IdTechAssetDecodeResult{title, {}, "Unable to parse SKAN header."};
	}
	if (ident != kSkanIdent) {
		return IdTechAssetDecodeResult{
			title,
			{},
			QString("Invalid SKAN magic: expected %1, got %2.")
			  .arg(fourcc_text(kSkanIdent))
			  .arg(fourcc_text(ident))
		};
	}

	QString summary;
	QTextStream s(&summary);
	s << "Type: FAKK2/MOHAA skeletal animation\n";
	s << "Format: SKAN\n";
	s << "Version: " << version;
	if (version != kKnownVersionOld && version != kKnownVersionProcessed) {
		s << " (unrecognized)";
	}
	s << "\n";

	if (bytes.size() < kAnimHeaderSize) {
		s << "Layout: compact/processed animation blob\n";
		s << "Payload bytes (after magic/version): " << (bytes.size() - 8) << "\n";
		return IdTechAssetDecodeResult{title, summary, {}};
	}

	const QString anim_name = fixed_c_string(bytes.constData() + 8, 64);
	qint32 anim_type = 0;
	qint32 num_frames = 0;
	qint32 num_bones = 0;
	float total_time = 0.0f;
	float frame_time = 0.0f;
	float total_delta_x = 0.0f;
	float total_delta_y = 0.0f;
	float total_delta_z = 0.0f;
	qint32 ofs_frames = 0;
	if (!read_i32_le(bytes, 72, &anim_type) || !read_i32_le(bytes, 76, &num_frames) || !read_i32_le(bytes, 80, &num_bones) ||
	    !read_f32_le(bytes, 84, &total_time) || !read_f32_le(bytes, 88, &frame_time) || !read_f32_le(bytes, 92, &total_delta_x) ||
	    !read_f32_le(bytes, 96, &total_delta_y) || !read_f32_le(bytes, 100, &total_delta_z) || !read_i32_le(bytes, 104, &ofs_frames)) {
		return IdTechAssetDecodeResult{title, {}, "Unable to parse SKAN header fields."};
	}

	const bool sane_counts = (num_frames >= 0 && num_frames <= 500000 && num_bones >= 0 && num_bones <= 8192);
	const bool sane_offsets = (ofs_frames >= kAnimHeaderSize && ofs_frames <= bytes.size());
	const bool sane_time = (total_time >= 0.0f && total_time <= 1.0e7f && frame_time >= 0.0f && frame_time <= 60.0f);
	const qint64 frame_stride = 40LL + static_cast<qint64>(num_bones) * 16LL;
	const bool frame_stride_ok = (frame_stride >= 40 && frame_stride <= (32LL * 1024 * 1024));
	const qint64 frame_table_bytes = (num_frames >= 0 && frame_stride_ok) ? static_cast<qint64>(num_frames) * frame_stride : -1;
	const bool frame_table_fits =
	  sane_offsets && frame_stride_ok && frame_table_bytes >= 0 &&
	  (static_cast<qint64>(ofs_frames) + frame_table_bytes <= static_cast<qint64>(bytes.size()));

	s << "Name: " << (anim_name.isEmpty() ? "<unnamed>" : anim_name) << "\n";
	s << "Animation type: " << anim_type << "\n";
	s << "Frames: " << num_frames << "\n";
	s << "Bones: " << num_bones << "\n";
	s << "Frame time: " << frame_time << " s";
	if (frame_time > 0.0f) {
		s << " (~" << (1.0f / frame_time) << " FPS)";
	}
	s << "\n";
	s << "Total time: " << total_time << " s\n";
	s << "Total delta: (" << total_delta_x << ", " << total_delta_y << ", " << total_delta_z << ")\n";
	s << "Frame table offset: " << ofs_frames << "\n";

	if (!sane_counts || !sane_offsets || !sane_time) {
		s << "Header sanity: suspicious values detected\n";
	}

	if (frame_table_fits) {
		s << "Frame data layout: raw SKAN frames (" << frame_stride << " bytes/frame)\n";
		if (num_frames > 0) {
			const int base = ofs_frames;
			float mins_x = 0.0f;
			float mins_y = 0.0f;
			float mins_z = 0.0f;
			float maxs_x = 0.0f;
			float maxs_y = 0.0f;
			float maxs_z = 0.0f;
			float radius = 0.0f;
			float frame_delta_x = 0.0f;
			float frame_delta_y = 0.0f;
			float frame_delta_z = 0.0f;
			if (read_f32_le(bytes, base + 0, &mins_x) && read_f32_le(bytes, base + 4, &mins_y) && read_f32_le(bytes, base + 8, &mins_z) &&
			    read_f32_le(bytes, base + 12, &maxs_x) && read_f32_le(bytes, base + 16, &maxs_y) && read_f32_le(bytes, base + 20, &maxs_z) &&
			    read_f32_le(bytes, base + 24, &radius) && read_f32_le(bytes, base + 28, &frame_delta_x) &&
			    read_f32_le(bytes, base + 32, &frame_delta_y) && read_f32_le(bytes, base + 36, &frame_delta_z)) {
				s << "Frame 0 bounds: mins(" << mins_x << ", " << mins_y << ", " << mins_z << "), maxs(" << maxs_x << ", " << maxs_y << ", "
				  << maxs_z << "), radius " << radius << "\n";
				s << "Frame 0 delta: (" << frame_delta_x << ", " << frame_delta_y << ", " << frame_delta_z << ")\n";
			}
		}
		const qint64 parsed_end = static_cast<qint64>(ofs_frames) + frame_table_bytes;
		if (parsed_end < bytes.size()) {
			s << "Trailing bytes: " << (bytes.size() - parsed_end) << "\n";
		}
	} else {
		s << "Frame data layout: processed/custom packed payload\n";
		s << "Payload bytes after magic/version: " << (bytes.size() - 8) << "\n";
	}

	return IdTechAssetDecodeResult{title, summary, {}};
}
}  // namespace

bool is_supported_idtech_asset_file(const QString& file_name) {
	const QString ext = file_ext_lower(file_name);
	return ext == "spr" || ext == "sp2" || ext == "spr2" || ext == "dm2" || ext == "aas" || ext == "qvm" || ext == "crc" ||
	       ext == "skb" || ext == "skd" || ext == "skc" || ext == "ska" || ext == "tag" || ext == "mdx" || ext == "mds" ||
	       (ext == "dat" && is_quake_progs_dat_file(file_name));
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
	if (ext == "crc") {
		return decode_crc(bytes);
	}
	if (ext == "skb") {
		return decode_skb(bytes);
	}
	if (ext == "skd") {
		return decode_skd(bytes);
	}
	if (ext == "skc" || ext == "ska") {
		return decode_skan(bytes);
	}
	if (ext == "tag") {
		return decode_tag(bytes);
	}
	if (ext == "mdx") {
		return decode_mdx(bytes);
	}
	if (ext == "mds") {
		return decode_mds(bytes);
	}
	if (ext == "dat" && is_quake_progs_dat_file(file_name)) {
		return decode_progs_dat(bytes);
	}

	return IdTechAssetDecodeResult{{}, {}, "Unsupported idTech asset type."};
}
