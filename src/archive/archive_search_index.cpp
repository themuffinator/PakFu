#include "archive/archive_search_index.h"

#include <algorithm>

#include <QFileInfo>
#include <QHash>
#include <QtGlobal>

#include "archive/path_safety.h"

namespace {
[[nodiscard]] bool path_starts_with(const QString& path, const QString& prefix) {
	if (path.isEmpty() || prefix.isEmpty()) {
		return false;
	}
#if defined(Q_OS_WIN)
	return path.startsWith(prefix, Qt::CaseInsensitive);
#else
	return path.startsWith(prefix);
#endif
}

[[nodiscard]] QString normalize_dir_prefix(QString path) {
	path = normalize_archive_entry_name(std::move(path));
	if (!path.isEmpty() && !path.endsWith('/')) {
		path += '/';
	}
	return path;
}

[[nodiscard]] QString leaf_name(QString path) {
	path = normalize_archive_entry_name(std::move(path));
	if (path.endsWith('/')) {
		path.chop(1);
	}
	const int slash = path.lastIndexOf('/');
	return slash >= 0 ? path.mid(slash + 1) : path;
}

[[nodiscard]] QString file_ext_lower(const QString& path) {
	const QString leaf = leaf_name(path).toLower();
	const int dot = leaf.lastIndexOf('.');
	return dot >= 0 ? leaf.mid(dot + 1) : QString();
}

[[nodiscard]] bool is_model_ext(const QString& ext) {
	return ext == "mdl" || ext == "md2" || ext == "md3" || ext == "mdc" || ext == "md4" ||
	       ext == "mdr" || ext == "skb" || ext == "skd" || ext == "mdm" || ext == "glm" ||
	       ext == "iqm" || ext == "md5mesh" || ext == "tan" || ext == "fm" || ext == "lwo" || ext == "obj";
}

[[nodiscard]] QStringList dependency_hints_for_path(const QString& path) {
	const QString normalized = normalize_archive_entry_name(path);
	if (normalized.isEmpty() || normalized.endsWith('/')) {
		return {};
	}

	const int slash = normalized.lastIndexOf('/');
	const QString dir = slash >= 0 ? normalized.left(slash + 1) : QString();
	const QString leaf = slash >= 0 ? normalized.mid(slash + 1) : normalized;
	const QFileInfo info(leaf);
	const QString base = info.completeBaseName();
	const QString ext = file_ext_lower(leaf);

	QStringList hints;
	if (is_model_ext(ext)) {
		hints << dir + base + ".skin";
		hints << dir + base + ".png";
		hints << dir + base + ".tga";
		hints << dir + base + ".jpg";
		hints << dir + base + ".jpeg";
		hints << dir + base + ".m8";
		hints << dir + base + ".m32";
	}
	if (ext == "shader") {
		hints << "textures/";
		hints << "models/";
	}
	if (ext == "mtr") {
		hints << "textures/";
		hints << "materials/";
	}
	if (ext == "map") {
		hints << dir + base + ".proc";
		hints << dir + base + ".cm";
		hints << "materials/";
		hints << "textures/";
	}
	if (ext == "proc") {
		hints << dir + base + ".map";
		hints << dir + base + ".cm";
		hints << dir + base + ".aas";
		hints << "materials/";
		hints << "textures/";
	}
	if (ext == "bsp") {
		hints << "scripts/";
		hints << "textures/";
	}
	if (ext == "skin") {
		hints << dir + base + ".md3";
		hints << dir + base + ".mdc";
		hints << dir + base + ".mdr";
	}
	if (ext == "fontdat") {
		hints << dir + base + ".png";
		hints << dir + base + ".tga";
		hints << dir + base + ".jpg";
		hints << dir + base + ".jpeg";
		hints << dir + base + ".dds";
	}

	hints.removeDuplicates();
	return hints;
}

[[nodiscard]] QStringList query_tokens(QString query) {
	query.replace('\\', '/');
	query = query.toLower().simplified();
	if (query.isEmpty()) {
		return {};
	}
	return query.split(' ', Qt::SkipEmptyParts);
}

[[nodiscard]] int score_match(const ArchiveSearchIndex::Item& item, const QStringList& tokens) {
	if (tokens.isEmpty()) {
		return -1;
	}

	const QString path = item.path.toLower();
	const QString leaf = leaf_name(item.path).toLower();
	const QString source = item.source_path.toLower();
	const QString deps = item.dependency_hints.join(' ').toLower();
	const QString kind = item.is_dir ? QStringLiteral("folder directory") : QStringLiteral("file asset");
	const QString state = item.is_overridden ? QStringLiteral(" modified overridden") : (item.is_added ? QStringLiteral(" added new") : QString());
	const QString haystack = path + ' ' + leaf + ' ' + source + ' ' + deps + ' ' + kind + state;

	int score = 0;
	for (const QString& token : tokens) {
		if (!haystack.contains(token)) {
			return -1;
		}
		if (path == token || leaf == token) {
			score += 0;
		} else if (leaf.startsWith(token)) {
			score += 4;
		} else if (path.startsWith(token)) {
			score += 8;
		} else if (path.contains('/' + token)) {
			score += 14;
		} else if (deps.contains(token)) {
			score += 24;
		} else {
			score += 36;
		}
	}

	score += qMin(path.size(), 500);
	if (item.is_dir) {
		score += 10;
	}
	return score;
}
}  // namespace

void ArchiveSearchIndex::clear() {
	items_.clear();
}

void ArchiveSearchIndex::rebuild(const BuildInput& input) {
	items_.clear();

	QSet<QString> deleted_files;
	deleted_files.reserve(input.deleted_files.size());
	for (const QString& path : input.deleted_files) {
		const QString normalized = normalize_archive_entry_name(path);
		if (!normalized.isEmpty()) {
			deleted_files.insert(normalized);
		}
	}

	QVector<QString> deleted_dirs;
	deleted_dirs.reserve(input.deleted_dir_prefixes.size());
	for (const QString& path : input.deleted_dir_prefixes) {
		const QString normalized = normalize_dir_prefix(path);
		if (!normalized.isEmpty()) {
			deleted_dirs.push_back(normalized);
		}
	}

	const auto is_deleted = [&](const QString& path_in) -> bool {
		const QString path = normalize_archive_entry_name(path_in);
		if (path.isEmpty()) {
			return false;
		}
		if (deleted_files.contains(path)) {
			return true;
		}
		for (const QString& dir : deleted_dirs) {
			if (path_starts_with(path, dir)) {
				return true;
			}
		}
		return false;
	};

	QHash<QString, ArchiveEntry> archive_files;
	QSet<QString> dirs;
	archive_files.reserve(input.archive_entries.size());

	const auto add_parent_dirs = [&](const QString& path_in) {
		QString path = normalize_archive_entry_name(path_in);
		if (path.endsWith('/')) {
			path.chop(1);
		}
		int slash = path.indexOf('/');
		while (slash >= 0) {
			const QString dir = path.left(slash + 1);
			if (!dir.isEmpty() && !is_deleted(dir)) {
				dirs.insert(dir);
			}
			slash = path.indexOf('/', slash + 1);
		}
	};

	for (const ArchiveEntry& entry : input.archive_entries) {
		QString path = normalize_archive_entry_name(entry.name);
		if (path.isEmpty() || is_deleted(path) || !is_safe_archive_entry_name(path)) {
			continue;
		}
		if (path.endsWith('/')) {
			dirs.insert(path);
			add_parent_dirs(path);
			continue;
		}
		archive_files.insert(path, entry);
		add_parent_dirs(path);
	}

	for (const QString& dir_in : input.virtual_dirs) {
		const QString dir = normalize_dir_prefix(dir_in);
		if (!dir.isEmpty() && !is_deleted(dir) && is_safe_archive_entry_name(dir)) {
			dirs.insert(dir);
			add_parent_dirs(dir);
		}
	}

	QSet<QString> overlay_paths;
	overlay_paths.reserve(input.overlay_files.size());
	for (const OverlayFile& overlay : input.overlay_files) {
		const QString path = normalize_archive_entry_name(overlay.path);
		if (!path.isEmpty()) {
			overlay_paths.insert(path);
		}
	}

	items_.reserve(archive_files.size() + input.overlay_files.size() + dirs.size());

	QStringList sorted_dirs = dirs.values();
	std::sort(sorted_dirs.begin(), sorted_dirs.end(), [](const QString& a, const QString& b) {
		return a.compare(b, Qt::CaseInsensitive) < 0;
	});
	for (const QString& dir : sorted_dirs) {
		Item item;
		item.path = dir;
		item.scope_label = input.scope_label;
		item.is_dir = true;
		item.mtime_utc_secs = input.fallback_mtime_utc_secs;
		items_.push_back(std::move(item));
	}

	QStringList sorted_files = archive_files.keys();
	std::sort(sorted_files.begin(), sorted_files.end(), [](const QString& a, const QString& b) {
		return a.compare(b, Qt::CaseInsensitive) < 0;
	});
	for (const QString& path : sorted_files) {
		if (overlay_paths.contains(path)) {
			continue;
		}
		const ArchiveEntry entry = archive_files.value(path);
		Item item;
		item.path = path;
		item.scope_label = input.scope_label;
		item.size = entry.size;
		item.mtime_utc_secs = entry.mtime_utc_secs >= 0 ? entry.mtime_utc_secs : input.fallback_mtime_utc_secs;
		item.dependency_hints = dependency_hints_for_path(path);
		items_.push_back(std::move(item));
	}

	for (const OverlayFile& overlay : input.overlay_files) {
		const QString path = normalize_archive_entry_name(overlay.path);
		if (path.isEmpty() || is_deleted(path) || !is_safe_archive_entry_name(path)) {
			continue;
		}
		Item item;
		item.path = path;
		item.source_path = overlay.source_path;
		item.scope_label = input.scope_label;
		item.size = overlay.size;
		item.mtime_utc_secs = overlay.mtime_utc_secs;
		item.is_added = true;
		item.is_overridden = archive_files.contains(path);
		item.dependency_hints = dependency_hints_for_path(path);
		items_.push_back(std::move(item));
	}
}

QVector<ArchiveSearchIndex::Item> ArchiveSearchIndex::search(const QString& query, int max_results) const {
	const QStringList tokens = query_tokens(query);
	if (tokens.isEmpty() || max_results == 0) {
		return {};
	}

	struct ScoredItem {
		Item item;
		int score = 0;
	};

	QVector<ScoredItem> scored;
	scored.reserve(items_.size());
	for (const Item& item : items_) {
		const int score = score_match(item, tokens);
		if (score >= 0) {
			scored.push_back(ScoredItem{item, score});
		}
	}

	std::sort(scored.begin(), scored.end(), [](const ScoredItem& a, const ScoredItem& b) {
		if (a.score != b.score) {
			return a.score < b.score;
		}
		return a.item.path.compare(b.item.path, Qt::CaseInsensitive) < 0;
	});

	if (max_results > 0 && scored.size() > max_results) {
		scored.resize(max_results);
	}

	QVector<Item> out;
	out.reserve(scored.size());
	for (const ScoredItem& item : scored) {
		out.push_back(item.item);
	}
	return out;
}
