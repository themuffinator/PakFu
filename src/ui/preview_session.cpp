#include "ui/preview_session.h"

void PreviewSession::set_current_file_info(const QString& pak_path, qint64 size, qint64 mtime_utc_secs) {
	current_file_info_.pak_path = pak_path;
	current_file_info_.size = size;
	current_file_info_.mtime_utc_secs = mtime_utc_secs;
}

void PreviewSession::clear_current_file_info() {
	current_file_info_ = {};
}

void PreviewSession::set_asset_context(const PreviewAssetContext& context) {
	asset_context_ = context;
}

void PreviewSession::clear_asset_context() {
	asset_context_ = {};
}
