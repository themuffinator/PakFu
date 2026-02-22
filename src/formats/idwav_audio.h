#pragma once

#include <QByteArray>
#include <QString>

struct IdWavDecodeResult {
	QByteArray wav_bytes;
	QString error;
	QString codec_name;
	quint16 format_tag = 0;
	int channels = 0;
	int sample_rate = 0;
	int bits_per_sample = 0;
	int buffer_count = 0;
	int data_bytes = 0;
	qint64 timestamp = 0;
	int play_begin = 0;
	int play_length = 0;

	[[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] bool is_idwav_file_name(const QString& file_name);
[[nodiscard]] IdWavDecodeResult decode_idwav_to_wav_bytes(const QByteArray& bytes);
