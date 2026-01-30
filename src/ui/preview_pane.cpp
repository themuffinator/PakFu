#include "preview_pane.h"

#include "cfg_syntax_highlighter.h"

#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
/*
=============
hex_dump

Build a printable hex dump of a byte buffer.
=============
*/
QString hex_dump(const QByteArray& bytes, int max_lines) {
	const int kPerLine = 16;
	const int n = bytes.size();
	const int lines = (n + kPerLine - 1) / kPerLine;
	const int out_lines = qMin(lines, max_lines);

	QString out;
	out.reserve(out_lines * 80);
	for (int line = 0; line < out_lines; ++line) {
		const int base = line * kPerLine;
		out += QString("%1  ").arg(base, 8, 16, QLatin1Char('0'));
		for (int i = 0; i < kPerLine; ++i) {
			const int idx = base + i;
			if (idx < n) {
				out += QString("%1 ").arg(static_cast<unsigned char>(bytes[idx]), 2, 16, QLatin1Char('0'));
			} else {
				out += "   ";
			}
		}
		out += " ";
		for (int i = 0; i < kPerLine; ++i) {
			const int idx = base + i;
			if (idx >= n) {
				break;
			}
			const unsigned char c = static_cast<unsigned char>(bytes[idx]);
			out += (c >= 32 && c < 127) ? QChar(c) : QChar('.');
		}
		out += "\n";
	}
	return out;
}
}  // namespace

/*
=============
PreviewPane
=============
*/
PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
	build_ui();
	show_placeholder();
}

/*
=============
resizeEvent

Handle preview resizing for scaled images.
=============
*/
void PreviewPane::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	if (stack_ && stack_->currentWidget() == image_page_ && !original_pixmap_.isNull()) {
		set_image_pixmap(original_pixmap_);
	}
}

/*
=============
changeEvent

Refresh syntax colors when the palette changes.
=============
*/
void PreviewPane::changeEvent(QEvent* event) {
	QWidget::changeEvent(event);
	if (event && event->type() == QEvent::PaletteChange) {
		update_cfg_highlighter();
	}
}

/*
=============
build_ui

Build the preview pane layout and widgets.
=============
*/
void PreviewPane::build_ui() {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(14, 14, 14, 14);
	layout->setSpacing(10);

	auto* header = new QWidget(this);
	auto* header_layout = new QVBoxLayout(header);
	header_layout->setContentsMargins(8, 8, 8, 8);
	header_layout->setSpacing(2);

	title_label_ = new QLabel(header);
	title_label_->setWordWrap(false);
	QFont title_font = title_label_->font();
	title_font.setPointSize(title_font.pointSize() + 1);
	title_font.setWeight(QFont::DemiBold);
	title_label_->setFont(title_font);
	header_layout->addWidget(title_label_);

	subtitle_label_ = new QLabel(header);
	subtitle_label_->setWordWrap(true);
	subtitle_label_->setStyleSheet("color: rgba(180, 180, 180, 220);");
	header_layout->addWidget(subtitle_label_);

	header->setObjectName("previewHeader");
	header->setStyleSheet(
		"#previewHeader {"
		"  border: 1px solid rgba(120, 120, 120, 70);"
		"  border-radius: 10px;"
		"  background-color: rgba(255, 255, 255, 20);"
		"}");
	layout->addWidget(header, 0);

	stack_ = new QStackedWidget(this);
	layout->addWidget(stack_, 1);

	// Placeholder.
	placeholder_page_ = new QWidget(stack_);
	auto* ph_layout = new QVBoxLayout(placeholder_page_);
	ph_layout->setContentsMargins(18, 18, 18, 18);
	ph_layout->addStretch();
	placeholder_label_ = new QLabel("Select a file to preview.", placeholder_page_);
	placeholder_label_->setAlignment(Qt::AlignCenter);
	placeholder_label_->setWordWrap(true);
	placeholder_label_->setStyleSheet("color: rgba(200, 200, 200, 190);");
	ph_layout->addWidget(placeholder_label_);
	ph_layout->addStretch();
	stack_->addWidget(placeholder_page_);

	// Message page.
	message_page_ = new QWidget(stack_);
	auto* msg_layout = new QVBoxLayout(message_page_);
	msg_layout->setContentsMargins(18, 18, 18, 18);
	msg_layout->addStretch();
	message_label_ = new QLabel(message_page_);
	message_label_->setAlignment(Qt::AlignCenter);
	message_label_->setWordWrap(true);
	msg_layout->addWidget(message_label_);
	msg_layout->addStretch();
	stack_->addWidget(message_page_);

	// Image page.
	image_page_ = new QWidget(stack_);
	auto* img_layout = new QVBoxLayout(image_page_);
	img_layout->setContentsMargins(0, 0, 0, 0);
	image_scroll_ = new QScrollArea(image_page_);
	image_scroll_->setWidgetResizable(true);
	image_scroll_->setFrameShape(QFrame::NoFrame);
	image_label_ = new QLabel(image_scroll_);
	image_label_->setAlignment(Qt::AlignCenter);
	image_label_->setScaledContents(false);
	image_scroll_->setWidget(image_label_);
	img_layout->addWidget(image_scroll_);
	stack_->addWidget(image_page_);

	// Text/binary page (shared).
	text_page_ = new QWidget(stack_);
	auto* text_layout = new QVBoxLayout(text_page_);
	text_layout->setContentsMargins(0, 0, 0, 0);
	text_view_ = new QPlainTextEdit(text_page_);
	text_view_->setReadOnly(true);
	text_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
	QFont mono("Consolas");
	if (!mono.exactMatch()) {
		mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	}
	text_view_->setFont(mono);
	text_layout->addWidget(text_view_);
	stack_->addWidget(text_page_);
}

/*
=============
set_header

Update the preview title and subtitle.
=============
*/
void PreviewPane::set_header(const QString& title, const QString& subtitle) {
	if (title_label_) {
		title_label_->setText(title);
	}
	if (subtitle_label_) {
		subtitle_label_->setText(subtitle);
	}
}

/*
=============
show_placeholder

Show the empty preview state.
=============
*/
void PreviewPane::show_placeholder() {
	set_header("Preview", "Select a file from the list.");
	if (stack_ && placeholder_page_) {
		stack_->setCurrentWidget(placeholder_page_);
	}
}

/*
=============
show_message

Show a centered message in the preview pane.
=============
*/
void PreviewPane::show_message(const QString& title, const QString& body) {
	set_header(title, QString());
	if (message_label_) {
		message_label_->setText(body);
		message_label_->setStyleSheet("color: rgba(220, 220, 220, 210);");
	}
	if (stack_ && message_page_) {
		stack_->setCurrentWidget(message_page_);
	}
}

/*
=============
show_cfg_text

Show syntax-highlighted cfg text.
=============
*/
void PreviewPane::show_cfg_text(const QString& title, const QString& subtitle, const QString& text) {
	set_header(title, subtitle);
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	update_cfg_highlighter();
	if (stack_ && text_page_) {
		stack_->setCurrentWidget(text_page_);
	}
}

/*
=============
show_text

Show plain text content in the preview pane.
=============
*/
void PreviewPane::show_text(const QString& title, const QString& subtitle, const QString& text) {
	set_header(title, subtitle);
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	if (cfg_highlighter_) {
		cfg_highlighter_->setDocument(nullptr);
	}
	if (stack_ && text_page_) {
		stack_->setCurrentWidget(text_page_);
	}
}

/*
=============
show_binary

Show a hex preview for binary data.
=============
*/
void PreviewPane::show_binary(const QString& title,
			      const QString& subtitle,
			      const QByteArray& bytes,
			      bool truncated) {
	QString sub = subtitle;
	if (truncated) {
		sub = sub.isEmpty() ? "Preview truncated." : (sub + "  (Preview truncated)");
	}
	set_header(title, sub);
	if (text_view_) {
		text_view_->setPlainText(hex_dump(bytes, 256));
	}
	if (cfg_highlighter_) {
		cfg_highlighter_->setDocument(nullptr);
	}
	if (stack_ && text_page_) {
		stack_->setCurrentWidget(text_page_);
	}
}

/*
=============
set_image_pixmap

Scale and set the preview image pixmap.
=============
*/
void PreviewPane::set_image_pixmap(const QPixmap& pixmap) {
	if (!image_label_) {
		return;
	}
	original_pixmap_ = pixmap;
	if (pixmap.isNull()) {
		image_label_->setPixmap(QPixmap());
		return;
	}

	// Fit to available viewport while keeping aspect ratio.
	const QSize avail =
		image_scroll_ ? image_scroll_->viewport()->size() : QSize();
	if (avail.isValid()) {
		image_label_->setPixmap(pixmap.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	} else {
		image_label_->setPixmap(pixmap);
	}
}

/*
=============
show_image_from_bytes

Decode and display an image preview.
=============
*/
void PreviewPane::show_image_from_bytes(const QString& title,
					const QString& subtitle,
					const QByteArray& bytes) {
	set_header(title, subtitle);
	QPixmap pixmap;
	pixmap.loadFromData(bytes);
	if (pixmap.isNull()) {
		show_message(title, "Unable to decode this image format.");
		return;
	}
	set_image_pixmap(pixmap);
	if (stack_ && image_page_) {
		stack_->setCurrentWidget(image_page_);
	}
}

/*
=============
show_image_from_file

Load and display an image from disk.
=============
*/
void PreviewPane::show_image_from_file(const QString& title,
				       const QString& subtitle,
				       const QString& file_path) {
	set_header(title, subtitle);
	QPixmap pixmap(file_path);
	if (pixmap.isNull()) {
		show_message(title, "Unable to load this image file.");
		return;
	}
	set_image_pixmap(pixmap);
	if (stack_ && image_page_) {
		stack_->setCurrentWidget(image_page_);
	}
}

/*
=============
update_cfg_highlighter

Ensure cfg highlighting uses the current palette.
=============
*/
void PreviewPane::update_cfg_highlighter() {
	if (!text_view_) {
		return;
	}
	if (!cfg_highlighter_) {
		cfg_highlighter_ = new CfgSyntaxHighlighter(text_view_->document());
	}
	if (cfg_highlighter_->document() != text_view_->document()) {
		cfg_highlighter_->setDocument(text_view_->document());
	}
	cfg_highlighter_->set_palette(text_view_->palette());
}
