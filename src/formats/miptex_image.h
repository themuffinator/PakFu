#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// Decodes a Quake/Half-Life "miptex" texture (commonly stored in WAD2/WAD3 files).
//
// - If an embedded palette is present (WAD3-style), it is used.
// - Otherwise, an external 256-color Quake palette must be provided.
// - mip_level selects which mip to decode (0 = largest).
[[nodiscard]] QImage decode_miptex_image(const QByteArray& bytes,
                                        const QVector<QRgb>* external_palette,
                                        int mip_level,
                                        QString* error = nullptr);
