#pragma once

#include <QString>
#include <QStringList>

#include "archive/archive.h"
#include "archive/archive_session.h"
#include "archive/archive_search_index.h"
#include "extensions/extension_plugin.h"
#include "formats/bsp_preview.h"
#include "formats/cinematic.h"
#include "formats/fontdat_font.h"
#include "formats/idtech_asset_loader.h"
#include "formats/idtech4_map.h"
#include "formats/idwav_audio.h"
#include "formats/image_loader.h"
#include "formats/image_writer.h"
#include "formats/model.h"
#include "formats/quake3_shader.h"
#include "formats/quake3_skin.h"
#include "formats/sprite_loader.h"
#include "game/game_auto_detect.h"
#include "game/game_set.h"

namespace PakFu::Core {

inline constexpr int kApiMajor = 0;
inline constexpr int kApiMinor = 1;
inline constexpr int kApiPatch = 0;
inline constexpr const char* kApiVersion = "0.1.0";
inline constexpr const char* kApiStability = "provisional-source";

[[nodiscard]] inline QString api_version_string() {
	return QString::fromLatin1(kApiVersion);
}

[[nodiscard]] inline QString api_stability_label() {
	return QString::fromLatin1(kApiStability);
}

[[nodiscard]] inline QStringList public_capabilities() {
	return {
		QStringLiteral("archive.load"),
		QStringLiteral("archive.extract"),
		QStringLiteral("archive.search"),
		QStringLiteral("archive.session"),
		QStringLiteral("archive.write.pak"),
		QStringLiteral("archive.write.wad2"),
		QStringLiteral("archive.write.zip"),
		QStringLiteral("format.image.decode"),
		QStringLiteral("format.image.write"),
		QStringLiteral("format.model.load"),
		QStringLiteral("format.bsp.preview"),
		QStringLiteral("format.cinematic.decode"),
		QStringLiteral("format.fontdat.decode"),
		QStringLiteral("format.fontdat.render"),
		QStringLiteral("format.idtech.inspect"),
		QStringLiteral("format.idtech4.map.inspect"),
		QStringLiteral("format.idtech4.proc.mesh"),
		QStringLiteral("format.idtech4.material.parse"),
		QStringLiteral("game.profile.detect"),
		QStringLiteral("extension.command"),
		QStringLiteral("extension.import"),
	};
}

[[nodiscard]] inline bool has_public_capability(const QString& capability) {
	return public_capabilities().contains(capability);
}

}  // namespace PakFu::Core
