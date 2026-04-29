#pragma once

#include <QString>
#include <QtGlobal>

struct PreviewAssetContext {
	QString palette_provenance;
	QString companion_resolution;
	QString texture_dependencies;
	QString shader_dependencies;
	QString renderer_state;
	QString preview_fallback;
	QString performance_profile;
};

struct PreviewFileInfo {
	QString pak_path;
	qint64 size = -1;
	qint64 mtime_utc_secs = -1;

	[[nodiscard]] bool is_valid() const {
		return !pak_path.isEmpty() || size >= 0 || mtime_utc_secs >= 0;
	}
};

class PreviewSession {
public:
	void set_current_file_info(const QString& pak_path, qint64 size, qint64 mtime_utc_secs);
	void clear_current_file_info();
	[[nodiscard]] const PreviewFileInfo& current_file_info() const { return current_file_info_; }

	void set_asset_context(const PreviewAssetContext& context);
	void clear_asset_context();
	[[nodiscard]] const PreviewAssetContext& asset_context() const { return asset_context_; }

private:
	PreviewFileInfo current_file_info_;
	PreviewAssetContext asset_context_;
};
