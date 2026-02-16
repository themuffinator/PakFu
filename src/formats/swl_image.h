#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// Decodes a SiN SWL texture mip level (0 = base/largest).
[[nodiscard]] QImage decode_swl_image(const QByteArray& bytes,
                                      int mip_level,
                                      const QString& texture_name = {},
                                      QString* error = nullptr);
