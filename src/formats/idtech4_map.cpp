#include "formats/idtech4_map.h"

#include <algorithm>
#include <utility>

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

namespace {
QString file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return dot >= 0 ? lower.mid(dot + 1) : QString();
}

bool looks_like_text(const QByteArray& bytes) {
	if (bytes.isEmpty()) {
		return true;
	}
	int printable = 0;
	int control = 0;
	for (const char c : bytes) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (u == 0) {
			return false;
		}
		if (u == '\n' || u == '\r' || u == '\t') {
			++printable;
			continue;
		}
		if (u >= 32 && u < 127) {
			++printable;
			continue;
		}
		if (u < 32) {
			++control;
		} else {
			++printable;
		}
	}
	const int total = bytes.size();
	return total <= 0 || ((printable * 100) / total >= 85 && (control * 100) / total < 5);
}

QString normalize_asset_ref(QString ref) {
	ref = ref.trimmed();
	ref.replace('\\', '/');
	while (ref.startsWith('/')) {
		ref.remove(0, 1);
	}
	return ref;
}

QColor color_for_material(const QString& material) {
	uint h = qHash(material.toLower());
	const int hue = static_cast<int>(h % 360);
	return QColor::fromHsv(hue, 96, 205);
}

struct TextToken {
	enum class Kind {
		End,
		Word,
		String,
		LBrace,
		RBrace,
		LParen,
		RParen,
	};

	Kind kind = Kind::End;
	QString text;
};

class TextTokenizer {
public:
	explicit TextTokenizer(QString text) : text_(std::move(text)) {}

	TextToken next() {
		if (has_peek_) {
			has_peek_ = false;
			return peeked_;
		}
		return read_token();
	}

	TextToken peek() {
		if (!has_peek_) {
			peeked_ = read_token();
			has_peek_ = true;
		}
		return peeked_;
	}

private:
	void skip_space_and_comments() {
		for (;;) {
			while (pos_ < text_.size() && text_.at(pos_).isSpace()) {
				++pos_;
			}
			if (pos_ + 1 >= text_.size()) {
				return;
			}
			const QChar a = text_.at(pos_);
			const QChar b = text_.at(pos_ + 1);
			if (a == '/' && b == '/') {
				pos_ += 2;
				while (pos_ < text_.size() && text_.at(pos_) != '\n') {
					++pos_;
				}
				continue;
			}
			if (a == '/' && b == '*') {
				pos_ += 2;
				while (pos_ + 1 < text_.size()) {
					if (text_.at(pos_) == '*' && text_.at(pos_ + 1) == '/') {
						pos_ += 2;
						break;
					}
					++pos_;
				}
				continue;
			}
			return;
		}
	}

	TextToken read_token() {
		skip_space_and_comments();
		if (pos_ >= text_.size()) {
			return {};
		}
		const QChar ch = text_.at(pos_++);
		if (ch == '{') {
			return {TextToken::Kind::LBrace, "{"};
		}
		if (ch == '}') {
			return {TextToken::Kind::RBrace, "}"};
		}
		if (ch == '(') {
			return {TextToken::Kind::LParen, "("};
		}
		if (ch == ')') {
			return {TextToken::Kind::RParen, ")"};
		}
		if (ch == '"') {
			QString out;
			bool escaped = false;
			while (pos_ < text_.size()) {
				const QChar c = text_.at(pos_++);
				if (escaped) {
					out.append(c);
					escaped = false;
					continue;
				}
				if (c == '\\') {
					escaped = true;
					continue;
				}
				if (c == '"') {
					break;
				}
				out.append(c);
			}
			return {TextToken::Kind::String, out};
		}

		QString out;
		out.append(ch);
		while (pos_ < text_.size()) {
			const QChar c = text_.at(pos_);
			if (c.isSpace() || c == '{' || c == '}' || c == '(' || c == ')' || c == '"') {
				break;
			}
			if (c == '/' && pos_ + 1 < text_.size() && (text_.at(pos_ + 1) == '/' || text_.at(pos_ + 1) == '*')) {
				break;
			}
			out.append(c);
			++pos_;
		}
		return {TextToken::Kind::Word, out};
	}

	QString text_;
	int pos_ = 0;
	bool has_peek_ = false;
	TextToken peeked_;
};

bool token_is_value(const TextToken& token) {
	return token.kind == TextToken::Kind::Word || token.kind == TextToken::Kind::String;
}

bool token_to_int(const TextToken& token, int* out) {
	if (!out || !token_is_value(token)) {
		return false;
	}
	bool ok = false;
	const int v = token.text.toInt(&ok);
	if (ok) {
		*out = v;
	}
	return ok;
}

bool token_to_float(const TextToken& token, float* out) {
	if (!out || !token_is_value(token)) {
		return false;
	}
	bool ok = false;
	const float v = token.text.toFloat(&ok);
	if (ok) {
		*out = v;
	}
	return ok;
}

bool skip_balanced_block(TextTokenizer* tokenizer) {
	if (!tokenizer) {
		return false;
	}
	int depth = 1;
	while (depth > 0) {
		const TextToken token = tokenizer->next();
		if (token.kind == TextToken::Kind::End) {
			return false;
		}
		if (token.kind == TextToken::Kind::LBrace) {
			++depth;
		} else if (token.kind == TextToken::Kind::RBrace) {
			--depth;
		}
	}
	return true;
}

int line_count(const QString& text) {
	if (text.isEmpty()) {
		return 0;
	}
	return text.count('\n') + 1;
}

int count_matches(const QString& text,
                  const QString& pattern,
                  QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption) {
	const QRegularExpression re(pattern, options);
	int count = 0;
	auto it = re.globalMatch(text);
	while (it.hasNext()) {
		it.next();
		++count;
	}
	return count;
}

QString first_capture(const QString& text,
                      const QString& pattern,
                      QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption) {
	const QRegularExpression re(pattern, options);
	const QRegularExpressionMatch match = re.match(text);
	if (!match.hasMatch() || match.lastCapturedIndex() < 1) {
		return {};
	}
	return match.captured(1).trimmed();
}

int count_top_level_blocks(const QString& text) {
	int depth = 0;
	int count = 0;
	bool in_string = false;
	bool escaped = false;
	bool line_comment = false;
	bool block_comment = false;

	for (int i = 0; i < text.size(); ++i) {
		const QChar ch = text.at(i);
		const QChar next = (i + 1 < text.size()) ? text.at(i + 1) : QChar();

		if (line_comment) {
			if (ch == '\n') {
				line_comment = false;
			}
			continue;
		}
		if (block_comment) {
			if (ch == '*' && next == '/') {
				block_comment = false;
				++i;
			}
			continue;
		}
		if (in_string) {
			if (escaped) {
				escaped = false;
			} else if (ch == '\\') {
				escaped = true;
			} else if (ch == '"') {
				in_string = false;
			}
			continue;
		}

		if (ch == '/' && next == '/') {
			line_comment = true;
			++i;
			continue;
		}
		if (ch == '/' && next == '*') {
			block_comment = true;
			++i;
			continue;
		}
		if (ch == '"') {
			in_string = true;
			continue;
		}
		if (ch == '{') {
			if (depth == 0) {
				++count;
			}
			++depth;
			continue;
		}
		if (ch == '}') {
			depth = std::max(0, depth - 1);
		}
	}

	return count;
}

QStringList classname_preview(const QString& text, int* total) {
	if (total) {
		*total = 0;
	}
	QHash<QString, int> counts;
	const QRegularExpression re(QStringLiteral(R"re("classname"\s+"([^"]+)")re"),
	                            QRegularExpression::CaseInsensitiveOption);
	auto it = re.globalMatch(text);
	while (it.hasNext()) {
		const QRegularExpressionMatch match = it.next();
		const QString cls = match.captured(1).trimmed();
		if (cls.isEmpty()) {
			continue;
		}
		counts[cls] += 1;
		if (total) {
			++(*total);
		}
	}

	QVector<QPair<QString, int>> sorted;
	sorted.reserve(counts.size());
	for (auto i = counts.cbegin(); i != counts.cend(); ++i) {
		sorted.push_back({i.key(), i.value()});
	}
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
		if (a.second != b.second) {
			return a.second > b.second;
		}
		return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
	});

	QStringList lines;
	const int preview_count = std::min(static_cast<int>(sorted.size()), 12);
	for (int i = 0; i < preview_count; ++i) {
		lines.push_back(QString("%1: %2").arg(sorted[i].first).arg(sorted[i].second));
	}
	if (sorted.size() > preview_count) {
		lines.push_back(QString("... (%1 more classes)").arg(sorted.size() - preview_count));
	}
	return lines;
}

IdTech4MapInspectResult inspect_source_map(const QByteArray& bytes, const QString& text) {
	const QString version = first_capture(text,
	                                      QStringLiteral(R"(^\s*Version\s+([0-9]+))"),
	                                      QRegularExpression::MultilineOption |
	                                        QRegularExpression::CaseInsensitiveOption);
	const int top_level_blocks = count_top_level_blocks(text);
	const int brush_def3_count = count_matches(text, QStringLiteral(R"(\bbrushDef3\b)"));
	const int brush_def_count = count_matches(text, QStringLiteral(R"(\bbrushDef\b)"));
	const int patch_def_count = count_matches(text, QStringLiteral(R"(\bpatchDef[0-9]*\b)"));
	const int idtech4_entity_defs = count_matches(text,
	                                              QStringLiteral(R"(\bentityDef\b)"),
	                                              QRegularExpression::CaseInsensitiveOption);
	int classname_total = 0;
	const QStringList classes = classname_preview(text, &classname_total);
	const bool idtech4_hints = version == "2" || brush_def3_count > 0 || patch_def_count > 0 || idtech4_entity_defs > 0;

	QString summary;
	QTextStream s(&summary);
	s << "Type: " << (idtech4_hints ? "idTech4 source map text" : "source map text") << "\n";
	s << "Scope: source-map text/metadata inspection; compiled .proc files can render 3D geometry.\n";
	if (!version.isEmpty()) {
		s << "Version: " << version << "\n";
	}
	s << "Detected idTech4 hints: " << (idtech4_hints ? "yes" : "no") << "\n";
	s << "Bytes inspected: " << bytes.size() << "\n";
	s << "Lines: " << line_count(text) << "\n";
	s << "Top-level map blocks: " << top_level_blocks << "\n";
	if (classname_total > 0) {
		s << "Classname entries: " << classname_total << "\n";
	}
	s << "brushDef3 blocks: " << brush_def3_count << "\n";
	s << "brushDef blocks: " << brush_def_count << "\n";
	s << "patchDef blocks: " << patch_def_count << "\n";
	if (!classes.isEmpty()) {
		s << "Class preview:\n";
		for (const QString& line : classes) {
			s << "  " << line << "\n";
		}
	}

	return IdTech4MapInspectResult{IdTech4MapArtifact::SourceMap, idtech4_hints ? "idTech4 Source Map" : "Source Map", summary, {}};
}

QString proc_summary(const IdTech4ProcLoadResult& proc, int bytes, int lines) {
	QString summary;
	QTextStream s(&summary);
	s << "Type: idTech4 compiled map render description (.proc)\n";
	s << "Scope: 3D render-geometry preview with material/texture dependency resolution.\n";
	if (!proc.version.isEmpty()) {
		s << "PROC version: " << proc.version << "\n";
	} else {
		s << "PROC header: not detected\n";
	}
	s << "Bytes inspected: " << bytes << "\n";
	s << "Lines: " << lines << "\n";
	s << "Model sections: " << proc.model_names.size() << "\n";
	s << "Renderable surfaces: " << proc.surface_count << "\n";
	s << "Vertices: " << proc.vertex_count << "\n";
	s << "Indices: " << proc.index_count << "\n";
	s << "Materials: " << proc.material_refs.size() << "\n";
	s << "Inter-area portal sections: " << (proc.inter_area_portal_count >= 0 ? 1 : 0) << "\n";
	if (proc.inter_area_portal_count >= 0) {
		s << "Inter-area portals: " << proc.inter_area_portal_count << "\n";
	}
	s << "Node sections: " << (proc.node_count >= 0 ? 1 : 0) << "\n";
	if (proc.node_count >= 0) {
		s << "Nodes: " << proc.node_count << "\n";
	}
	if (!proc.material_refs.isEmpty()) {
		s << "Material preview:\n";
		const int count = std::min(static_cast<int>(proc.material_refs.size()), 12);
		for (int i = 0; i < count; ++i) {
			s << "  " << proc.material_refs[i] << "\n";
		}
		if (proc.material_refs.size() > count) {
			s << "  ... (" << (proc.material_refs.size() - count) << " more materials)\n";
		}
	}
	return summary;
}

bool parse_proc_surface(TextTokenizer* tokenizer, IdTech4ProcLoadResult* out, QString* error) {
	if (!tokenizer || !out) {
		return false;
	}

	const TextToken material_token = tokenizer->next();
	if (!token_is_value(material_token)) {
		if (error) {
			*error = "Malformed .proc surface: missing material name.";
		}
		return false;
	}
	const QString material = normalize_asset_ref(material_token.text);

	int vertex_count = 0;
	int index_count = 0;
	if (!token_to_int(tokenizer->next(), &vertex_count) ||
	    !token_to_int(tokenizer->next(), &index_count)) {
		if (error) {
			*error = QString("Malformed .proc surface \"%1\": missing vertex/index counts.").arg(material);
		}
		return false;
	}
	if (vertex_count <= 0 || vertex_count > 1'000'000 || index_count <= 0 || index_count > 6'000'000) {
		if (error) {
			*error = QString("Unreasonable .proc surface size for \"%1\".").arg(material);
		}
		return false;
	}

	// idTech4 writes an extra surface flag after the index count.
	(void)tokenizer->next();

	const int base_vertex = out->mesh.vertices.size();
	const QColor material_color = color_for_material(material);
	bool have_bounds = !out->mesh.vertices.isEmpty();

	out->mesh.vertices.reserve(out->mesh.vertices.size() + vertex_count);
	for (int i = 0; i < vertex_count; ++i) {
		if (tokenizer->next().kind != TextToken::Kind::LParen) {
			if (error) {
				*error = QString("Malformed .proc surface \"%1\": missing vertex tuple.").arg(material);
			}
			return false;
		}

		float values[11] = {};
		for (float& v : values) {
			if (!token_to_float(tokenizer->next(), &v)) {
				if (error) {
					*error = QString("Malformed .proc surface \"%1\": invalid vertex value.").arg(material);
				}
				return false;
			}
		}
		if (tokenizer->next().kind != TextToken::Kind::RParen) {
			if (error) {
				*error = QString("Malformed .proc surface \"%1\": unterminated vertex tuple.").arg(material);
			}
			return false;
		}

		BspMeshVertex vertex;
		vertex.pos = QVector3D(values[0], values[1], values[2]);
		vertex.uv = QVector2D(values[3], values[4]);
		vertex.normal = QVector3D(values[5], values[6], values[7]);
		if (vertex.normal.lengthSquared() > 0.0001f) {
			vertex.normal.normalize();
		} else {
			vertex.normal = QVector3D(0.0f, 0.0f, 1.0f);
		}
		vertex.color = material_color;
		out->mesh.vertices.push_back(vertex);

		if (!have_bounds) {
			out->mesh.mins = vertex.pos;
			out->mesh.maxs = vertex.pos;
			have_bounds = true;
		} else {
			out->mesh.mins.setX(std::min(out->mesh.mins.x(), vertex.pos.x()));
			out->mesh.mins.setY(std::min(out->mesh.mins.y(), vertex.pos.y()));
			out->mesh.mins.setZ(std::min(out->mesh.mins.z(), vertex.pos.z()));
			out->mesh.maxs.setX(std::max(out->mesh.maxs.x(), vertex.pos.x()));
			out->mesh.maxs.setY(std::max(out->mesh.maxs.y(), vertex.pos.y()));
			out->mesh.maxs.setZ(std::max(out->mesh.maxs.z(), vertex.pos.z()));
		}
	}

	BspMeshSurface surface;
	surface.first_index = out->mesh.indices.size();
	surface.texture = material.toLower();
	surface.uv_normalized = true;
	surface.lightmap_index = -1;

	out->mesh.indices.reserve(out->mesh.indices.size() + index_count);
	for (int i = 0; i < index_count; ++i) {
		int idx = 0;
		if (!token_to_int(tokenizer->next(), &idx) || idx < 0 || idx >= vertex_count) {
			if (error) {
				*error = QString("Malformed .proc surface \"%1\": invalid triangle index.").arg(material);
			}
			return false;
		}
		out->mesh.indices.push_back(static_cast<std::uint32_t>(base_vertex + idx));
	}
	if (tokenizer->next().kind != TextToken::Kind::RBrace) {
		if (error) {
			*error = QString("Malformed .proc surface \"%1\": missing closing brace.").arg(material);
		}
		return false;
	}

	surface.index_count = out->mesh.indices.size() - surface.first_index;
	out->mesh.surfaces.push_back(surface);
	out->surface_count += 1;
	out->vertex_count += vertex_count;
	out->index_count += surface.index_count;
	if (!material.isEmpty() && !out->material_refs.contains(material, Qt::CaseInsensitive)) {
		out->material_refs.push_back(material);
	}
	return true;
}

bool parse_proc_model(TextTokenizer* tokenizer, IdTech4ProcLoadResult* out, QString* error) {
	if (!tokenizer || !out) {
		return false;
	}
	while (true) {
		const TextToken token = tokenizer->next();
		if (token.kind == TextToken::Kind::End) {
			if (error) {
				*error = "Malformed .proc model: unexpected end of file.";
			}
			return false;
		}
		if (token.kind == TextToken::Kind::RBrace) {
			return true;
		}
		if (token.kind == TextToken::Kind::LBrace) {
			if (!parse_proc_surface(tokenizer, out, error)) {
				return false;
			}
			continue;
		}
		if (!token_is_value(token)) {
			continue;
		}

		const QString model_name = normalize_asset_ref(token.text);
		int expected_surfaces = 0;
		const bool has_surface_count = token_to_int(tokenizer->peek(), &expected_surfaces);
		if (has_surface_count && expected_surfaces >= 0) {
			(void)tokenizer->next();
		}
		if (!model_name.isEmpty() && !out->model_names.contains(model_name, Qt::CaseInsensitive)) {
			out->model_names.push_back(model_name);
		}
	}
}

IdTech4ProcLoadResult parse_proc_text(const QByteArray& bytes, const QString& text) {
	IdTech4ProcLoadResult out;
	out.inter_area_portal_count = -1;
	out.node_count = -1;

	TextTokenizer tokenizer(text);
	while (true) {
		const TextToken token = tokenizer.next();
		if (token.kind == TextToken::Kind::End) {
			break;
		}
		if (!token_is_value(token)) {
			continue;
		}
		const QString word = token.text.toLower();
		if (word == "proc") {
			const TextToken version = tokenizer.next();
			if (token_is_value(version)) {
				out.version = version.text;
			}
			continue;
		}
		if (word == "model") {
			if (tokenizer.next().kind != TextToken::Kind::LBrace) {
				out.error = "Malformed .proc model section.";
				return out;
			}
			QString err;
			if (!parse_proc_model(&tokenizer, &out, &err)) {
				out.error = err;
				return out;
			}
			continue;
		}
		if (word == "interareaportals") {
			if (tokenizer.next().kind != TextToken::Kind::LBrace) {
				out.error = "Malformed .proc interAreaPortals section.";
				return out;
			}
			int count = 0;
			if (token_to_int(tokenizer.peek(), &count)) {
				out.inter_area_portal_count = count;
			}
			if (!skip_balanced_block(&tokenizer)) {
				out.error = "Malformed .proc interAreaPortals section.";
				return out;
			}
			continue;
		}
		if (word == "nodes") {
			if (tokenizer.next().kind != TextToken::Kind::LBrace) {
				out.error = "Malformed .proc nodes section.";
				return out;
			}
			int count = 0;
			if (token_to_int(tokenizer.peek(), &count)) {
				out.node_count = count;
			}
			if (!skip_balanced_block(&tokenizer)) {
				out.error = "Malformed .proc nodes section.";
				return out;
			}
			continue;
		}
	}

	if (out.mesh.vertices.isEmpty() || out.mesh.indices.isEmpty()) {
		out.error = "No renderable .proc geometry was found.";
		return out;
	}
	out.material_refs.sort(Qt::CaseInsensitive);
	out.summary = proc_summary(out, bytes.size(), line_count(text));
	return out;
}

IdTech4MapInspectResult inspect_proc_file(const QByteArray& bytes, const QString& text) {
	const QString version = first_capture(text,
	                                      QStringLiteral(R"re(^\s*PROC\s+"?([^"\s]+)"?)re"),
	                                      QRegularExpression::MultilineOption |
	                                        QRegularExpression::CaseInsensitiveOption);
	const int model_sections = count_matches(text,
	                                         QStringLiteral(R"(\bmodel\s*\{)"),
	                                         QRegularExpression::CaseInsensitiveOption);
	const int shadow_model_sections = count_matches(text,
	                                                QStringLiteral(R"(\bshadowModel\s*\{)"),
	                                                QRegularExpression::CaseInsensitiveOption);
	const int portal_sections = count_matches(text,
	                                          QStringLiteral(R"(\binterAreaPortals\s*\{)"),
	                                          QRegularExpression::CaseInsensitiveOption);
	const int node_sections = count_matches(text,
	                                       QStringLiteral(R"(\bnodes\s*\{)"),
	                                       QRegularExpression::CaseInsensitiveOption);
	const int material_refs = count_matches(text, QStringLiteral(R"("[^"\r\n/][^"\r\n]*")"));

	IdTech4ProcLoadResult proc = parse_proc_text(bytes, text);
	QString summary;
	if (proc.ok()) {
		summary = proc.summary;
	} else {
		QTextStream s(&summary);
		s << "Type: idTech4 compiled map render description (.proc)\n";
		s << "Scope: 3D render-geometry preview when renderable model surfaces are present.\n";
		if (!version.isEmpty()) {
			s << "PROC version: " << version << "\n";
		} else {
			s << "PROC header: not detected\n";
		}
		s << "Bytes inspected: " << bytes.size() << "\n";
		s << "Lines: " << line_count(text) << "\n";
		s << "Model sections: " << model_sections << "\n";
		s << "Shadow model sections: " << shadow_model_sections << "\n";
		s << "Inter-area portal sections: " << portal_sections << "\n";
		s << "Node sections: " << node_sections << "\n";
		if (material_refs > 0) {
			s << "Quoted tokens/material refs: " << material_refs << "\n";
		}
		if (!proc.error.isEmpty()) {
			s << "Geometry preview: " << proc.error << "\n";
		}
	}

	return IdTech4MapInspectResult{IdTech4MapArtifact::ProcFile, "idTech4 PROC Map", summary, {}};
}
}  // namespace

IdTech4MapArtifact idtech4_map_artifact_for_file(const QString& file_name) {
	const QString ext = file_ext_lower(file_name);
	if (ext == "map") {
		return IdTech4MapArtifact::SourceMap;
	}
	if (ext == "proc") {
		return IdTech4MapArtifact::ProcFile;
	}
	return IdTech4MapArtifact::None;
}

bool is_idtech4_map_text_file(const QString& file_name) {
	return idtech4_map_artifact_for_file(file_name) != IdTech4MapArtifact::None;
}

IdTech4MapInspectResult inspect_idtech4_map_bytes(const QByteArray& bytes, const QString& file_name) {
	const IdTech4MapArtifact artifact = idtech4_map_artifact_for_file(file_name);
	if (artifact == IdTech4MapArtifact::None) {
		return IdTech4MapInspectResult{IdTech4MapArtifact::None, {}, {}, "Unsupported idTech4 map artifact extension."};
	}
	if (!looks_like_text(bytes)) {
		const QString type = (artifact == IdTech4MapArtifact::ProcFile) ? "idTech4 PROC Map" : "Source Map";
		return IdTech4MapInspectResult{artifact, type, {}, "Map artifact is not a plain-text payload."};
	}

	const QString text = QString::fromUtf8(bytes);
	if (artifact == IdTech4MapArtifact::SourceMap) {
		return inspect_source_map(bytes, text);
	}
	return inspect_proc_file(bytes, text);
}

IdTech4MaterialParseResult parse_idtech4_material_bytes(const QByteArray& bytes, const QString& file_name) {
	(void)file_name;
	IdTech4MaterialParseResult out;
	if (!looks_like_text(bytes)) {
		out.error = "Material file is not a plain-text payload.";
		return out;
	}

	TextTokenizer tokenizer(QString::fromUtf8(bytes));
	while (true) {
		const TextToken name_token = tokenizer.next();
		if (name_token.kind == TextToken::Kind::End) {
			break;
		}
		if (!token_is_value(name_token)) {
			continue;
		}

		const TextToken maybe_block = tokenizer.peek();
		if (maybe_block.kind != TextToken::Kind::LBrace) {
			continue;
		}
		(void)tokenizer.next();

		IdTech4MaterialDecl decl;
		decl.name = normalize_asset_ref(name_token.text);
		int depth = 1;
		int preferred_rank = 99;
		while (depth > 0) {
			const TextToken token = tokenizer.next();
			if (token.kind == TextToken::Kind::End) {
				out.error = QString("Unterminated material declaration: %1").arg(decl.name);
				return out;
			}
			if (token.kind == TextToken::Kind::LBrace) {
				++depth;
				continue;
			}
			if (token.kind == TextToken::Kind::RBrace) {
				--depth;
				continue;
			}
			if (!token_is_value(token)) {
				continue;
			}

			const QString key = token.text.toLower();
			int rank = -1;
			if (key == "diffusemap") {
				rank = 0;
			} else if (key == "qer_editorimage") {
				rank = 1;
			} else if (key == "map") {
				rank = 2;
			} else if (key == "bumpmap" || key == "specularmap") {
				rank = 3;
			}
			if (rank < 0) {
				continue;
			}

			const TextToken ref_token = tokenizer.next();
			if (!token_is_value(ref_token)) {
				continue;
			}
			const QString ref = normalize_asset_ref(ref_token.text);
			if (ref.isEmpty() || ref.startsWith('_') || ref.contains('(') || ref.contains(')')) {
				continue;
			}
			if (!decl.image_refs.contains(ref, Qt::CaseInsensitive)) {
				decl.image_refs.push_back(ref);
			}
			if (rank < preferred_rank) {
				decl.preferred_image = ref;
				preferred_rank = rank;
			}
		}

		if (!decl.name.isEmpty()) {
			out.materials.push_back(std::move(decl));
		}
	}
	return out;
}

IdTech4ProcLoadResult load_idtech4_proc_mesh_bytes(const QByteArray& bytes, const QString& file_name) {
	IdTech4ProcLoadResult out;
	if (idtech4_map_artifact_for_file(file_name) != IdTech4MapArtifact::ProcFile) {
		out.error = "Unsupported idTech4 .proc extension.";
		return out;
	}
	if (!looks_like_text(bytes)) {
		out.error = "PROC file is not a plain-text payload.";
		return out;
	}
	return parse_proc_text(bytes, QString::fromUtf8(bytes));
}
