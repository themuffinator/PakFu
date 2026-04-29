#include "formats/fontdat_font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QFileInfo>
#include <QPainter>
#include <QRect>
#include <QtGlobal>

namespace {
constexpr int kGlyphCount = 256;
constexpr int kGlyphRecordBytes = 28;
constexpr int kFooterBytes = 10;
constexpr int kFontDatMinBytes = kGlyphCount * kGlyphRecordBytes + kFooterBytes;
constexpr int kMaxReasonableMetric = 4096;

[[nodiscard]] bool read_u16_le(const QByteArray& bytes, int offset, quint16* out) {
	if (!out || offset < 0 || offset + 2 > bytes.size()) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8);
	return true;
}

[[nodiscard]] bool read_i16_le(const QByteArray& bytes, int offset, qint16* out) {
	quint16 u = 0;
	if (!read_u16_le(bytes, offset, &u) || !out) {
		return false;
	}
	*out = static_cast<qint16>(u);
	return true;
}

[[nodiscard]] bool read_u32_le(const QByteArray& bytes, int offset, quint32* out) {
	if (!out || offset < 0 || offset + 4 > bytes.size()) {
		return false;
	}
	const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = static_cast<quint32>(p[0]) |
	       (static_cast<quint32>(p[1]) << 8) |
	       (static_cast<quint32>(p[2]) << 16) |
	       (static_cast<quint32>(p[3]) << 24);
	return true;
}

[[nodiscard]] bool read_i32_le(const QByteArray& bytes, int offset, qint32* out) {
	quint32 u = 0;
	if (!read_u32_le(bytes, offset, &u) || !out) {
		return false;
	}
	*out = static_cast<qint32>(u);
	return true;
}

[[nodiscard]] bool read_f32_le(const QByteArray& bytes, int offset, float* out) {
	quint32 u = 0;
	if (!read_u32_le(bytes, offset, &u) || !out) {
		return false;
	}
	static_assert(sizeof(float) == sizeof(quint32), "Unexpected float size");
	std::memcpy(out, &u, sizeof(float));
	return true;
}

[[nodiscard]] QString file_ext_lower(const QString& name) {
	const QString lower = QFileInfo(name).fileName().toLower();
	const int dot = lower.lastIndexOf('.');
	return dot >= 0 ? lower.mid(dot + 1) : QString();
}

[[nodiscard]] bool metric_in_range(int value, int min_value = -kMaxReasonableMetric) {
	return value >= min_value && value <= kMaxReasonableMetric;
}

[[nodiscard]] bool uv_in_reasonable_range(float value) {
	return std::isfinite(value) && value >= -0.01f && value <= 1.01f;
}

[[nodiscard]] QRect glyph_source_rect(const FontDatGlyph& glyph, const QSize& atlas_size) {
	if (!glyph.has_pixels() || atlas_size.width() <= 0 || atlas_size.height() <= 0) {
		return {};
	}
	if (!uv_in_reasonable_range(glyph.s) || !uv_in_reasonable_range(glyph.t) ||
	    !uv_in_reasonable_range(glyph.s2) || !uv_in_reasonable_range(glyph.t2)) {
		return {};
	}

	const double ax = static_cast<double>(atlas_size.width());
	const double ay = static_cast<double>(atlas_size.height());
	int x1 = static_cast<int>(std::lround(static_cast<double>(std::min(glyph.s, glyph.s2)) * ax));
	int y1 = static_cast<int>(std::lround(static_cast<double>(std::min(glyph.t, glyph.t2)) * ay));
	int x2 = static_cast<int>(std::lround(static_cast<double>(std::max(glyph.s, glyph.s2)) * ax));
	int y2 = static_cast<int>(std::lround(static_cast<double>(std::max(glyph.t, glyph.t2)) * ay));

	x1 = std::clamp(x1, 0, atlas_size.width());
	y1 = std::clamp(y1, 0, atlas_size.height());
	x2 = std::clamp(x2, 0, atlas_size.width());
	y2 = std::clamp(y2, 0, atlas_size.height());

	QRect rect(QPoint(x1, y1), QPoint(x2 - 1, y2 - 1));
	if (rect.width() <= 0 || rect.height() <= 0) {
		rect = QRect(x1, y1, glyph.width, glyph.height).intersected(QRect(QPoint(0, 0), atlas_size));
	}
	return rect;
}

[[nodiscard]] QImage tint_atlas_for_preview(const QImage& atlas, const QColor& foreground, bool tint_from_alpha) {
	QImage src = atlas.convertToFormat(QImage::Format_ARGB32);
	if (src.isNull()) {
		return {};
	}
	if (!tint_from_alpha) {
		return src;
	}

	int min_alpha = 255;
	int max_alpha = 0;
	for (int y = 0; y < src.height(); ++y) {
		const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
		for (int x = 0; x < src.width(); ++x) {
			const int a = qAlpha(row[x]);
			min_alpha = std::min(min_alpha, a);
			max_alpha = std::max(max_alpha, a);
		}
	}
	const bool useful_alpha = src.hasAlphaChannel() && min_alpha < max_alpha;

	QImage out(src.size(), QImage::Format_ARGB32);
	out.fill(Qt::transparent);
	for (int y = 0; y < src.height(); ++y) {
		const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
		QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
		for (int x = 0; x < src.width(); ++x) {
			const QRgb px = in[x];
			const int alpha = useful_alpha
			                    ? qAlpha(px)
			                    : std::max({qRed(px), qGreen(px), qBlue(px)});
			dst[x] = qRgba(foreground.red(), foreground.green(), foreground.blue(), std::clamp(alpha, 0, 255));
		}
	}
	return out;
}

[[nodiscard]] int glyph_advance_for(const FontDatGlyph& glyph) {
	if (glyph.horiz_advance > 0) {
		return glyph.horiz_advance;
	}
	if (glyph.width > 0) {
		return glyph.width + 1;
	}
	return 4;
}

void draw_glyph(QPainter* painter,
                const QImage& atlas,
                const FontDatGlyph& glyph,
                int x,
                int y,
                int scale,
                int line_height) {
	if (!painter || atlas.isNull() || scale <= 0) {
		return;
	}
	const QRect src = glyph_source_rect(glyph, atlas.size());
	if (src.isEmpty()) {
		return;
	}

	const int draw_w = std::max(1, src.width() * scale);
	const int draw_h = std::max(1, src.height() * scale);
	const int baseline = std::max(0, glyph.baseline);
	const int top_from_baseline = (baseline > 0) ? (baseline - glyph.height) : 0;
	int top = y + std::max(0, (line_height - draw_h) / 2);
	if (baseline > 0 && line_height > 0) {
		top = y + std::max(0, ((line_height / 2) - top_from_baseline * scale - draw_h / 2));
	}
	const QRect dest(x + glyph.horiz_offset * scale, top, draw_w, draw_h);
	painter->drawImage(dest, atlas, src);
}

[[nodiscard]] int measure_text_line(const FontDatDocument& font, const QByteArray& latin1, int scale) {
	int width = 0;
	for (const char c : latin1) {
		const int idx = static_cast<unsigned char>(c);
		if (idx >= 0 && idx < font.glyphs.size()) {
			width += glyph_advance_for(font.glyphs[idx]) * scale;
		}
	}
	return width;
}

void draw_text_line(QPainter* painter,
                    const FontDatDocument& font,
                    const QImage& atlas,
                    const QByteArray& latin1,
                    int y,
                    int image_width,
                    int scale,
                    int line_height) {
	if (!painter || latin1.isEmpty()) {
		return;
	}
	const int measured = measure_text_line(font, latin1, scale);
	int x = std::max(24, (image_width - measured) / 2);
	for (const char c : latin1) {
		const int idx = static_cast<unsigned char>(c);
		if (idx >= 0 && idx < font.glyphs.size()) {
			const FontDatGlyph& glyph = font.glyphs[idx];
			draw_glyph(painter, atlas, glyph, x, y, scale, line_height);
			x += glyph_advance_for(glyph) * scale;
		}
	}
}

const char* tiny_hex_pattern(QChar ch, int row) {
	const ushort c = ch.toLatin1();
	switch (c) {
		case '0': {
			static constexpr const char* p[] = {"111", "101", "101", "101", "111"};
			return p[row];
		}
		case '1': {
			static constexpr const char* p[] = {"010", "110", "010", "010", "111"};
			return p[row];
		}
		case '2': {
			static constexpr const char* p[] = {"111", "001", "111", "100", "111"};
			return p[row];
		}
		case '3': {
			static constexpr const char* p[] = {"111", "001", "111", "001", "111"};
			return p[row];
		}
		case '4': {
			static constexpr const char* p[] = {"101", "101", "111", "001", "001"};
			return p[row];
		}
		case '5': {
			static constexpr const char* p[] = {"111", "100", "111", "001", "111"};
			return p[row];
		}
		case '6': {
			static constexpr const char* p[] = {"111", "100", "111", "101", "111"};
			return p[row];
		}
		case '7': {
			static constexpr const char* p[] = {"111", "001", "010", "010", "010"};
			return p[row];
		}
		case '8': {
			static constexpr const char* p[] = {"111", "101", "111", "101", "111"};
			return p[row];
		}
		case '9': {
			static constexpr const char* p[] = {"111", "101", "111", "001", "111"};
			return p[row];
		}
		case 'A': {
			static constexpr const char* p[] = {"010", "101", "111", "101", "101"};
			return p[row];
		}
		case 'B': {
			static constexpr const char* p[] = {"110", "101", "110", "101", "110"};
			return p[row];
		}
		case 'C': {
			static constexpr const char* p[] = {"111", "100", "100", "100", "111"};
			return p[row];
		}
		case 'D': {
			static constexpr const char* p[] = {"110", "101", "101", "101", "110"};
			return p[row];
		}
		case 'E': {
			static constexpr const char* p[] = {"111", "100", "110", "100", "111"};
			return p[row];
		}
		case 'F': {
			static constexpr const char* p[] = {"111", "100", "110", "100", "100"};
			return p[row];
		}
		default: {
			static constexpr const char* p[] = {"000", "000", "000", "000", "000"};
			return p[row];
		}
	}
}

void draw_hex_label(QPainter* painter, int value, int x, int y, const QColor& color, int scale) {
	if (!painter) {
		return;
	}
	scale = std::clamp(scale, 1, 4);
	const QString text = QString("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
	painter->setPen(Qt::NoPen);
	painter->setBrush(color);
	int cursor = x;
	for (const QChar ch : text) {
		for (int row = 0; row < 5; ++row) {
			const char* bits = tiny_hex_pattern(ch, row);
			for (int col = 0; col < 3; ++col) {
				if (bits[col] == '1') {
					painter->drawRect(QRect(cursor + col * scale, y + row * scale, scale, scale));
				}
			}
		}
		cursor += 4 * scale;
	}
}
}  // namespace

int FontDatDocument::active_glyph_count() const {
	int count = 0;
	for (const FontDatGlyph& glyph : glyphs) {
		if (glyph.has_pixels() || glyph.horiz_advance > 0) {
			++count;
		}
	}
	return count;
}

int FontDatDocument::max_glyph_width() const {
	int out = 0;
	for (const FontDatGlyph& glyph : glyphs) {
		out = std::max(out, glyph.width);
	}
	return out;
}

int FontDatDocument::max_glyph_height() const {
	int out = 0;
	for (const FontDatGlyph& glyph : glyphs) {
		out = std::max(out, glyph.height);
	}
	return out;
}

int FontDatDocument::max_advance() const {
	int out = 0;
	for (const FontDatGlyph& glyph : glyphs) {
		out = std::max(out, glyph_advance_for(glyph));
	}
	return out;
}

bool is_fontdat_file_name(const QString& file_name) {
	return file_ext_lower(file_name) == "fontdat";
}

QStringList fontdat_atlas_candidates_for_path(const QString& fontdat_path) {
	QString normalized = fontdat_path;
	normalized.replace('\\', '/');
	const int slash = normalized.lastIndexOf('/');
	const QString dir = slash >= 0 ? normalized.left(slash + 1) : QString();
	const QString leaf = slash >= 0 ? normalized.mid(slash + 1) : normalized;
	const QFileInfo info(leaf);
	const QString base = info.completeBaseName().isEmpty() ? leaf : info.completeBaseName();

	QStringList out;
	for (const QString& ext : {QStringLiteral("png"), QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("dds")}) {
		out.push_back(dir + base + "." + ext);
	}
	out.removeDuplicates();
	return out;
}

FontDatDecodeResult decode_fontdat_bytes(const QByteArray& bytes) {
	FontDatDecodeResult out;
	if (bytes.size() < kFontDatMinBytes) {
		out.error = QString("FONTDAT file is too small (%1 bytes, expected at least %2).").arg(bytes.size()).arg(kFontDatMinBytes);
		return out;
	}

	// The disk layout mirrors Raven/OpenJK dfontdat_t/glyphInfo_t: 256 glyph
	// metric records followed by point-size metrics. Source reference:
	// https://github.com/JACoders/OpenJK/blob/407a56fbca21310ff158fabbb3207410e381bfc8/code/qcommon/qfiles.h
	out.font.glyphs.reserve(kGlyphCount);
	int invalid_metrics = 0;
	int invalid_uvs = 0;
	for (int i = 0; i < kGlyphCount; ++i) {
		const int off = i * kGlyphRecordBytes;
		qint16 width = 0;
		qint16 height = 0;
		qint16 advance = 0;
		qint16 offset = 0;
		qint32 baseline = 0;
		float s = 0.0f;
		float t = 0.0f;
		float s2 = 0.0f;
		float t2 = 0.0f;
		if (!read_i16_le(bytes, off + 0, &width) ||
		    !read_i16_le(bytes, off + 2, &height) ||
		    !read_i16_le(bytes, off + 4, &advance) ||
		    !read_i16_le(bytes, off + 6, &offset) ||
		    !read_i32_le(bytes, off + 8, &baseline) ||
		    !read_f32_le(bytes, off + 12, &s) ||
		    !read_f32_le(bytes, off + 16, &t) ||
		    !read_f32_le(bytes, off + 20, &s2) ||
		    !read_f32_le(bytes, off + 24, &t2)) {
			out.error = QString("Unable to parse FONTDAT glyph %1.").arg(i);
			return out;
		}

		FontDatGlyph glyph;
		glyph.code = i;
		glyph.width = width;
		glyph.height = height;
		glyph.horiz_advance = advance;
		glyph.horiz_offset = offset;
		glyph.baseline = baseline;
		glyph.s = s;
		glyph.t = t;
		glyph.s2 = s2;
		glyph.t2 = t2;
		out.font.glyphs.push_back(glyph);

		if (!metric_in_range(glyph.width, 0) || !metric_in_range(glyph.height, 0) ||
		    !metric_in_range(glyph.horiz_advance) || !metric_in_range(glyph.horiz_offset) ||
		    !metric_in_range(glyph.baseline)) {
			++invalid_metrics;
		}
		if (!uv_in_reasonable_range(glyph.s) || !uv_in_reasonable_range(glyph.t) ||
		    !uv_in_reasonable_range(glyph.s2) || !uv_in_reasonable_range(glyph.t2)) {
			++invalid_uvs;
		}
	}

	const int footer = kGlyphCount * kGlyphRecordBytes;
	qint16 point_size = 0;
	qint16 height = 0;
	qint16 ascender = 0;
	qint16 descender = 0;
	qint16 korean_hack = 0;
	if (!read_i16_le(bytes, footer + 0, &point_size) ||
	    !read_i16_le(bytes, footer + 2, &height) ||
	    !read_i16_le(bytes, footer + 4, &ascender) ||
	    !read_i16_le(bytes, footer + 6, &descender) ||
	    !read_i16_le(bytes, footer + 8, &korean_hack)) {
		out.error = "Unable to parse FONTDAT footer.";
		return out;
	}
	out.font.point_size = point_size;
	out.font.height = height;
	out.font.ascender = ascender;
	out.font.descender = descender;
	out.font.korean_hack = korean_hack;
	out.font.trailing_bytes = static_cast<int>(std::max<qsizetype>(0, bytes.size() - kFontDatMinBytes));

	if (invalid_metrics > 0) {
		out.error = QString("FONTDAT contains %1 glyphs with out-of-range metrics.").arg(invalid_metrics);
		return out;
	}
	if (invalid_uvs > 0) {
		out.error = QString("FONTDAT contains %1 glyphs with invalid atlas coordinates.").arg(invalid_uvs);
		return out;
	}
	if (out.font.active_glyph_count() <= 0) {
		out.error = "FONTDAT contains no active glyphs.";
		return out;
	}
	return out;
}

FontDatRenderResult render_fontdat_preview(const FontDatDocument& font,
                                           const QImage& atlas,
                                           const FontDatRenderOptions& options) {
	FontDatRenderResult out;
	if (font.glyphs.size() != kGlyphCount) {
		out.error = "FONTDAT preview requires 256 parsed glyphs.";
		return out;
	}
	if (atlas.isNull()) {
		out.error = "FONTDAT preview requires a companion atlas image.";
		return out;
	}

	const int scale = std::clamp(options.glyph_scale, 1, 8);
	const int max_w = std::max({font.max_glyph_width(), font.max_advance(), 8});
	const int max_h = std::max({font.max_glyph_height(), font.height, 8});
	const int cell_w = std::clamp(max_w * scale + 18, 34, 180);
	const int cell_h = std::clamp(max_h * scale + 24, 38, 200);
	const int margin = 18;
	const int sample_line_h = std::clamp(max_h * scale + 10, 28, 140);
	const int sample_area_h = sample_line_h * 4 + 18;
	const int grid_top = margin + sample_area_h;
	const int image_w = margin * 2 + cell_w * 16;
	const int image_h = grid_top + cell_h * 16 + margin;

	QImage image(image_w, image_h, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) {
		out.error = "Unable to allocate FONTDAT preview image.";
		return out;
	}
	image.fill(options.background);

	const QImage glyph_atlas = tint_atlas_for_preview(atlas, options.foreground, options.tint_from_alpha);
	if (glyph_atlas.isNull()) {
		out.error = "Unable to prepare FONTDAT atlas image.";
		return out;
	}

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

	const QByteArray sample_upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const QByteArray sample_lower = "abcdefghijklmnopqrstuvwxyz";
	const QByteArray sample_digits = "0123456789  !?.,:;+-*/";
	const QByteArray sample_pangram = "The quick brown fox jumps over the lazy dog";
	int sample_y = margin + 4;
	draw_text_line(&painter, font, glyph_atlas, sample_pangram, sample_y, image_w, scale, sample_line_h);
	sample_y += sample_line_h;
	draw_text_line(&painter, font, glyph_atlas, sample_upper, sample_y, image_w, scale, sample_line_h);
	sample_y += sample_line_h;
	draw_text_line(&painter, font, glyph_atlas, sample_lower, sample_y, image_w, scale, sample_line_h);
	sample_y += sample_line_h;
	draw_text_line(&painter, font, glyph_atlas, sample_digits, sample_y, image_w, scale, sample_line_h);

	for (int row = 0; row < 16; ++row) {
		for (int col = 0; col < 16; ++col) {
			const int code = row * 16 + col;
			const int x = margin + col * cell_w;
			const int y = grid_top + row * cell_h;
			const QRect cell(x, y, cell_w, cell_h);
			painter.fillRect(cell, ((row + col) % 2 == 0) ? options.cell_background : options.cell_alternate_background);
			painter.setPen(QPen(options.grid, 1));
			painter.drawRect(cell.adjusted(0, 0, -1, -1));
			draw_hex_label(&painter, code, x + 5, y + 5, options.label, 2);

			const FontDatGlyph& glyph = font.glyphs[code];
			const QRect src = glyph_source_rect(glyph, glyph_atlas.size());
			if (src.isEmpty()) {
				continue;
			}
			const int draw_w = src.width() * scale;
			const int draw_h = src.height() * scale;
			const int gx = x + std::max(0, (cell_w - draw_w) / 2);
			const int gy = y + 17 + std::max(0, (cell_h - 20 - draw_h) / 2);
			painter.drawImage(QRect(gx, gy, draw_w, draw_h), glyph_atlas, src);
		}
	}
	painter.end();

	out.image = std::move(image);
	return out;
}

QString fontdat_summary_text(const FontDatDocument& font, const QString& atlas_name, const QSize& atlas_size) {
	QStringList lines;
	lines << "idTech FONTDAT bitmap font";
	lines << QString("Glyph records: %1").arg(font.glyphs.size());
	lines << QString("Active glyphs: %1").arg(font.active_glyph_count());
	lines << QString("Point size: %1").arg(font.point_size);
	lines << QString("Font height: %1").arg(font.height);
	lines << QString("Ascender: %1").arg(font.ascender);
	lines << QString("Descender: %1").arg(font.descender);
	if (!atlas_name.isEmpty()) {
		lines << QString("Atlas: %1").arg(atlas_name);
	}
	if (atlas_size.isValid() && !atlas_size.isEmpty()) {
		lines << QString("Atlas dimensions: %1x%2").arg(atlas_size.width()).arg(atlas_size.height());
	}
	if (font.trailing_bytes > 0) {
		lines << QString("Trailing bytes: %1").arg(font.trailing_bytes);
	}
	return lines.join('\n');
}
