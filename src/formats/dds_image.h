#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

[[nodiscard]] QImage decode_dds_image(const QByteArray& bytes, QString* error = nullptr);

