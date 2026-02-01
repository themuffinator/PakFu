#pragma once

#include <QDir>
#include <QString>

// Common archive path utilities shared by PAK/ZIP backends and UI/CLI.
//
// Rules:
// - Archive entry names use forward slashes.
// - Names must be relative (no leading '/', no drive letters, no '..' segments).
// - Trailing slash is preserved when present (directory-like entries).
[[nodiscard]] inline QString normalize_archive_entry_name(QString name) {
	const bool want_trailing_slash = name.endsWith('/') || name.endsWith('\\');
	name.replace('\\', '/');
	while (name.startsWith('/')) {
		name.remove(0, 1);
	}
	name = QDir::cleanPath(name);
	name.replace('\\', '/');
	if (name == ".") {
		name.clear();
	}
	name = name.trimmed();
	if (want_trailing_slash && !name.isEmpty() && !name.endsWith('/')) {
		name += '/';
	}
	return name;
}

[[nodiscard]] inline bool is_safe_archive_entry_name(const QString& name) {
	if (name.isEmpty()) {
		return false;
	}
	if (name.contains('\\') || name.contains(':')) {
		return false;
	}
	if (name.startsWith('/') || name.startsWith("./") || name.startsWith("../")) {
		return false;
	}
	const QStringList parts = name.split('/', Qt::SkipEmptyParts);
	for (const QString& p : parts) {
		if (p == "." || p == "..") {
			return false;
		}
	}
	return true;
}

