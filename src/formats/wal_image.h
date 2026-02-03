#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// Decodes a Quake II WAL texture using the provided 256-color palette.
// Returns a composite image that shows all 4 mip levels in a grid.
[[nodiscard]] QImage decode_wal_image_with_mips(const QByteArray& bytes, const QVector<QRgb>& palette, QString* error);

// Decodes only the base (mip0) WAL texture.
[[nodiscard]] QImage decode_wal_image(const QByteArray& bytes, const QVector<QRgb>& palette, QString* error);
