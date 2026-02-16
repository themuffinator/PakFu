#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// Extracts a 256-color palette from a Quake palette.lmp file (768 bytes: 256 * RGB).
[[nodiscard]] bool extract_lmp_palette_256(const QByteArray& bytes, QVector<QRgb>* out_palette, QString* error);

// Decodes common idTech/GoldSrc .lmp "image" files:
// - QPIC: 32-bit LE width/height header + 8bpp indices (Quake)
// - WAD3/GoldSrc QPIC: QPIC data + embedded palette trailer (Half-Life)
// - conchars.lmp: raw 128x128 8bpp indices (no header)
// - palette.lmp: shown as a 16x16 palette grid
[[nodiscard]] QImage decode_lmp_image(const QByteArray& bytes, const QString& file_name, const QVector<QRgb>* palette, QString* error);
