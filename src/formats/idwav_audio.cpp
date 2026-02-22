#include "formats/idwav_audio.h"

#include <algorithm>
#include <QFileInfo>

namespace {
constexpr quint32 kSoundMagicIdmsa = 0x6D7A7274u;

constexpr quint16 kFormatPcm = 0x0001u;
constexpr quint16 kFormatAdpcm = 0x0002u;
constexpr quint16 kFormatXma2 = 0x0166u;
constexpr quint16 kFormatExtensible = 0xFFFFu;

constexpr int kAdpcmExtraBytes = 32;
constexpr int kExtensibleExtraBytes = 22;
constexpr int kXma2ExtraBytes = 34;

constexpr int kMaxAmplitudeBytes = 64 * 1024 * 1024;
constexpr int kMaxBufferCount = 1 << 20;
constexpr int kMaxAudioBytes = 1024 * 1024 * 1024;

struct ParsedIdWav {
	qint64 timestamp = 0;
	quint16 format_tag = 0;
	quint16 channels = 0;
	quint32 sample_rate = 0;
	quint32 avg_bytes_per_sec = 0;
	quint16 block_align = 0;
	quint16 bits_per_sample = 0;
	quint16 extra_size = 0;
	QByteArray extra_data;
	int play_begin = 0;
	int play_length = 0;
	int total_buffer_size = 0;
	int buffer_count = 0;
	QByteArray audio_data;
};

[[nodiscard]] bool span_fits(const QByteArray& bytes, qint64 offset, qint64 len) {
	if (offset < 0 || len < 0) {
		return false;
	}
	const qint64 size = bytes.size();
	return offset <= size && len <= (size - offset);
}

[[nodiscard]] bool read_u8(const QByteArray& bytes, qint64* offset, quint8* out) {
	if (!offset || !out || !span_fits(bytes, *offset, 1)) {
		return false;
	}
	*out = static_cast<quint8>(bytes[static_cast<int>(*offset)]);
	*offset += 1;
	return true;
}

[[nodiscard]] bool read_u16_le(const QByteArray& bytes, qint64* offset, quint16* out) {
	if (!offset || !out || !span_fits(bytes, *offset, 2)) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + *offset);
	*out = static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8);
	*offset += 2;
	return true;
}

[[nodiscard]] bool read_u32_le(const QByteArray& bytes, qint64* offset, quint32* out) {
	if (!offset || !out || !span_fits(bytes, *offset, 4)) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + *offset);
	*out = static_cast<quint32>(p[0]) |
	       (static_cast<quint32>(p[1]) << 8) |
	       (static_cast<quint32>(p[2]) << 16) |
	       (static_cast<quint32>(p[3]) << 24);
	*offset += 4;
	return true;
}

[[nodiscard]] bool read_u32_be(const QByteArray& bytes, qint64* offset, quint32* out) {
	if (!offset || !out || !span_fits(bytes, *offset, 4)) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + *offset);
	*out = (static_cast<quint32>(p[0]) << 24) |
	       (static_cast<quint32>(p[1]) << 16) |
	       (static_cast<quint32>(p[2]) << 8) |
	       static_cast<quint32>(p[3]);
	*offset += 4;
	return true;
}

[[nodiscard]] bool read_i32_be(const QByteArray& bytes, qint64* offset, qint32* out) {
	quint32 raw = 0;
	if (!read_u32_be(bytes, offset, &raw) || !out) {
		return false;
	}
	*out = static_cast<qint32>(raw);
	return true;
}

[[nodiscard]] bool read_i64_be(const QByteArray& bytes, qint64* offset, qint64* out) {
	if (!offset || !out || !span_fits(bytes, *offset, 8)) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + *offset);
	const quint64 raw = (static_cast<quint64>(p[0]) << 56) |
	                    (static_cast<quint64>(p[1]) << 48) |
	                    (static_cast<quint64>(p[2]) << 40) |
	                    (static_cast<quint64>(p[3]) << 32) |
	                    (static_cast<quint64>(p[4]) << 24) |
	                    (static_cast<quint64>(p[5]) << 16) |
	                    (static_cast<quint64>(p[6]) << 8) |
	                    static_cast<quint64>(p[7]);
	*out = static_cast<qint64>(raw);
	*offset += 8;
	return true;
}

void append_u16_le(QByteArray* out, quint16 value) {
	if (!out) {
		return;
	}
	out->append(static_cast<char>(value & 0xFFu));
	out->append(static_cast<char>((value >> 8) & 0xFFu));
}

void append_u32_le(QByteArray* out, quint32 value) {
	if (!out) {
		return;
	}
	out->append(static_cast<char>(value & 0xFFu));
	out->append(static_cast<char>((value >> 8) & 0xFFu));
	out->append(static_cast<char>((value >> 16) & 0xFFu));
	out->append(static_cast<char>((value >> 24) & 0xFFu));
}

void patch_u32_le(QByteArray* out, int offset, quint32 value) {
	if (!out || offset < 0 || offset + 4 > out->size()) {
		return;
	}
	(*out)[offset + 0] = static_cast<char>(value & 0xFFu);
	(*out)[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
	(*out)[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
	(*out)[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
}

void append_chunk(QByteArray* out, const char id[4], const QByteArray& payload) {
	if (!out) {
		return;
	}
	out->append(id, 4);
	append_u32_le(out, static_cast<quint32>(payload.size()));
	if (!payload.isEmpty()) {
		out->append(payload);
	}
	if (payload.size() & 1) {
		out->append('\0');
	}
}

[[nodiscard]] const char* codec_name_for_tag(quint16 tag) {
	switch (tag) {
		case kFormatPcm:
			return "PCM";
		case kFormatAdpcm:
			return "MS ADPCM";
		case kFormatXma2:
			return "XMA2";
		case kFormatExtensible:
			return "WAVE_FORMAT_EXTENSIBLE";
		default:
			return "Unknown";
	}
}

[[nodiscard]] bool parse_idwav(const QByteArray& bytes, ParsedIdWav* out, QString* error) {
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = "Internal parse error.";
		}
		return false;
	}

	qint64 offset = 0;
	quint32 magic = 0;
	if (!read_u32_be(bytes, &offset, &magic) || magic != kSoundMagicIdmsa) {
		if (error) {
			*error = "Not a valid Doom 3 BFG IDWAV file.";
		}
		return false;
	}

	if (!read_i64_be(bytes, &offset, &out->timestamp)) {
		if (error) {
			*error = "IDWAV header is truncated (timestamp).";
		}
		return false;
	}

	quint8 loaded_flag = 0;
	if (!read_u8(bytes, &offset, &loaded_flag)) {
		if (error) {
			*error = "IDWAV header is truncated (loaded flag).";
		}
		return false;
	}
	Q_UNUSED(loaded_flag);

	if (!read_i32_be(bytes, &offset, &out->play_begin) || !read_i32_be(bytes, &offset, &out->play_length)) {
		if (error) {
			*error = "IDWAV header is truncated (play range).";
		}
		return false;
	}

	if (!read_u16_le(bytes, &offset, &out->format_tag) ||
	    !read_u16_le(bytes, &offset, &out->channels) ||
	    !read_u32_le(bytes, &offset, &out->sample_rate) ||
	    !read_u32_le(bytes, &offset, &out->avg_bytes_per_sec) ||
	    !read_u16_le(bytes, &offset, &out->block_align) ||
	    !read_u16_le(bytes, &offset, &out->bits_per_sample)) {
		if (error) {
			*error = "IDWAV wave format header is truncated.";
		}
		return false;
	}

	if (out->channels == 0 || out->sample_rate == 0 || out->block_align == 0) {
		if (error) {
			*error = "IDWAV has invalid wave format values.";
		}
		return false;
	}

	if (out->format_tag != kFormatPcm) {
		quint16 extra_size = 0;
		if (!read_u16_le(bytes, &offset, &extra_size)) {
			if (error) {
				*error = "IDWAV wave format extra header is truncated.";
			}
			return false;
		}
		out->extra_size = extra_size;
		if (!span_fits(bytes, offset, out->extra_size)) {
			if (error) {
				*error = "IDWAV wave format extra data is truncated.";
			}
			return false;
		}
		out->extra_data = bytes.mid(static_cast<int>(offset), out->extra_size);
		offset += out->extra_size;
	}

	switch (out->format_tag) {
		case kFormatPcm:
			break;
		case kFormatAdpcm:
			if (out->extra_size != kAdpcmExtraBytes) {
				if (error) {
					*error = QString("Unsupported IDWAV ADPCM extra format size: %1.").arg(out->extra_size);
				}
				return false;
			}
			break;
		case kFormatExtensible:
			if (out->extra_size != kExtensibleExtraBytes) {
				if (error) {
					*error = QString("Unsupported IDWAV extensible format size: %1.").arg(out->extra_size);
				}
				return false;
			}
			break;
		case kFormatXma2:
			if (out->extra_size != kXma2ExtraBytes) {
				if (error) {
					*error = QString("Unsupported IDWAV XMA2 extra format size: %1.").arg(out->extra_size);
				}
				return false;
			}
			break;
		default:
			if (error) {
				*error = QString("Unsupported IDWAV codec tag: 0x%1.")
				           .arg(QString::number(out->format_tag, 16).rightJustified(4, QLatin1Char('0')));
			}
			return false;
	}

	qint32 amplitude_size = 0;
	if (!read_i32_be(bytes, &offset, &amplitude_size)) {
		if (error) {
			*error = "IDWAV amplitude section is truncated.";
		}
		return false;
	}
	if (amplitude_size < 0 || amplitude_size > kMaxAmplitudeBytes || !span_fits(bytes, offset, amplitude_size)) {
		if (error) {
			*error = "IDWAV amplitude section is invalid.";
		}
		return false;
	}
	offset += amplitude_size;

	if (!read_i32_be(bytes, &offset, &out->total_buffer_size) || !read_i32_be(bytes, &offset, &out->buffer_count)) {
		if (error) {
			*error = "IDWAV buffer table is truncated.";
		}
		return false;
	}
	if (out->total_buffer_size < 0 || out->buffer_count < 0 || out->buffer_count > kMaxBufferCount) {
		if (error) {
			*error = "IDWAV buffer table is invalid.";
		}
		return false;
	}

	const int reserve_bytes = std::max(out->total_buffer_size, 0);
	out->audio_data.reserve(reserve_bytes);

	qint64 sum_buffer_bytes = 0;
	for (int i = 0; i < out->buffer_count; ++i) {
		qint32 num_samples = 0;
		qint32 buffer_size = 0;
		if (!read_i32_be(bytes, &offset, &num_samples) || !read_i32_be(bytes, &offset, &buffer_size)) {
			if (error) {
				*error = QString("IDWAV buffer header %1 is truncated.").arg(i);
			}
			return false;
		}
		if (buffer_size < 0 || !span_fits(bytes, offset, buffer_size)) {
			if (error) {
				*error = QString("IDWAV buffer %1 is invalid.").arg(i);
			}
			return false;
		}
		Q_UNUSED(num_samples);

		sum_buffer_bytes += buffer_size;
		if (sum_buffer_bytes > kMaxAudioBytes) {
			if (error) {
				*error = "IDWAV audio payload exceeds safety limits.";
			}
			return false;
		}

		if (buffer_size > 0) {
			out->audio_data.append(bytes.constData() + static_cast<int>(offset), buffer_size);
		}
		offset += buffer_size;
	}

	if (out->audio_data.isEmpty()) {
		if (error) {
			*error = "IDWAV contains no audio payload.";
		}
		return false;
	}

	return true;
}

[[nodiscard]] QByteArray build_wav_bytes(const ParsedIdWav& parsed) {
	QByteArray fmt_payload;
	fmt_payload.reserve(16 + (parsed.format_tag == kFormatPcm ? 0 : (2 + parsed.extra_data.size())));
	append_u16_le(&fmt_payload, parsed.format_tag);
	append_u16_le(&fmt_payload, parsed.channels);
	append_u32_le(&fmt_payload, parsed.sample_rate);
	append_u32_le(&fmt_payload, parsed.avg_bytes_per_sec);
	append_u16_le(&fmt_payload, parsed.block_align);
	append_u16_le(&fmt_payload, parsed.bits_per_sample);
	if (parsed.format_tag != kFormatPcm) {
		append_u16_le(&fmt_payload, parsed.extra_size);
		if (!parsed.extra_data.isEmpty()) {
			fmt_payload.append(parsed.extra_data);
		}
	}

	QByteArray out;
	out.reserve(12 + 8 + fmt_payload.size() + 8 + parsed.audio_data.size() + 8);
	out.append("RIFF", 4);
	append_u32_le(&out, 0);  // Filled after chunks are appended.
	out.append("WAVE", 4);
	append_chunk(&out, "fmt ", fmt_payload);
	append_chunk(&out, "data", parsed.audio_data);

	const quint32 riff_size = static_cast<quint32>(out.size() - 8);
	patch_u32_le(&out, 4, riff_size);
	return out;
}
}  // namespace

bool is_idwav_file_name(const QString& file_name) {
	const QString ext = QFileInfo(file_name).suffix().toLower();
	return ext == "idwav";
}

IdWavDecodeResult decode_idwav_to_wav_bytes(const QByteArray& bytes) {
	IdWavDecodeResult result;

	ParsedIdWav parsed;
	if (!parse_idwav(bytes, &parsed, &result.error)) {
		return result;
	}

	result.codec_name = codec_name_for_tag(parsed.format_tag);
	result.format_tag = parsed.format_tag;
	result.channels = parsed.channels;
	result.sample_rate = static_cast<int>(parsed.sample_rate);
	result.bits_per_sample = parsed.bits_per_sample;
	result.buffer_count = parsed.buffer_count;
	result.data_bytes = parsed.audio_data.size();
	result.timestamp = parsed.timestamp;
	result.play_begin = parsed.play_begin;
	result.play_length = parsed.play_length;

	if (parsed.format_tag == kFormatXma2) {
		result.error = "IDWAV uses XMA2 audio, which is not currently supported for playback.";
		return result;
	}

	if (parsed.format_tag != kFormatPcm && parsed.format_tag != kFormatAdpcm && parsed.format_tag != kFormatExtensible) {
		result.error = QString("IDWAV codec %1 is not currently supported for playback.").arg(result.codec_name);
		return result;
	}

	result.wav_bytes = build_wav_bytes(parsed);
	if (result.wav_bytes.isEmpty()) {
		result.error = "Failed to convert IDWAV payload to WAV.";
	}

	return result;
}
