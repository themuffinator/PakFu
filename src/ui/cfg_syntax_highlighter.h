#pragma once

#include <QPalette>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class CfgSyntaxHighlighter : public QSyntaxHighlighter {
	Q_OBJECT

public:
	explicit CfgSyntaxHighlighter(QTextDocument* document);

	void set_palette(const QPalette& palette);

protected:
	void highlightBlock(const QString& text) override;

private:
	struct ThemeFormats {
		QTextCharFormat comment;
		QTextCharFormat comment_invalid;
		QTextCharFormat string;
		QTextCharFormat string_invalid;
		QTextCharFormat number;
		QTextCharFormat identifier;
		QTextCharFormat operator_fmt;
		QTextCharFormat punctuation;
		QTextCharFormat invalid;
		QTextCharFormat command;
		QTextCharFormat cvar_name;
		QTextCharFormat cvar_value;
		QTextCharFormat alias_name;
		QTextCharFormat alias_body;
		QTextCharFormat bind_key;
		QTextCharFormat bind_command;
		QTextCharFormat variable_ref;
		QTextCharFormat path;
		QTextCharFormat button_command;
		QTextCharFormat color_code;
	} formats_;

	void update_formats(const QPalette& palette);
};
