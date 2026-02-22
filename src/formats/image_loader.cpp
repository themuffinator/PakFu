#include "formats/image_loader.h"

#include <utility>

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include "formats/dds_image.h"
#include "formats/ftx_image.h"
#include "formats/lmp_image.h"
#include "formats/miptex_image.h"
#include "formats/pcx_image.h"
#include "formats/swl_image.h"
#include "formats/tga_image.h"
#include "formats/wal_image.h"

namespace {
QString file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return dot >= 0 ? lower.mid(dot + 1) : QString();
}

ImageDecodeResult decode_with_qt_reader(const QByteArray& bytes, const QByteArray& format_hint) {
	QBuffer buffer;
	buffer.setData(bytes);
	if (!buffer.open(QIODevice::ReadOnly)) {
		return ImageDecodeResult{QImage(), "Unable to open image buffer."};
	}

	QImageReader reader(&buffer, format_hint);
	reader.setAutoTransform(true);
	QImage image = reader.read();
	if (image.isNull()) {
		return ImageDecodeResult{QImage(), reader.errorString().isEmpty() ? "Unable to decode image." : reader.errorString()};
	}
	return ImageDecodeResult{std::move(image), QString()};
}

ImageDecodeResult decode_with_qt_reader(const QString& file_path, const QByteArray& format_hint) {
	QImageReader reader(file_path, format_hint);
	reader.setAutoTransform(true);
	QImage image = reader.read();
	if (image.isNull()) {
		return ImageDecodeResult{QImage(), reader.errorString().isEmpty() ? "Unable to decode image." : reader.errorString()};
	}
	return ImageDecodeResult{std::move(image), QString()};
}
}  // namespace

ImageDecodeResult decode_image_bytes(const QByteArray& bytes, const QString& file_name, const ImageDecodeOptions& options) {
	if (bytes.isEmpty()) {
		return ImageDecodeResult{QImage(), "Empty image data."};
	}

	const QString ext = file_ext_lower(file_name);
	if (ext == "tga") {
		QString err;
		QImage image = decode_tga_image(bytes, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode TGA image." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "pcx") {
		QString err;
		QImage image = decode_pcx_image(bytes, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode PCX image." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "wal") {
		if (!options.palette || options.palette->size() != 256) {
			return ImageDecodeResult{QImage(), "WAL textures require a 256-color palette (Quake II: pics/colormap.pcx)."};
		}
		QString err;
		QImage image = decode_wal_image(bytes, *options.palette, options.mip_level, file_name, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode WAL texture." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "swl") {
		QString err;
		QImage image = decode_swl_image(bytes, options.mip_level, file_name, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode SWL texture." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "dds") {
		QString err;
		QImage image = decode_dds_image(bytes, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode DDS image." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "ftx") {
		QString err;
		QImage image = decode_ftx_image(bytes, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode FTX image." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "lmp") {
		QString err;
		QImage image = decode_lmp_image(bytes, file_name, options.palette, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode LMP image." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}
	if (ext == "mip") {
		QString err;
		QImage image = decode_miptex_image(bytes, options.palette, options.mip_level, file_name, &err);
		if (image.isNull()) {
			return ImageDecodeResult{QImage(), err.isEmpty() ? "Unable to decode MIP texture." : err};
		}
		return ImageDecodeResult{std::move(image), QString()};
	}

	if (ext == "png") {
		return decode_with_qt_reader(bytes, "png");
	}
	if (ext == "jpg" || ext == "jpeg") {
		return decode_with_qt_reader(bytes, "jpeg");
	}

	return decode_with_qt_reader(bytes, QByteArray());
}

ImageDecodeResult decode_image_file(const QString& file_path, const ImageDecodeOptions& options) {
	if (file_path.isEmpty()) {
		return ImageDecodeResult{QImage(), "Empty image path."};
	}

	const QFileInfo info(file_path);
	if (!info.exists()) {
		return ImageDecodeResult{QImage(), "Image file not found."};
	}

	const QString ext = file_ext_lower(info.fileName());
	if (ext == "tga") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "pcx") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "wal") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "swl") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "dds") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "ftx") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "lmp") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}
	if (ext == "mip") {
		QFile f(file_path);
		if (!f.open(QIODevice::ReadOnly)) {
			return ImageDecodeResult{QImage(), "Unable to open image file."};
		}
		const QByteArray bytes = f.readAll();
		return decode_image_bytes(bytes, info.fileName(), options);
	}

	if (ext == "png") {
		return decode_with_qt_reader(file_path, "png");
	}
	if (ext == "jpg" || ext == "jpeg") {
		return decode_with_qt_reader(file_path, "jpeg");
	}

	return decode_with_qt_reader(file_path, QByteArray());
}
