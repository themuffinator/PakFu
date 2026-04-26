#include <QCoreApplication>
#include <QTextStream>

#include "pakfu_core.h"

namespace {
void fail_message(const QString& message) {
	QTextStream(stderr) << message << '\n';
}

bool run_test(QString* error) {
	Archive archive;
	if (archive.is_loaded()) {
		if (error) {
			*error = "Archive default state should be unloaded.";
		}
		return false;
	}

	ArchiveSearchIndex index;
	if (!index.items().isEmpty()) {
		if (error) {
			*error = "ArchiveSearchIndex should default to an empty item set.";
		}
		return false;
	}

	const QStringList extension_dirs = default_extension_search_dirs();
	if (extension_dirs.isEmpty()) {
		if (error) {
			*error = "Expected at least one default extension search directory.";
		}
		return false;
	}

	ImageDecodeOptions decode_options;
	if (decode_options.mip_level != 0) {
		if (error) {
			*error = "ImageDecodeOptions should default to mip level 0.";
		}
		return false;
	}

	LoadedModel model;
	if (model.frame_count != 1 || model.surface_count != 1) {
		if (error) {
			*error = "LoadedModel defaults changed unexpectedly.";
		}
		return false;
	}

	const QVector<GameId> games = supported_game_ids();
	if (games.isEmpty()) {
		if (error) {
			*error = "supported_game_ids() returned no games.";
		}
		return false;
	}
	if (game_display_name(games.first()).trimmed().isEmpty()) {
		if (error) {
			*error = "First supported game has an empty display name.";
		}
		return false;
	}

	return true;
}
}  // namespace

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	Q_UNUSED(app);

	QString error;
	if (!run_test(&error)) {
		fail_message(error.isEmpty() ? "Core API smoke test failed." : error);
		return 1;
	}
	return 0;
}
