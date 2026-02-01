#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

struct ImageDecodeResult {
	QImage image;
	QString error;

	[[nodiscard]] bool ok() const { return !image.isNull(); }
};

struct ImageDecodeOptions {
	// Optional 256-color palette for paletted formats that do not embed one (e.g. Quake II WAL).
	const QVector<QRgb>* palette = nullptr;
};

[[nodiscard]] ImageDecodeResult decode_image_bytes(const QByteArray& bytes, const QString& file_name, const ImageDecodeOptions& options = {});
[[nodiscard]] ImageDecodeResult decode_image_file(const QString& file_path, const ImageDecodeOptions& options = {});
