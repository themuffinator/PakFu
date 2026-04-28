#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// Decodes a Heretic II M32 texture. M32 stores up to 16 RGBA mip levels with
// extended material metadata. Format behavior is credited to the Heretic II
// source at https://github.com/0lvin/heretic2.
[[nodiscard]] QImage decode_m32_image(const QByteArray& bytes,
                                      int mip_level,
                                      const QString& texture_name = {},
                                      QString* error = nullptr);
