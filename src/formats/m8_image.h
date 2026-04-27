#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// Decodes a Heretic II M8 texture. M8 embeds a 256-color RGB palette and
// stores up to 16 indexed mip levels (0 = largest). Format behavior is
// credited to the Heretic II source at https://github.com/0lvin/heretic2.
[[nodiscard]] QImage decode_m8_image(const QByteArray& bytes,
                                     int mip_level,
                                     const QString& texture_name = {},
                                     QString* error = nullptr);
