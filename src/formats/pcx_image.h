#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// Extracts a 256-color palette from a PCX file (version 5 extension).
// Returns false if a 256-color palette is not present.
[[nodiscard]] bool extract_pcx_palette_256(const QByteArray& bytes, QVector<QRgb>* out_palette, QString* error);

// Decodes a PCX image (common Quake/Quake2 usage: 8bpp paletted + RLE).
[[nodiscard]] QImage decode_pcx_image(const QByteArray& bytes, QString* error);

