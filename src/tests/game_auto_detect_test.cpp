#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "game/game_auto_detect.h"

namespace {
void fail_message(const QString& message) {
	QTextStream(stderr) << message << '\n';
}

void set_error(QString* error, const QString& message) {
	if (error) {
		*error = message;
	}
}

bool write_file(const QString& path, const QByteArray& bytes, QString* error) {
	const QFileInfo info(path);
	if (!QDir().mkpath(info.absolutePath())) {
		set_error(error, "Failed to create fixture directory: " + info.absolutePath());
		return false;
	}

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		set_error(error, "Failed to create fixture file: " + path);
		return false;
	}
	if (file.write(bytes) != bytes.size()) {
		set_error(error, "Failed to write fixture file: " + path);
		return false;
	}
	return true;
}

bool expect_detected(const QString& path, GameId expected, QString* error) {
	const std::optional<GameId> detected = detect_game_id_for_path(path);
	if (!detected) {
		set_error(error, "No game detected for path: " + path);
		return false;
	}
	if (*detected != expected) {
		set_error(error,
		          QString("Unexpected game for %1: expected %2, got %3")
		            .arg(path, game_display_name(expected), game_display_name(*detected)));
		return false;
	}
	return true;
}

bool expect_game_key(const QString& key, GameId expected, QString* error) {
	bool ok = false;
	const GameId actual = game_id_from_key(key, &ok);
	if (!ok || actual != expected) {
		set_error(error, "Unexpected game id for key alias: " + key);
		return false;
	}
	return true;
}

bool run_test(QString* error) {
	bool found_h2 = false;
	for (const GameId id : supported_game_ids()) {
		if (id == GameId::Heretic2) {
			found_h2 = true;
			break;
		}
	}
	if (!found_h2) {
		set_error(error, "Heretic II is missing from supported_game_ids().");
		return false;
	}

	for (const QString& key : {QStringLiteral("heretic2"), QStringLiteral("heretic_2"),
	                          QStringLiteral("hereticii"), QStringLiteral("heretic_ii"),
	                          QStringLiteral("heretic-ii"), QStringLiteral("h2"),
	                          QStringLiteral("htic2")}) {
		if (!expect_game_key(key, GameId::Heretic2, error)) {
			return false;
		}
	}

	QTemporaryDir temp;
	if (!temp.isValid()) {
		set_error(error, "Failed to create temporary directory.");
		return false;
	}

	const QString full_root = QDir(temp.path()).filePath("Heretic II");
	const QString full_base = QDir(full_root).filePath("base");
	const QString htic2_pak = QDir(full_base).filePath("HTIC2-2.PAK");
	const QString os_dir = QDir(full_base).filePath("ds");
	if (!write_file(htic2_pak, QByteArray("PACK", 4), error)) {
		return false;
	}
	if (!QDir().mkpath(os_dir)) {
		set_error(error, "Failed to create Heretic II dynamic-script marker directory.");
		return false;
	}
	if (!write_file(QDir(os_dir).filePath("entity.os"), QByteArray("OS", 2), error)) {
		return false;
	}

	if (!expect_detected(full_root, GameId::Heretic2, error) ||
	    !expect_detected(full_base, GameId::Heretic2, error) ||
	    !expect_detected(htic2_pak, GameId::Heretic2, error) ||
	    !expect_detected(QDir(os_dir).filePath("entity.os"), GameId::Heretic2, error)) {
		return false;
	}

	const QString legacy_root = QDir(temp.path()).filePath("HERETIC2");
	const QString legacy_pak = QDir(legacy_root).filePath("htic2-0.pak");
	if (!write_file(legacy_pak, QByteArray("PACK", 4), error)) {
		return false;
	}
	if (!expect_detected(legacy_root, GameId::Heretic2, error) ||
	    !expect_detected(legacy_pak, GameId::Heretic2, error)) {
		return false;
	}

	const QString book_only_root = QDir(temp.path()).filePath("Heretic2");
	const QString book_dir = QDir(book_only_root).filePath("base/book");
	if (!QDir().mkpath(book_dir)) {
		set_error(error, "Failed to create Heretic II book marker directory.");
		return false;
	}
	if (!expect_detected(book_only_root, GameId::Heretic2, error) ||
	    !expect_detected(book_dir, GameId::Heretic2, error)) {
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
		fail_message(error.isEmpty() ? "Game auto-detect test failed." : error);
		return 1;
	}
	return 0;
}
