#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

[[nodiscard]] QImage decode_tga_image(const QByteArray& bytes, QString* error);

