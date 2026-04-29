#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "archive/archive.h"

class ArchiveSession {
public:
	struct MountedArchiveLayer {
		std::unique_ptr<Archive> archive;
		QString mount_name;
		QString mount_fs_path;
		QStringList outer_dir_before_mount;
	};

	[[nodiscard]] Archive& primary_archive() { return primary_archive_; }
	[[nodiscard]] const Archive& primary_archive() const { return primary_archive_; }

	[[nodiscard]] bool has_mounted_archive() const { return !mounted_archives_.empty(); }
	[[nodiscard]] Archive& current_archive();
	[[nodiscard]] const Archive& current_archive() const;

	[[nodiscard]] std::vector<MountedArchiveLayer>& mounted_archives() { return mounted_archives_; }
	[[nodiscard]] const std::vector<MountedArchiveLayer>& mounted_archives() const { return mounted_archives_; }
	[[nodiscard]] const MountedArchiveLayer* current_mounted_archive() const;

	void clear_mounted_archives();
	void push_mounted_archive(MountedArchiveLayer layer);
	void pop_mounted_archive();

private:
	Archive primary_archive_;
	std::vector<MountedArchiveLayer> mounted_archives_;
};
