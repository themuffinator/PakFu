#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

struct ImageWriteOptions {
	// Output format extension/id (for example: png, jpg, wal, mip, dds).
	QString format;
	// Quality for lossy encoders (for example JPEG). Range: 1..100.
	int quality = 90;
	// Compression level for supported lossless encoders. Use -1 for encoder default.
	int compression = -1;
	// Paletted conversions: enable dithering.
	bool dither = true;
	// Paletted conversions: alpha <= threshold maps to transparent index (255).
	int alpha_threshold = 127;
	// When writing MIP/LMP, include an embedded 256-color palette trailer.
	bool embed_palette = true;
	// Optional palette override for paletted encoders (must contain 256 entries).
	const QVector<QRgb>* palette = nullptr;
	// Optional texture name hint for legacy texture headers.
	QString texture_name;
};

[[nodiscard]] QString normalize_image_write_format(const QString& format);
[[nodiscard]] bool image_write_format_is_paletted(const QString& format);
[[nodiscard]] bool image_write_format_supports_embedded_palette(const QString& format);
[[nodiscard]] QStringList supported_image_write_formats();

// Writes an image file using Qt writers and custom legacy format encoders.
[[nodiscard]] bool write_image_file(const QImage& image,
                                    const QString& file_path,
                                    const ImageWriteOptions& options,
                                    QString* error);

