#include "archive/archive_session.h"

#include <utility>

Archive& ArchiveSession::current_archive() {
	if (has_mounted_archive() && mounted_archives_.back().archive) {
		return *mounted_archives_.back().archive;
	}
	return primary_archive_;
}

const Archive& ArchiveSession::current_archive() const {
	if (has_mounted_archive() && mounted_archives_.back().archive) {
		return *mounted_archives_.back().archive;
	}
	return primary_archive_;
}

const ArchiveSession::MountedArchiveLayer* ArchiveSession::current_mounted_archive() const {
	if (!has_mounted_archive()) {
		return nullptr;
	}
	return &mounted_archives_.back();
}

void ArchiveSession::clear_mounted_archives() {
	mounted_archives_.clear();
}

void ArchiveSession::push_mounted_archive(MountedArchiveLayer layer) {
	mounted_archives_.push_back(std::move(layer));
}

void ArchiveSession::pop_mounted_archive() {
	if (!mounted_archives_.empty()) {
		mounted_archives_.pop_back();
	}
}
