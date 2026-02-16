#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

#include <functional>

#include "formats/image_loader.h"

struct SpriteFrame {
	QImage image;
	int duration_ms = 100;
	QString name;
	int origin_x = 0;
	int origin_y = 0;
};

struct SpriteDecodeResult {
	QString format;
	int nominal_width = 0;
	int nominal_height = 0;
	QVector<SpriteFrame> frames;
	QString error;

	[[nodiscard]] bool ok() const { return error.isEmpty() && !frames.isEmpty(); }
};

using Sp2FrameLoader = std::function<ImageDecodeResult(const QString& frame_name)>;

[[nodiscard]] SpriteDecodeResult decode_spr_sprite(const QByteArray& bytes, const QVector<QRgb>* palette);
[[nodiscard]] SpriteDecodeResult decode_sp2_sprite(const QByteArray& bytes, const Sp2FrameLoader& frame_loader);
