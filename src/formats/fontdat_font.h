#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

struct FontDatGlyph {
	int code = 0;
	int width = 0;
	int height = 0;
	int horiz_advance = 0;
	int horiz_offset = 0;
	int baseline = 0;
	float s = 0.0f;
	float t = 0.0f;
	float s2 = 0.0f;
	float t2 = 0.0f;

	[[nodiscard]] bool has_pixels() const { return width > 0 && height > 0; }
};

struct FontDatDocument {
	QVector<FontDatGlyph> glyphs;
	int point_size = 0;
	int height = 0;
	int ascender = 0;
	int descender = 0;
	int korean_hack = 0;
	int trailing_bytes = 0;

	[[nodiscard]] int active_glyph_count() const;
	[[nodiscard]] int max_glyph_width() const;
	[[nodiscard]] int max_glyph_height() const;
	[[nodiscard]] int max_advance() const;
};

struct FontDatDecodeResult {
	FontDatDocument font;
	QString error;

	[[nodiscard]] bool ok() const { return error.isEmpty() && font.glyphs.size() == 256; }
};

struct FontDatRenderOptions {
	QColor foreground = QColor(232, 238, 250);
	QColor background = QColor(23, 27, 34);
	QColor cell_background = QColor(31, 36, 45);
	QColor cell_alternate_background = QColor(36, 42, 52);
	QColor grid = QColor(91, 104, 124);
	QColor label = QColor(161, 174, 195);
	int glyph_scale = 2;
	bool tint_from_alpha = true;
};

struct FontDatRenderResult {
	QImage image;
	QString error;

	[[nodiscard]] bool ok() const { return error.isEmpty() && !image.isNull(); }
};

[[nodiscard]] bool is_fontdat_file_name(const QString& file_name);
[[nodiscard]] QStringList fontdat_atlas_candidates_for_path(const QString& fontdat_path);
[[nodiscard]] FontDatDecodeResult decode_fontdat_bytes(const QByteArray& bytes);
[[nodiscard]] FontDatRenderResult render_fontdat_preview(const FontDatDocument& font,
                                                         const QImage& atlas,
                                                         const FontDatRenderOptions& options = {});
[[nodiscard]] QString fontdat_summary_text(const FontDatDocument& font,
                                           const QString& atlas_name = {},
                                           const QSize& atlas_size = {});
