#pragma once

#include <QByteArray>
#include <QString>

struct IdTechAssetDecodeResult {
	QString type;
	QString summary;
	QString error;

	[[nodiscard]] bool ok() const { return error.isEmpty() && !summary.isEmpty(); }
};

[[nodiscard]] bool is_supported_idtech_asset_file(const QString& file_name);
[[nodiscard]] IdTechAssetDecodeResult decode_idtech_asset_bytes(const QByteArray& bytes, const QString& file_name);
