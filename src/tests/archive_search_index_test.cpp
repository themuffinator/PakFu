#include <QCoreApplication>
#include <QTextStream>

#include "archive/archive_search_index.h"

namespace {
void fail_message(const QString& message) {
	QTextStream(stderr) << message << '\n';
}

ArchiveEntry entry(QString name, quint32 size) {
	ArchiveEntry out;
	out.name = std::move(name);
	out.size = size;
	out.mtime_utc_secs = 10;
	return out;
}

bool has_path(const QVector<ArchiveSearchIndex::Item>& items, const QString& path) {
	for (const ArchiveSearchIndex::Item& item : items) {
		if (item.path == path) {
			return true;
		}
	}
	return false;
}

const ArchiveSearchIndex::Item* find_path(const QVector<ArchiveSearchIndex::Item>& items, const QString& path) {
	for (const ArchiveSearchIndex::Item& item : items) {
		if (item.path == path) {
			return &item;
		}
	}
	return nullptr;
}

bool run_test(QString* error) {
	ArchiveSearchIndex::BuildInput input;
	input.scope_label = "fixture.pk3";
	input.archive_entries = {
		entry("maps/e1m1.bsp", 100),
		entry("maps/game/test.proc", 120),
		entry("materials/test.mtr", 80),
		entry("models/player/lower.md3", 200),
		entry("fonts/hud.fontdat", 40),
		entry("textures/wall/stone.tga", 300),
		entry("old.txt", 10),
		entry("hidden/skip.txt", 20),
	};
	input.overlay_files.push_back(ArchiveSearchIndex::OverlayFile{"textures/wall/stone.tga", "C:/tmp/stone-new.tga", 301, 42});
	input.overlay_files.push_back(ArchiveSearchIndex::OverlayFile{"scripts/new.shader", "C:/tmp/new.shader", 50, 43});
	input.virtual_dirs.insert("empty/");
	input.deleted_files.insert("old.txt");
	input.deleted_dir_prefixes.insert("hidden/");

	ArchiveSearchIndex index;
	index.rebuild(input);

	if (has_path(index.items(), "old.txt") || has_path(index.items(), "hidden/skip.txt")) {
		if (error) {
			*error = "Deleted entries remained in the search index.";
		}
		return false;
	}
	if (!has_path(index.items(), "maps/") || !has_path(index.items(), "textures/") || !has_path(index.items(), "empty/")) {
		if (error) {
			*error = "Expected directory entries were not indexed.";
		}
		return false;
	}

	const QVector<ArchiveSearchIndex::Item> stone_results = index.search("stone");
	const ArchiveSearchIndex::Item* stone = find_path(stone_results, "textures/wall/stone.tga");
	if (!stone || !stone->is_added || !stone->is_overridden || stone->source_path.isEmpty()) {
		if (error) {
			*error = "Overlay replacement did not search as an added/overridden item.";
		}
		return false;
	}

	const QVector<ArchiveSearchIndex::Item> dependency_results = index.search("lower.skin");
	const ArchiveSearchIndex::Item* lower = find_path(dependency_results, "models/player/lower.md3");
	if (!lower) {
		if (error) {
			*error = "Model dependency hint did not participate in search.";
		}
		return false;
	}

	const QVector<ArchiveSearchIndex::Item> proc_dependency_results = index.search("test.cm materials");
	const ArchiveSearchIndex::Item* proc = find_path(proc_dependency_results, "maps/game/test.proc");
	if (!proc) {
		if (error) {
			*error = "idTech4 PROC dependency hints did not participate in search.";
		}
		return false;
	}

	const QVector<ArchiveSearchIndex::Item> font_dependency_results = index.search("hud.png");
	const ArchiveSearchIndex::Item* fontdat = find_path(font_dependency_results, "fonts/hud.fontdat");
	if (!fontdat) {
		if (error) {
			*error = "FONTDAT atlas dependency hint did not participate in search.";
		}
		return false;
	}

	const QVector<ArchiveSearchIndex::Item> folder_results = index.search("empty folder");
	const ArchiveSearchIndex::Item* empty = find_path(folder_results, "empty/");
	if (!empty || !empty->is_dir) {
		if (error) {
			*error = "Virtual directory did not search as a folder.";
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
		fail_message(error.isEmpty() ? "Archive search index test failed." : error);
		return 1;
	}
	return 0;
}
