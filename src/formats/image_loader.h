#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

struct ImageDecodeResult {
	QImage image;
	QString error;

	[[nodiscard]] bool ok() const { return !image.isNull(); }
};

[[nodiscard]] ImageDecodeResult decode_image_bytes(const QByteArray& bytes, const QString& file_name);
[[nodiscard]] ImageDecodeResult decode_image_file(const QString& file_path);

