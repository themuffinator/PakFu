#include "formats/idtech4_map.h"

#include <algorithm>

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QVector>

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
	s << "Scope: text/metadata inspection only; 3D rendering is currently limited to Quake-family .bsp maps.\n";
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

	QString summary;
	QTextStream s(&summary);
	s << "Type: idTech4 compiled map render description (.proc)\n";
	s << "Scope: text/metadata inspection only; .proc geometry is not rendered as a 3D map preview.\n";
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
