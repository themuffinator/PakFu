#include "preview_pane.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
/*
=============
hex_dump

Format a byte buffer into a hex/ascii dump string for preview display.
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

/*
=============
format_duration

Format milliseconds as a mm:ss or hh:mm:ss time string.
=============
*/
QString format_duration(qint64 millis) {
	if (millis < 0) {
		return "--:--";
	}
	const qint64 total_seconds = millis / 1000;
	const qint64 seconds = total_seconds % 60;
	const qint64 minutes = (total_seconds / 60) % 60;
	const qint64 hours = total_seconds / 3600;
	if (hours > 0) {
		return QString("%1:%2:%3")
			.arg(hours, 2, 10, QLatin1Char('0'))
			.arg(minutes, 2, 10, QLatin1Char('0'))
			.arg(seconds, 2, 10, QLatin1Char('0'));
	}
	return QString("%1:%2")
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'));
}
}  // namespace

/*
=============
PreviewPane::PreviewPane

Create the preview pane widget and populate its stacked preview pages.
=============
*/
PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
	build_ui();
	show_placeholder();
}

/*
=============
PreviewPane::resizeEvent

Reflow the current preview when the pane size changes.
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
PreviewPane::build_ui

Construct the shared header and preview content pages.
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

	// Audio page.
	audio_page_ = new QWidget(stack_);
	auto* audio_layout = new QVBoxLayout(audio_page_);
	audio_layout->setContentsMargins(12, 12, 12, 12);
	audio_layout->setSpacing(10);

	auto* controls_layout = new QHBoxLayout();
	controls_layout->setSpacing(8);
	audio_prev_button_ = new QPushButton("Previous", audio_page_);
	audio_prev_button_->setToolTip("Previous audio file");
	audio_play_button_ = new QPushButton("Play", audio_page_);
	audio_play_button_->setToolTip("Play or pause");
	audio_next_button_ = new QPushButton("Next", audio_page_);
	audio_next_button_->setToolTip("Next audio file");
	audio_info_button_ = new QToolButton(audio_page_);
	audio_info_button_->setText("?");
	audio_info_button_->setToolTip("Audio details will appear here once loaded.");

	controls_layout->addWidget(audio_prev_button_);
	controls_layout->addWidget(audio_play_button_);
	controls_layout->addWidget(audio_next_button_);
	controls_layout->addStretch();
	controls_layout->addWidget(audio_info_button_);
	audio_layout->addLayout(controls_layout);

	audio_position_slider_ = new QSlider(Qt::Horizontal, audio_page_);
	audio_position_slider_->setRange(0, 0);
	audio_position_slider_->setToolTip("Seek");
	audio_layout->addWidget(audio_position_slider_);

	auto* volume_layout = new QHBoxLayout();
	volume_layout->setSpacing(8);
	auto* volume_label = new QLabel("Volume", audio_page_);
	audio_volume_slider_ = new QSlider(Qt::Horizontal, audio_page_);
	audio_volume_slider_->setRange(0, 100);
	audio_volume_slider_->setValue(80);
	audio_volume_slider_->setToolTip("Volume");
	volume_layout->addWidget(volume_label);
	volume_layout->addWidget(audio_volume_slider_, 1);
	audio_layout->addLayout(volume_layout);

	audio_player_ = new QMediaPlayer(this);
	audio_output_ = new QAudioOutput(this);
	audio_output_->setVolume(static_cast<float>(audio_volume_slider_->value()) / 100.0f);
	audio_player_->setAudioOutput(audio_output_);

	connect(audio_prev_button_, &QPushButton::clicked, this, &PreviewPane::request_previous_audio);
	connect(audio_next_button_, &QPushButton::clicked, this, &PreviewPane::request_next_audio);
	connect(audio_play_button_, &QPushButton::clicked, this, [this]() {
		if (!audio_player_) {
			return;
		}
		if (audio_player_->playbackState() == QMediaPlayer::PlayingState) {
			audio_player_->pause();
		} else {
			audio_player_->play();
		}
	});
	connect(audio_volume_slider_, &QSlider::valueChanged, this, [this](int value) {
		if (audio_output_) {
			audio_output_->setVolume(static_cast<float>(value) / 100.0f);
		}
	});
	connect(audio_position_slider_, &QSlider::sliderPressed, this, [this]() {
		audio_user_scrubbing_ = true;
	});
	connect(audio_position_slider_, &QSlider::sliderReleased, this, [this]() {
		audio_user_scrubbing_ = false;
		if (audio_player_) {
			audio_player_->setPosition(audio_position_slider_->value());
		}
	});
	connect(audio_position_slider_, &QSlider::sliderMoved, this, [this](int value) {
		if (audio_user_scrubbing_ && audio_player_) {
			audio_player_->setPosition(value);
		}
	});
	connect(audio_player_, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
		if (audio_position_slider_) {
			audio_position_slider_->setRange(0, static_cast<int>(duration));
		}
		update_audio_tooltip();
	});
	connect(audio_player_, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
		if (!audio_user_scrubbing_ && audio_position_slider_) {
			audio_position_slider_->setValue(static_cast<int>(position));
		}
	});
	connect(audio_player_, &QMediaPlayer::metaDataChanged, this, [this]() {
		update_audio_tooltip();
	});
	connect(audio_player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
		if (audio_play_button_) {
			audio_play_button_->setText(state == QMediaPlayer::PlayingState ? "Pause" : "Play");
		}
	});
	stack_->addWidget(audio_page_);
}

/*
=============
PreviewPane::set_header

Update the header title and subtitle text.
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
PreviewPane::show_placeholder

Show the default placeholder panel when no item is selected.
=============
*/
void PreviewPane::show_placeholder() {
	stop_audio_playback();
	set_header("Preview", "Select a file from the list.");
	if (stack_ && placeholder_page_) {
		stack_->setCurrentWidget(placeholder_page_);
	}
}

/*
=============
PreviewPane::show_message

Show a centered message panel with a title and body text.
=============
*/
void PreviewPane::show_message(const QString& title, const QString& body) {
	stop_audio_playback();
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
PreviewPane::show_text

Show a plain-text preview panel.
=============
*/
void PreviewPane::show_text(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	set_header(title, subtitle);
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	if (stack_ && text_page_) {
		stack_->setCurrentWidget(text_page_);
	}
}

/*
=============
PreviewPane::show_binary

Show a binary hex dump preview panel.
=============
*/
void PreviewPane::show_binary(const QString& title,
								const QString& subtitle,
								const QByteArray& bytes,
								bool truncated) {
	stop_audio_playback();
	QString sub = subtitle;
	if (truncated) {
		sub = sub.isEmpty() ? "Preview truncated." : (sub + "  (Preview truncated)");
	}
	set_header(title, sub);
	if (text_view_) {
		text_view_->setPlainText(hex_dump(bytes, 256));
	}
	if (stack_ && text_page_) {
		stack_->setCurrentWidget(text_page_);
	}
}

/*
=============
PreviewPane::set_image_pixmap

Scale and display an image pixmap in the preview.
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
PreviewPane::show_image_from_bytes

Decode and display an image from raw bytes.
=============
*/
void PreviewPane::show_image_from_bytes(const QString& title,
									   const QString& subtitle,
									   const QByteArray& bytes) {
	stop_audio_playback();
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
PreviewPane::show_image_from_file

Load and display an image from a file path.
=============
*/
void PreviewPane::show_image_from_file(const QString& title,
								  const QString& subtitle,
								  const QString& file_path) {
	stop_audio_playback();
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
PreviewPane::show_audio_from_file

Prepare the audio player controls for a selected audio file.
=============
*/
void PreviewPane::show_audio_from_file(const QString& title,
								const QString& subtitle,
								const QString& file_path) {
	set_header(title, subtitle);
	set_audio_source(file_path);
	if (stack_ && audio_page_) {
		stack_->setCurrentWidget(audio_page_);
	}
}

/*
=============
PreviewPane::stop_audio_playback

Stop any active audio playback and reset the player state.
=============
*/
void PreviewPane::stop_audio_playback() {
	if (!audio_player_) {
		return;
	}
	if (audio_player_->playbackState() != QMediaPlayer::StoppedState) {
		audio_player_->stop();
	}
}

/*
=============
PreviewPane::set_audio_source

Load a new audio source into the player and reset UI controls.
=============
*/
void PreviewPane::set_audio_source(const QString& file_path) {
	if (!audio_player_) {
		return;
	}
	audio_file_path_.clear();
	audio_player_->stop();
	audio_player_->setSource(QUrl::fromLocalFile(file_path));
	audio_file_path_ = file_path;
	sync_audio_controls();
	update_audio_tooltip();
}

/*
=============
PreviewPane::sync_audio_controls

Reset audio control widgets to a consistent initial state.
=============
*/
void PreviewPane::sync_audio_controls() {
	if (audio_position_slider_) {
		audio_position_slider_->setRange(0, 0);
		audio_position_slider_->setValue(0);
	}
	if (audio_play_button_) {
		audio_play_button_->setText("Play");
	}
}

/*
=============
PreviewPane::update_audio_tooltip

Populate the audio details tooltip from metadata and file info.
=============
*/
void PreviewPane::update_audio_tooltip() {
	if (!audio_info_button_) {
		return;
	}
	QStringList lines;
	if (!audio_file_path_.isEmpty()) {
		const QFileInfo info(audio_file_path_);
		lines << QString("File: %1").arg(info.fileName());
		lines << QString("Size: %1 bytes").arg(info.size());
		if (!info.suffix().isEmpty()) {
			lines << QString("Format: %1").arg(info.suffix().toLower());
		}
	}
	if (audio_player_) {
		const qint64 duration = audio_player_->duration();
		if (duration > 0) {
			lines << QString("Duration: %1").arg(format_duration(duration));
		}
		const QMediaMetaData meta = audio_player_->metaData();
		const QVariant title = meta.value(QMediaMetaData::Title);
		if (title.isValid() && !title.toString().isEmpty()) {
			lines << QString("Title: %1").arg(title.toString());
		}
		const QVariant artist = meta.value(QMediaMetaData::ContributingArtist);
		if (artist.isValid()) {
			const QStringList artists = artist.toStringList();
			if (!artists.isEmpty()) {
				lines << QString("Artist: %1").arg(artists.join(", "));
			}
		}
		const QVariant bitrate = meta.value(QMediaMetaData::AudioBitRate);
		if (bitrate.isValid()) {
			lines << QString("Bitrate: %1 kbps").arg(bitrate.toInt() / 1000);
		}
		const QVariant codec = meta.value(QMediaMetaData::AudioCodec);
		if (codec.isValid() && !codec.toString().isEmpty()) {
			lines << QString("Codec: %1").arg(codec.toString());
		}
	}
	if (lines.isEmpty()) {
		lines << "Audio details unavailable.";
	}
	audio_info_button_->setToolTip(lines.join("\n"));
}
