#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// Decodes a Ritual/OpenMOHAA FTX texture (RGBA8 payload after a 12-byte header).
[[nodiscard]] QImage decode_ftx_image(const QByteArray& bytes, QString* error = nullptr);
