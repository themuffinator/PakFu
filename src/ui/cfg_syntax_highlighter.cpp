#include "cfg_syntax_highlighter.h"

#include <QApplication>
#include <QFont>
#include <QTextDocument>
#include <QVector>

namespace {
/*
=============
make_format

Create a text format with optional styles.
=============
*/
QTextCharFormat make_format(const QColor& color, bool italic = false, bool bold = false, bool underline = false) {
	QTextCharFormat fmt;
	fmt.setForeground(color);
	if (italic) {
		fmt.setFontItalic(true);
	}
	if (bold) {
		fmt.setFontWeight(QFont::Bold);
	}
	if (underline) {
		fmt.setFontUnderline(true);
	}
	return fmt;
}

/*
=============
make_invalid_format

Build an underline format for invalid spans.
=============
*/
QTextCharFormat make_invalid_format(const QColor& color) {
	QTextCharFormat fmt;
	fmt.setForeground(color);
	fmt.setFontUnderline(true);
	fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
	return fmt;
}

/*
=============
is_ident_char

Return whether a character can appear in identifiers.
=============
*/
bool is_ident_char(QChar c) {
	return c.isLetterOrNumber() || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\';
}

/*
=============
is_number_start

Return whether a character can start a number.
=============
*/
bool is_number_start(QChar c) {
	return c.isDigit() || c == '-' || c == '+';
}

/*
=============
looks_like_path

Detect simple path-like strings.
=============
*/
bool looks_like_path(const QString& text) {
	return text.contains('/') || text.contains('\\');
}
}  // namespace

/*
=============
CfgSyntaxHighlighter
=============
*/
CfgSyntaxHighlighter::CfgSyntaxHighlighter(QTextDocument* document)
	: QSyntaxHighlighter(document) {
	update_formats(QApplication::palette());
}

/*
=============
set_palette

Update syntax colors based on a Qt palette.
=============
*/
void CfgSyntaxHighlighter::set_palette(const QPalette& palette) {
	update_formats(palette);
	rehighlight();
}

/*
=============
update_formats

Rebuild theme formats from palette values.
=============
*/
void CfgSyntaxHighlighter::update_formats(const QPalette& palette) {
	const QColor foreground = palette.color(QPalette::Text);
	const QColor background = palette.color(QPalette::Base);
	const QColor comment = palette.color(QPalette::Disabled, QPalette::Text);
	const QColor number = palette.color(QPalette::Link);
	const QColor accent = palette.color(QPalette::Highlight);
	const QColor subtle = palette.color(QPalette::Mid);
	const QColor warning = palette.color(QPalette::BrightText);

	formats_.comment = make_format(comment, true, false, false);
	formats_.comment_invalid = make_invalid_format(comment);
	formats_.string = make_format(QColor::fromHsl((accent.hue() + 30) % 360, 160, 180), false, false, false);
	formats_.string_invalid = make_invalid_format(formats_.string.foreground().color());
	formats_.number = make_format(number, false, false, false);
	formats_.identifier = make_format(foreground, false, false, false);
	formats_.operator_fmt = make_format(subtle, false, false, false);
	formats_.punctuation = make_format(subtle, false, false, false);
	formats_.invalid = make_invalid_format(warning.isValid() ? warning : foreground);

	formats_.command = make_format(accent, false, true, false);
	formats_.cvar_name = make_format(QColor::fromHsl((accent.hue() + 300) % 360, 160, 180), false, true, false);
	formats_.cvar_value = make_format(foreground, false, false, false);
	formats_.alias_name = make_format(QColor::fromHsl((accent.hue() + 60) % 360, 160, 180), false, true, false);
	formats_.alias_body = make_format(foreground, false, false, false);
	formats_.bind_key = make_format(QColor::fromHsl((accent.hue() + 120) % 360, 160, 180), false, true, false);
	formats_.bind_command = make_format(foreground, false, false, false);
	formats_.variable_ref = make_format(QColor::fromHsl((accent.hue() + 200) % 360, 170, 175), false, false, false);
	formats_.path = make_format(QColor::fromHsl((accent.hue() + 20) % 360, 110, 170), false, false, false);
	formats_.button_command = make_format(QColor::fromHsl((accent.hue() + 160) % 360, 170, 175), false, true, false);
	formats_.color_code = make_format(QColor::fromHsl((accent.hue() + 320) % 360, 200, 170), false, true, false);

	Q_UNUSED(background);
}

/*
=============
highlightBlock

Apply cfg syntax highlighting for the current text block.
=============
*/
void CfgSyntaxHighlighter::highlightBlock(const QString& text) {
	const int len = text.size();
	int i = 0;
	const bool prev_in_block_comment = (previousBlockState() == 1);
	bool in_block_comment = prev_in_block_comment;

	if (in_block_comment) {
		const int end = text.indexOf("*/");
		if (end == -1) {
			setFormat(0, len, formats_.comment_invalid);
			setCurrentBlockState(1);
			return;
		}
		setFormat(0, end + 2, formats_.comment);
		i = end + 2;
		in_block_comment = false;
	}

	struct TokenSpan {
		int start = 0;
		int end = 0;
		QString text;
		enum class Kind {
			Identifier,
			String,
			Number,
			Comment,
			Punctuation,
			Operator,
			VariableRef,
			Path,
			ColorCode,
			Invalid,
		} kind = Kind::Identifier;
	};

	QVector<TokenSpan> tokens;
	bool statement_start = true;
	QString verb;
	int token_index_in_statement = 0;

	auto push_token = [&](int start, int end, TokenSpan::Kind kind, const QString& value) {
		TokenSpan span;
		span.start = start;
		span.end = end;
		span.kind = kind;
		span.text = value;
		tokens.push_back(span);
	};

	auto consume_number = [&](int start) {
		int pos = start;
		if (pos < len && (text[pos] == '+' || text[pos] == '-')) {
			++pos;
		}
		bool has_digits = false;
		while (pos < len && text[pos].isDigit()) {
			++pos;
			has_digits = true;
		}
		if (pos < len && text[pos] == '.') {
			++pos;
			while (pos < len && text[pos].isDigit()) {
				++pos;
				has_digits = true;
			}
		}
		if (pos < len && (text[pos] == 'e' || text[pos] == 'E')) {
			++pos;
			if (pos < len && (text[pos] == '+' || text[pos] == '-')) {
				++pos;
			}
			while (pos < len && text[pos].isDigit()) {
				++pos;
				has_digits = true;
			}
		}
		return has_digits ? pos : start;
	};

	while (i < len) {
		const QChar c = text[i];
		if (c.isSpace()) {
			++i;
			continue;
		}

		if (i + 1 < len && text.midRef(i, 2) == "/*") {
			const int start = i;
			const int end = text.indexOf("*/", i + 2);
			if (end == -1) {
				setFormat(start, len - start, formats_.comment_invalid);
				setCurrentBlockState(1);
				return;
			}
			setFormat(start, end + 2 - start, formats_.comment);
			i = end + 2;
			continue;
		}

		if ((c == '/' && i + 1 < len && text[i + 1] == '/') || c == '#') {
			setFormat(i, len - i, formats_.comment);
			break;
		}

		if (c == '\"') {
			const int start = i;
			++i;
			bool closed = false;
			while (i < len) {
				const QChar ch = text[i];
				if (ch == '\\' && i + 1 < len) {
					i += 2;
					continue;
				}
				if (ch == '\"') {
					++i;
					closed = true;
					break;
				}
				++i;
			}
			const int end = i;
			setFormat(start, end - start, closed ? formats_.string : formats_.string_invalid);
			const QString token_text = text.mid(start, end - start);
			push_token(start, end, TokenSpan::Kind::String, token_text);
			if (closed) {
				int scan = start;
				while (scan < end) {
					const int caret = token_text.indexOf('^', scan - start);
					if (caret == -1) {
						break;
					}
					const int caret_pos = start + caret;
					if (caret_pos + 1 < end) {
						const QChar code = text[caret_pos + 1];
						if (code.isDigit()) {
							setFormat(caret_pos, 2, formats_.color_code);
						} else if (code == 'x' && caret_pos + 4 < end) {
							setFormat(caret_pos, 5, formats_.color_code);
						}
					}
					scan = caret_pos + 1;
				}
			}
			continue;
		}

		if (c == '$') {
			const int start = i;
			++i;
			if (i < len && text[i] == '{') {
				++i;
				while (i < len && text[i] != '}') {
					++i;
				}
				if (i < len && text[i] == '}') {
					++i;
				}
			} else {
				while (i < len && is_ident_char(text[i])) {
					++i;
				}
			}
			push_token(start, i, TokenSpan::Kind::VariableRef, text.mid(start, i - start));
			continue;
		}

		if (is_number_start(c)) {
			const int start = i;
			const int end = consume_number(start);
			if (end > start) {
				push_token(start, end, TokenSpan::Kind::Number, text.mid(start, end - start));
				i = end;
				continue;
			}
		}

		if (is_ident_char(c)) {
			const int start = i;
			while (i < len && is_ident_char(text[i])) {
				++i;
			}
			const QString word = text.mid(start, i - start);
			TokenSpan::Kind kind = TokenSpan::Kind::Identifier;
			if (looks_like_path(word)) {
				kind = TokenSpan::Kind::Path;
			}
			push_token(start, i, kind, word);
			continue;
		}

		if (c == ';' || c == '+' || c == '-') {
			push_token(i, i + 1, TokenSpan::Kind::Operator, QString(c));
			if (c == ';') {
				statement_start = true;
				verb.clear();
				token_index_in_statement = 0;
			}
			++i;
			continue;
		}

		if (c == ',') {
			push_token(i, i + 1, TokenSpan::Kind::Punctuation, QString(c));
			++i;
			continue;
		}

		push_token(i, i + 1, TokenSpan::Kind::Invalid, QString(c));
		++i;
	}

	auto apply_format = [&](const TokenSpan& token, const QTextCharFormat& fmt) {
		setFormat(token.start, token.end - token.start, fmt);
	};

	for (int idx = 0; idx < tokens.size(); ++idx) {
		const TokenSpan& token = tokens[idx];
		switch (token.kind) {
			case TokenSpan::Kind::Comment:
				apply_format(token, formats_.comment);
				break;
			case TokenSpan::Kind::String:
				break;
			case TokenSpan::Kind::Number:
				apply_format(token, formats_.number);
				break;
			case TokenSpan::Kind::Identifier:
				apply_format(token, formats_.identifier);
				break;
			case TokenSpan::Kind::Punctuation:
				apply_format(token, formats_.punctuation);
				break;
			case TokenSpan::Kind::Operator:
				apply_format(token, formats_.operator_fmt);
				break;
			case TokenSpan::Kind::VariableRef:
				apply_format(token, formats_.variable_ref);
				break;
			case TokenSpan::Kind::Path:
				apply_format(token, formats_.path);
				break;
			case TokenSpan::Kind::ColorCode:
				apply_format(token, formats_.color_code);
				break;
			case TokenSpan::Kind::Invalid:
				apply_format(token, formats_.invalid);
				break;
		}
	}

	statement_start = true;
	verb.clear();
	token_index_in_statement = 0;

	for (int idx = 0; idx < tokens.size(); ++idx) {
		const TokenSpan& token = tokens[idx];
		if (token.kind == TokenSpan::Kind::Operator && token.text == ";") {
			statement_start = true;
			verb.clear();
			token_index_in_statement = 0;
			continue;
		}
		if (statement_start && token.kind == TokenSpan::Kind::Operator && (token.text == "+" || token.text == "-")) {
			if (idx + 1 < tokens.size() && tokens[idx + 1].kind == TokenSpan::Kind::Identifier) {
				const TokenSpan& next = tokens[idx + 1];
				apply_format(token, formats_.button_command);
				apply_format(next, formats_.button_command);
				verb = (token.text + next.text).toLower();
				statement_start = false;
				token_index_in_statement = 1;
				++idx;
				continue;
			}
		}
		if (token.kind == TokenSpan::Kind::Identifier || token.kind == TokenSpan::Kind::VariableRef || token.kind == TokenSpan::Kind::String
		    || token.kind == TokenSpan::Kind::Number || token.kind == TokenSpan::Kind::Path) {
			if (statement_start) {
				verb = token.text.toLower();
				statement_start = false;
				token_index_in_statement = 1;
				if (token.text.startsWith('+') || token.text.startsWith('-')) {
					apply_format(token, formats_.button_command);
				} else {
					apply_format(token, formats_.command);
				}
			} else {
				if (verb == "set" || verb == "seta" || verb == "setu") {
					if (token_index_in_statement == 1) {
						apply_format(token, formats_.cvar_name);
					} else {
						apply_format(token, formats_.cvar_value);
					}
				} else if (verb == "bind") {
					if (token_index_in_statement == 1) {
						apply_format(token, formats_.bind_key);
					} else {
						apply_format(token, formats_.bind_command);
					}
				} else if (verb == "alias") {
					if (token_index_in_statement == 1) {
						apply_format(token, formats_.alias_name);
					} else {
						apply_format(token, formats_.alias_body);
					}
				} else if (verb == "vstr") {
					apply_format(token, formats_.variable_ref);
				}
				token_index_in_statement++;
			}
		}
	}

	if (in_block_comment) {
		setCurrentBlockState(1);
	} else {
		setCurrentBlockState(0);
	}
}
