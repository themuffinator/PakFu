#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "formats/bsp_preview.h"

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

struct IdTech4MaterialDecl {
	QString name;
	QString preferred_image;
	QStringList image_refs;
};

struct IdTech4MaterialParseResult {
	QVector<IdTech4MaterialDecl> materials;
	QString error;

	[[nodiscard]] bool ok() const { return error.isEmpty(); }
};

struct IdTech4ProcLoadResult {
	BspMesh mesh;
	QString version;
	QStringList model_names;
	QStringList material_refs;
	QString summary;
	QString error;
	int surface_count = 0;
	int vertex_count = 0;
	int index_count = 0;
	int inter_area_portal_count = 0;
	int node_count = 0;

	[[nodiscard]] bool ok() const { return error.isEmpty() && !mesh.vertices.isEmpty() && !mesh.indices.isEmpty(); }
};

[[nodiscard]] IdTech4MapArtifact idtech4_map_artifact_for_file(const QString& file_name);
[[nodiscard]] bool is_idtech4_map_text_file(const QString& file_name);
[[nodiscard]] IdTech4MapInspectResult inspect_idtech4_map_bytes(const QByteArray& bytes, const QString& file_name);
[[nodiscard]] IdTech4MaterialParseResult parse_idtech4_material_bytes(const QByteArray& bytes, const QString& file_name);
[[nodiscard]] IdTech4ProcLoadResult load_idtech4_proc_mesh_bytes(const QByteArray& bytes, const QString& file_name);
