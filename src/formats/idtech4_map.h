#pragma once

#include <QByteArray>
#include <QString>

enum class IdTech4MapArtifact {
	None = 0,
	SourceMap,
	ProcFile,
};

struct IdTech4MapInspectResult {
	IdTech4MapArtifact artifact = IdTech4MapArtifact::None;
	QString type;
	QString summary;
	QString error;

	[[nodiscard]] bool ok() const { return error.isEmpty() && !summary.isEmpty(); }
};

[[nodiscard]] IdTech4MapArtifact idtech4_map_artifact_for_file(const QString& file_name);
[[nodiscard]] bool is_idtech4_map_text_file(const QString& file_name);
[[nodiscard]] IdTech4MapInspectResult inspect_idtech4_map_bytes(const QByteArray& bytes, const QString& file_name);
