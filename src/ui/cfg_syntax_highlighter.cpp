#include "cfg_syntax_highlighter.h"

#include <array>

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVector>
#include <QStringView>

namespace {
enum class LexerState : int {
	Normal = 0,
	InBlockComment = 1,
	InString = 2,
};

enum class TokenKind : int {
	Whitespace,
	CommentLine,
	CommentBlock,
	String,
	Number,
	VariableRef,
	Identifier,
	Path,
	Operator,
	Punctuation,
	Invalid,
};

enum class ThemeKey : int {
	EditorForeground,
	EditorBackground,
	Comment,
	String,
	Number,
	Identifier,
	Operator,
	Punctuation,
	Invalid,

	Command,
	CvarName,
	CvarValue,
	AliasName,
	AliasBody,
	BindKey,
	BindCommand,
	VariableRef,
	Path,
	ButtonCommand,
	ColorCode,
	Count,
};

struct Token {
	TokenKind kind = TokenKind::Invalid;
	int start = 0;
	int length = 0;
	ThemeKey style = ThemeKey::Invalid;
};

struct UnderlineSpan {
	int start = 0;
	int length = 0;
	ThemeKey base_style = ThemeKey::Invalid;
};

struct ColorCodeSpan {
	int start = 0;
	int length = 0;
	QColor color;
};

struct LexResult {
	QVector<Token> tokens;
	QVector<UnderlineSpan> underline_spans;
	QVector<ColorCodeSpan> color_code_spans;
	LexerState end_state = LexerState::Normal;
};

constexpr bool is_hex_digit(QChar c) {
	const ushort u = c.unicode();
	return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

int hex_value(QChar c) {
	const ushort u = c.unicode();
	if (u >= '0' && u <= '9') {
		return static_cast<int>(u - '0');
	}
	if (u >= 'a' && u <= 'f') {
		return static_cast<int>(10 + (u - 'a'));
	}
	if (u >= 'A' && u <= 'F') {
		return static_cast<int>(10 + (u - 'A'));
	}
	return 0;
}

QColor blend(const QColor& a, const QColor& b, double t) {
	const double inv = 1.0 - t;
	return QColor(
		static_cast<int>(a.red() * inv + b.red() * t),
		static_cast<int>(a.green() * inv + b.green() * t),
		static_cast<int>(a.blue() * inv + b.blue() * t));
}

bool is_dark_background(const QPalette& pal) {
	const QColor base = pal.color(QPalette::Base);
	return base.lightness() < 128;
}

QColor accent_from(const QColor& seed, int hue_offset, bool dark) {
	int h = seed.hslHue();
	if (h < 0) {
		h = 210;
	}
	h = (h + hue_offset) % 360;
	const int s = dark ? 170 : 200;
	const int l = dark ? 175 : 85;
	QColor c;
	c.setHsl(h, s, l);
	return c;
}

QTextCharFormat make_format(const QColor& c, bool bold = false, bool italic = false) {
	QTextCharFormat f;
	f.setForeground(c);
	if (bold) {
		f.setFontWeight(QFont::DemiBold);
	}
	if (italic) {
		f.setFontItalic(true);
	}
	return f;
}

QTextCharFormat make_underlined(const QTextCharFormat& base, const QColor& underline_color) {
	QTextCharFormat f = base;
	f.setUnderlineStyle(QTextCharFormat::SingleUnderline);
	f.setUnderlineColor(underline_color);
	return f;
}

bool token_has_path_shape(QStringView v) {
	if (v.contains(u'/') || v.contains(u'\\')) {
		return true;
	}
	const int dot = v.lastIndexOf(u'.');
	if (dot <= 0 || dot + 1 >= v.size()) {
		return false;
	}
	const QStringView ext = v.mid(dot + 1);
	if (ext == u"cfg" || ext == u"txt" || ext == u"log" || ext == u"ini" || ext == u"json" ||
		ext == u"pk3" || ext == u"pak") {
		return true;
	}
	return false;
}

int parse_number_len(QStringView v) {
	const int n = v.size();
	if (n <= 0) {
		return 0;
	}

	int i = 0;
	if (v[i] == u'+' || v[i] == u'-') {
		++i;
	}

	int digits_before = 0;
	while (i < n && v[i].isDigit()) {
		++digits_before;
		++i;
	}

	int digits_after = 0;
	bool has_dot = false;
	if (i < n && v[i] == u'.') {
		has_dot = true;
		++i;
		while (i < n && v[i].isDigit()) {
			++digits_after;
			++i;
		}
	}

	if (digits_before == 0 && !has_dot) {
		return 0;
	}
	if (digits_before == 0 && has_dot && digits_after == 0) {
		return 0;
	}

	if (i < n && (v[i] == u'e' || v[i] == u'E')) {
		int j = i + 1;
		if (j < n && (v[j] == u'+' || v[j] == u'-')) {
			++j;
		}
		int exp_digits = 0;
		while (j < n && v[j].isDigit()) {
			++exp_digits;
			++j;
		}
		if (exp_digits > 0) {
			i = j;
		}
	}

	return i;
}

QColor quake_color_for_digit(QChar digit) {
	switch (digit.unicode()) {
		case '0':
			return QColor(0, 0, 0);
		case '1':
			return QColor(255, 0, 0);
		case '2':
			return QColor(0, 255, 0);
		case '3':
			return QColor(255, 255, 0);
		case '4':
			return QColor(0, 120, 255);
		case '5':
			return QColor(0, 255, 255);
		case '6':
			return QColor(255, 0, 255);
		case '7':
			return QColor(255, 255, 255);
		case '8':
			return QColor(255, 160, 0);
		case '9':
			return QColor(170, 170, 170);
	}
	return QColor();
}

QColor ensure_contrast(const QColor& c, const QPalette& pal, const QColor& fallback) {
	if (!c.isValid()) {
		return fallback;
	}
	const QColor bg = pal.color(QPalette::Base);
	if (qAbs(c.lightness() - bg.lightness()) < 70) {
		return fallback;
	}
	return c;
}

QStringView token_view(const QString& text, const Token& t) {
	if (t.start < 0 || t.length <= 0 || t.start + t.length > text.size()) {
		return {};
	}
	return QStringView(text).mid(t.start, t.length);
}

bool is_only_whitespace_before(const QString& text, int pos) {
	for (int i = 0; i < pos && i < text.size(); ++i) {
		if (!text[i].isSpace()) {
			return false;
		}
	}
	return true;
}

struct ThemeFormats {
	std::array<QTextCharFormat, static_cast<int>(ThemeKey::Count)> formats;
};

ThemeFormats* theme_formats() {
	static ThemeFormats tf;
	return &tf;
}
}  // namespace

CfgSyntaxHighlighter::CfgSyntaxHighlighter(QTextDocument* parent)
	: QSyntaxHighlighter(parent) {
	refresh_theme();

	if (auto* app = qobject_cast<QGuiApplication*>(QGuiApplication::instance())) {
		connect(app, &QGuiApplication::paletteChanged, this, [this](const QPalette&) {
			refresh_theme();
			rehighlight();
		});
	}
}

void CfgSyntaxHighlighter::refresh_theme() {
	ThemeFormats* tf = theme_formats();
	const QPalette pal = QApplication::palette();
	const bool dark = is_dark_background(pal);

	const QColor fg = pal.color(QPalette::Text);
	const QColor bg = pal.color(QPalette::Base);
	const QColor seed = pal.color(QPalette::Highlight).isValid()
						  ? pal.color(QPalette::Highlight)
						  : pal.color(QPalette::Link);

	const QColor comment = blend(bg, fg, dark ? 0.62 : 0.52);
	const QColor punctuation = blend(bg, fg, dark ? 0.78 : 0.68);
	const QColor op = blend(bg, fg, dark ? 0.86 : 0.76);

	const QColor command = accent_from(seed, 0, dark);
	const QColor cvar_name = accent_from(seed, 55, dark);
	const QColor cvar_value = accent_from(seed, 20, dark);
	const QColor alias_name = accent_from(seed, 295, dark);
	const QColor alias_body = accent_from(seed, 330, dark);
	const QColor bind_key = accent_from(seed, 215, dark);
	const QColor bind_command = accent_from(seed, 350, dark);
	const QColor var_ref = accent_from(seed, 265, dark);
	const QColor path = accent_from(seed, 155, dark);
	const QColor button_cmd = accent_from(seed, 10, dark);
	const QColor number = accent_from(seed, 25, dark);
	const QColor str = accent_from(seed, 120, dark);
	const QColor color_code = accent_from(seed, 175, dark);

	tf->formats[static_cast<int>(ThemeKey::EditorForeground)] = make_format(fg);
	tf->formats[static_cast<int>(ThemeKey::EditorBackground)] = make_format(bg);
	tf->formats[static_cast<int>(ThemeKey::Comment)] = make_format(comment, false, true);
	tf->formats[static_cast<int>(ThemeKey::String)] = make_format(str);
	tf->formats[static_cast<int>(ThemeKey::Number)] = make_format(number);
	tf->formats[static_cast<int>(ThemeKey::Identifier)] = make_format(fg);
	tf->formats[static_cast<int>(ThemeKey::Operator)] = make_format(op);
	tf->formats[static_cast<int>(ThemeKey::Punctuation)] = make_format(punctuation);

	QTextCharFormat invalid;
	invalid.setUnderlineStyle(QTextCharFormat::SingleUnderline);
	invalid.setUnderlineColor(fg);
	tf->formats[static_cast<int>(ThemeKey::Invalid)] = invalid;

	tf->formats[static_cast<int>(ThemeKey::Command)] = make_format(command, true, false);
	tf->formats[static_cast<int>(ThemeKey::CvarName)] = make_format(cvar_name, true, false);
	tf->formats[static_cast<int>(ThemeKey::CvarValue)] = make_format(cvar_value);
	tf->formats[static_cast<int>(ThemeKey::AliasName)] = make_format(alias_name, true, false);
	tf->formats[static_cast<int>(ThemeKey::AliasBody)] = make_format(alias_body);
	tf->formats[static_cast<int>(ThemeKey::BindKey)] = make_format(bind_key, true, false);
	tf->formats[static_cast<int>(ThemeKey::BindCommand)] = make_format(bind_command);
	tf->formats[static_cast<int>(ThemeKey::VariableRef)] = make_format(var_ref, true, false);
	tf->formats[static_cast<int>(ThemeKey::Path)] = make_format(path);
	tf->formats[static_cast<int>(ThemeKey::ButtonCommand)] = make_format(button_cmd, true, false);
	tf->formats[static_cast<int>(ThemeKey::ColorCode)] = make_format(color_code, true, false);
}

LexResult lex_line(const QString& line, LexerState state) {
	LexResult out;
	const int n = line.size();
	int i = 0;

	auto push_token = [&](TokenKind kind, int start, int len) {
		if (len <= 0) {
			return;
		}
		Token t;
		t.kind = kind;
		t.start = start;
		t.length = len;
		switch (kind) {
			case TokenKind::Whitespace:
				t.style = ThemeKey::Identifier;
				break;
			case TokenKind::CommentLine:
			case TokenKind::CommentBlock:
				t.style = ThemeKey::Comment;
				break;
			case TokenKind::String:
				t.style = ThemeKey::String;
				break;
			case TokenKind::Number:
				t.style = ThemeKey::Number;
				break;
			case TokenKind::VariableRef:
				t.style = ThemeKey::VariableRef;
				break;
			case TokenKind::Path:
				t.style = ThemeKey::Path;
				break;
			case TokenKind::Identifier:
				t.style = ThemeKey::Identifier;
				break;
			case TokenKind::Operator:
				t.style = ThemeKey::Operator;
				break;
			case TokenKind::Punctuation:
				t.style = ThemeKey::Punctuation;
				break;
			case TokenKind::Invalid:
				t.style = ThemeKey::Invalid;
				break;
		}
		out.tokens.push_back(t);
	};

	auto mark_underlined = [&](int start, int len, ThemeKey base_style) {
		if (len <= 0) {
			return;
		}
		UnderlineSpan s;
		s.start = start;
		s.length = len;
		s.base_style = base_style;
		out.underline_spans.push_back(s);
	};

	auto scan_color_codes = [&](int start, int len) {
		if (len <= 0) {
			return;
		}
		const QStringView v(line);
		const int end = qMin(n, start + len);
		for (int j = start; j + 1 < end; ++j) {
			if (v[j] != u'^') {
				continue;
			}
			const QChar next = v[j + 1];
			if (next.isDigit()) {
				ColorCodeSpan span;
				span.start = j;
				span.length = 2;
				span.color = quake_color_for_digit(next);
				out.color_code_spans.push_back(span);
				j += 1;
				continue;
			}
			if (next == u'x' || next == u'X') {
				const int hex_start = j + 2;
				int k = hex_start;
				while (k < end && is_hex_digit(v[k]) && (k - hex_start) < 6) {
					++k;
				}
				const int hex_len = k - hex_start;
				if (hex_len == 3 || hex_len == 6) {
					int r = 0;
					int g = 0;
					int b = 0;
					if (hex_len == 3) {
						r = hex_value(v[hex_start + 0]) * 17;
						g = hex_value(v[hex_start + 1]) * 17;
						b = hex_value(v[hex_start + 2]) * 17;
					} else {
						r = (hex_value(v[hex_start + 0]) << 4) | hex_value(v[hex_start + 1]);
						g = (hex_value(v[hex_start + 2]) << 4) | hex_value(v[hex_start + 3]);
						b = (hex_value(v[hex_start + 4]) << 4) | hex_value(v[hex_start + 5]);
					}

					ColorCodeSpan span;
					span.start = j;
					span.length = 2 + hex_len;
					span.color = QColor(r, g, b);
					out.color_code_spans.push_back(span);
					j += span.length - 1;
					continue;
				}
			}
		}
	};

	auto lex_string = [&](int start_pos, bool has_opening_quote) {
		const int start = start_pos;
		int j = start_pos;
		if (has_opening_quote && j < n && line[j] == u'\"') {
			++j;
		}
		bool terminated = false;
		while (j < n) {
			const QChar c = line[j];
			if (c == u'\"') {
				++j;
				terminated = true;
				break;
			}
			if (c == u'\\') {
				if (j + 1 >= n) {
					mark_underlined(j, 1, ThemeKey::String);
					++j;
					continue;
				}
				const QChar esc = line[j + 1];
				if (!(esc == u'\\' || esc == u'\"' || esc == u'n' || esc == u't' || esc == u'r')) {
					mark_underlined(j, 2, ThemeKey::String);
				}
				j += 2;
				continue;
			}
			if (c.unicode() < 0x20 && c != u'\t') {
				mark_underlined(j, 1, ThemeKey::String);
			}
			++j;
		}

		push_token(TokenKind::String, start, j - start);
		scan_color_codes(start, j - start);
		if (!terminated) {
			mark_underlined(start, j - start, ThemeKey::String);
			state = LexerState::InString;
		}
		i = j;
	};

	auto lex_block_comment = [&](int start_pos) {
		const int start = start_pos;
		int j = start_pos;
		bool terminated = false;
		while (j + 1 < n) {
			if (line[j] == u'*' && line[j + 1] == u'/') {
				j += 2;
				terminated = true;
				break;
			}
			++j;
		}
		if (!terminated) {
			j = n;
		}
		push_token(TokenKind::CommentBlock, start, j - start);
		if (!terminated) {
			mark_underlined(start, j - start, ThemeKey::Comment);
			state = LexerState::InBlockComment;
		}
		i = j;
	};

	if (state == LexerState::InBlockComment) {
		lex_block_comment(0);
		if (state == LexerState::InBlockComment) {
			out.end_state = state;
			return out;
		}
	}
	if (state == LexerState::InString) {
		// Continuation: this line starts inside a string opened on a previous line.
		lex_string(0, false);
		if (state == LexerState::InString) {
			out.end_state = state;
			return out;
		}
	}

	while (i < n) {
		const QChar c = line[i];

		// Whitespace.
		if (c.isSpace()) {
			const int start = i;
			while (i < n && line[i].isSpace()) {
				++i;
			}
			push_token(TokenKind::Whitespace, start, i - start);
			continue;
		}

		// Line comments.
		if (c == u'/' && i + 1 < n && line[i + 1] == u'/') {
			push_token(TokenKind::CommentLine, i, n - i);
			break;
		}
		if (c == u'#' && (i == 0 || line[i - 1].isSpace())) {
			push_token(TokenKind::CommentLine, i, n - i);
			break;
		}
		if (c == u';' && is_only_whitespace_before(line, i)) {
			push_token(TokenKind::CommentLine, i, n - i);
			break;
		}

		// Block comment.
		if (c == u'/' && i + 1 < n && line[i + 1] == u'*') {
			lex_block_comment(i);
			continue;
		}

		// String.
		if (c == u'\"') {
			lex_string(i, true);
			continue;
		}

		// Variable reference: $var or ${var}
		if (c == u'$') {
			const int start = i;
			++i;
			if (i < n && line[i] == u'{') {
				++i;
				while (i < n && line[i] != u'}') {
					++i;
				}
				if (i < n && line[i] == u'}') {
					++i;
				} else {
					mark_underlined(start, n - start, ThemeKey::VariableRef);
				}
				push_token(TokenKind::VariableRef, start, i - start);
				continue;
			}
			while (i < n) {
				const QChar ch = line[i];
				if (!(ch.isLetterOrNumber() || ch == u'_' || ch == u'-')) {
					break;
				}
				++i;
			}
			if (i == start + 1) {
				// Bare '$' token.
				mark_underlined(start, 1, ThemeKey::VariableRef);
			}
			push_token(TokenKind::VariableRef, start, i - start);
			continue;
		}

		// Numbers.
		{
			const QStringView rest = QStringView(line).mid(i);
			const int number_len = parse_number_len(rest);
			if (number_len > 0) {
				push_token(TokenKind::Number, i, number_len);
				i += number_len;
				continue;
			}
		}

		// Punctuation.
		if (c == u';' || c == u'(' || c == u')' || c == u'[' || c == u']' || c == u'{' || c == u'}' ||
			c == u',' || c == u':') {
			push_token(TokenKind::Punctuation, i, 1);
			++i;
			continue;
		}

		// Operators.
		if (c == u'+' || c == u'-' || c == u'=' || c == u'*') {
			// +attack/-forward style: treat as an identifier when followed by a name character.
			if ((c == u'+' || c == u'-') && i + 1 < n && (line[i + 1].isLetter() || line[i + 1] == u'_')) {
				// fall through to identifier logic below
			} else {
				push_token(TokenKind::Operator, i, 1);
				++i;
				continue;
			}
		}

		// Identifiers/paths: consume until whitespace or a known delimiter.
		if (c.unicode() < 0x20 && c != u'\t') {
			push_token(TokenKind::Invalid, i, 1);
			mark_underlined(i, 1, ThemeKey::Identifier);
			++i;
			continue;
		}

		const int start = i;
		while (i < n) {
			const QChar ch = line[i];
			if (ch.isSpace() || ch == u'\"' || ch == u';') {
				break;
			}
			// Stop at comment starts (// or /*) but allow plain '/' in paths.
			if (ch == u'/' && i + 1 < n && (line[i + 1] == u'/' || line[i + 1] == u'*')) {
				break;
			}
			// Stop at common punctuation.
			if (ch == u'(' || ch == u')' || ch == u'[' || ch == u']' || ch == u'{' || ch == u'}' ||
				ch == u',' || ch == u':') {
				break;
			}
			++i;
		}
		const QStringView v = QStringView(line).mid(start, i - start);
		if (token_has_path_shape(v)) {
			push_token(TokenKind::Path, start, i - start);
		} else {
			push_token(TokenKind::Identifier, start, i - start);
		}
	}

	out.end_state = state;
	return out;
}

void apply_semantics(const QString& text, QVector<Token>* tokens) {
	if (!tokens) {
		return;
	}

	auto next_non_ws = [&](int idx, int end) -> int {
		int i = idx;
		while (i <= end && i < tokens->size()) {
			const TokenKind kind = (*tokens)[i].kind;
			if (kind == TokenKind::Whitespace || kind == TokenKind::CommentBlock) {
				++i;
				continue;
			}
			break;
		}
		return i;
	};

	int stmt_start = 0;
	for (int i = 0; i <= tokens->size(); ++i) {
		const bool at_end = (i >= tokens->size());
		const bool is_sep =
			(!at_end && (*tokens)[i].kind == TokenKind::Punctuation &&
			 (*tokens)[i].length == 1 && (*tokens)[i].start >= 0 &&
			 (*tokens)[i].start < text.size() && text[(*tokens)[i].start] == u';');
		if (!at_end && !is_sep) {
			continue;
		}

		const int stmt_end = i - 1;
		int t = next_non_ws(stmt_start, stmt_end);
		if (t <= stmt_end && t >= 0 && t < tokens->size()) {
			Token& cmd = (*tokens)[t];
			if (cmd.kind == TokenKind::Identifier || cmd.kind == TokenKind::Path || cmd.kind == TokenKind::VariableRef) {
				const QStringView cmd_view = token_view(text, cmd);
				const QString cmd_lower = cmd_view.toString().toLower();
				const bool is_button = (cmd_view.startsWith(u'+') || cmd_view.startsWith(u'-')) && cmd_view.size() > 1;
				cmd.style = is_button ? ThemeKey::ButtonCommand : ThemeKey::Command;

				const int a1 = next_non_ws(t + 1, stmt_end);
				const int a2 = next_non_ws(a1 + 1, stmt_end);

				const auto style_range = [&](int start_idx, ThemeKey key) {
					if (start_idx < 0) {
						return;
					}
					for (int k = start_idx; k <= stmt_end && k < tokens->size(); ++k) {
						Token& tok = (*tokens)[k];
						if (tok.kind == TokenKind::Whitespace || tok.kind == TokenKind::CommentLine || tok.kind == TokenKind::CommentBlock) {
							continue;
						}
						if (tok.kind == TokenKind::Identifier) {
							tok.style = key;
						}
					}
				};

				if (cmd_lower == "set" || cmd_lower == "seta" || cmd_lower == "setu") {
					if (a1 <= stmt_end && a1 < tokens->size()) {
						Token& name = (*tokens)[a1];
						if (name.kind == TokenKind::Identifier) {
							name.style = ThemeKey::CvarName;
						}
					}
					style_range(a2, ThemeKey::CvarValue);
				} else if (cmd_lower == "bind") {
					if (a1 <= stmt_end && a1 < tokens->size()) {
						Token& key = (*tokens)[a1];
						if (key.kind == TokenKind::Identifier) {
							key.style = ThemeKey::BindKey;
						}
					}
					style_range(a2, ThemeKey::BindCommand);
					for (int k = a2; k <= stmt_end && k < tokens->size(); ++k) {
						Token& tok = (*tokens)[k];
						if (tok.kind != TokenKind::Identifier) {
							continue;
						}
						const QStringView v = token_view(text, tok);
						if ((v.startsWith(u'+') || v.startsWith(u'-')) && v.size() > 1) {
							tok.style = ThemeKey::ButtonCommand;
						}
					}
				} else if (cmd_lower == "alias") {
					if (a1 <= stmt_end && a1 < tokens->size()) {
						Token& name = (*tokens)[a1];
						if (name.kind == TokenKind::Identifier) {
							name.style = ThemeKey::AliasName;
						}
					}
					style_range(a2, ThemeKey::AliasBody);
				} else if (cmd_lower == "vstr") {
					if (a1 <= stmt_end && a1 < tokens->size()) {
						Token& var = (*tokens)[a1];
						if (var.kind == TokenKind::Identifier || var.kind == TokenKind::VariableRef) {
							var.style = ThemeKey::VariableRef;
						}
					}
				} else if (cmd_lower == "exec") {
					if (a1 <= stmt_end && a1 < tokens->size()) {
						Token& p = (*tokens)[a1];
						if (p.kind == TokenKind::Identifier || p.kind == TokenKind::Path) {
							p.style = ThemeKey::Path;
						}
					}
				}
			}
		}

		stmt_start = i + 1;
	}
}

void CfgSyntaxHighlighter::highlightBlock(const QString& text) {
	ThemeFormats* tf = theme_formats();
	const QPalette pal = QApplication::palette();

	LexerState state = LexerState::Normal;
	const int prev = previousBlockState();
	if (prev == static_cast<int>(LexerState::InBlockComment)) {
		state = LexerState::InBlockComment;
	} else if (prev == static_cast<int>(LexerState::InString)) {
		state = LexerState::InString;
	}

	LexResult lex = lex_line(text, state);
	apply_semantics(text, &lex.tokens);

	for (const Token& t : lex.tokens) {
		if (t.kind == TokenKind::Whitespace) {
			continue;
		}
		const QTextCharFormat& f = tf->formats[static_cast<int>(t.style)];
		setFormat(t.start, t.length, f);
	}

	// Inline color codes inside strings.
	const QTextCharFormat& base_color_code = tf->formats[static_cast<int>(ThemeKey::ColorCode)];
	const QColor fallback = base_color_code.foreground().color();
	for (const ColorCodeSpan& span : lex.color_code_spans) {
		QTextCharFormat f = base_color_code;
		f.setForeground(ensure_contrast(span.color, pal, fallback));
		setFormat(span.start, span.length, f);
	}

	// Underlines (subtle invalid markers).
	const QColor underline_color = pal.color(QPalette::Text);
	for (const UnderlineSpan& span : lex.underline_spans) {
		const QTextCharFormat& base = tf->formats[static_cast<int>(span.base_style)];
		setFormat(span.start, span.length, make_underlined(base, underline_color));
	}

	setCurrentBlockState(static_cast<int>(lex.end_state));
}
