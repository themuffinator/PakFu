#include "ui/video_player_widget.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStringList>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <QEnterEvent>

#include "ui/ui_icons.h"

namespace {
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

VideoPlayerWidget::VideoPlayerWidget(QWidget* parent) : QWidget(parent) {
	QSettings settings;
	texture_smoothing_ = settings.value("preview/image/textureSmoothing", false).toBool();
	build_ui();
	update_ui_state();
}

VideoPlayerWidget::~VideoPlayerWidget() {
	disconnect();
	unload();
}

bool VideoPlayerWidget::has_media() const {
	return !file_path_.isEmpty();
}

QString VideoPlayerWidget::file_path() const {
	return file_path_;
}

qint64 VideoPlayerWidget::duration_ms() const {
	return player_ ? player_->duration() : -1;
}

QMediaMetaData VideoPlayerWidget::meta_data() const {
	return player_ ? player_->metaData() : QMediaMetaData{};
}

QSize VideoPlayerWidget::video_size() const {
	return current_video_size_;
}

bool VideoPlayerWidget::load_file(const QString& file_path, QString* error) {
	if (error) {
		error->clear();
	}
	unload();

	if (!player_ || !video_widget_) {
		if (error) {
			*error = "Video playback is not available.";
		}
		return false;
	}

	file_path_ = file_path;
	current_video_size_ = {};
	clear_status_override();

	if (position_slider_) {
		position_slider_->setEnabled(false);
		position_slider_->setRange(0, 0);
		position_slider_->setValue(0);
	}

	player_->setSource(QUrl::fromLocalFile(file_path));
	set_status_text({});
	update_ui_state();
	emit media_info_changed();

	// Prefetch first frame (muted) so selection shows a still preview like CIN/ROQ.
	start_prefetch_first_frame();

	return true;
}

void VideoPlayerWidget::unload() {
	stop_prefetch_first_frame();
	clear_status_override();

	if (player_) {
		player_->stop();
		player_->setSource(QUrl());
	}

	file_path_.clear();
	current_video_size_ = {};
	user_scrubbing_ = false;
	resume_after_scrub_ = false;

	if (position_slider_) {
		position_slider_->setEnabled(false);
		position_slider_->setRange(0, 0);
		position_slider_->setValue(0);
	}

	set_status_text({});
	update_ui_state();
	emit media_info_changed();
}

void VideoPlayerWidget::play_from_start() {
	if (!has_media() || !player_) {
		return;
	}
	stop_prefetch_first_frame();
	clear_status_override();
	player_->setPosition(0);
	player_->play();
}

void VideoPlayerWidget::set_texture_smoothing(bool enabled) {
	// QVideoWidget does its own scaling; keep this for UI consistency.
	texture_smoothing_ = enabled;
}

void VideoPlayerWidget::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
}

void VideoPlayerWidget::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
}

void VideoPlayerWidget::enterEvent(QEnterEvent* event) {
	QWidget::enterEvent(event);
	if (controls_container_) {
		controls_container_->setVisible(true);
	}
}

void VideoPlayerWidget::leaveEvent(QEvent* event) {
	QWidget::leaveEvent(event);
	if (controls_container_) {
		controls_container_->setVisible(false);
	}
}

void VideoPlayerWidget::build_ui() {
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);

	video_container_ = new QFrame(this);
	video_container_->setMinimumHeight(240);
	video_container_->setObjectName("videoContainer");
	video_container_->setStyleSheet(
		"#videoContainer {"
		"  background-color: rgba(0,0,0,60);"
		"  border: 1px solid rgba(120,120,120,70);"
		"  border-radius: 8px;"
		"}");
	root->addWidget(video_container_, 1);

	auto* video_layout = new QVBoxLayout(video_container_);
	video_layout->setContentsMargins(0, 0, 0, 0);
	video_layout->setSpacing(0);

	video_widget_ = new QVideoWidget(video_container_);
	video_widget_->setAspectRatioMode(Qt::KeepAspectRatio);
	video_layout->addWidget(video_widget_, 1);

	controls_container_ = new QWidget(this);
	controls_container_->setVisible(false);
	root->addWidget(controls_container_, 0);

	auto* controls_root = new QVBoxLayout(controls_container_);
	controls_root->setContentsMargins(0, 0, 0, 0);
	controls_root->setSpacing(6);

	position_slider_ = new QSlider(Qt::Horizontal, controls_container_);
	position_slider_->setRange(0, 0);
	controls_root->addWidget(position_slider_, 0);

	auto* controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->setSpacing(8);

	prev_button_ = new QToolButton(controls_container_);
	prev_button_->setAutoRaise(true);
	prev_button_->setCursor(Qt::PointingHandCursor);
	prev_button_->setIcon(UiIcons::icon(UiIcons::Id::MediaPrevious, style()));
	prev_button_->setToolTip("Previous video file");

	play_button_ = new QToolButton(controls_container_);
	play_button_->setAutoRaise(true);
	play_button_->setCursor(Qt::PointingHandCursor);
	play_button_->setIcon(UiIcons::icon(UiIcons::Id::MediaPlay, style()));
	play_button_->setToolTip("Play/Pause");

	next_button_ = new QToolButton(controls_container_);
	next_button_->setAutoRaise(true);
	next_button_->setCursor(Qt::PointingHandCursor);
	next_button_->setIcon(UiIcons::icon(UiIcons::Id::MediaNext, style()));
	next_button_->setToolTip("Next video file");

	stop_button_ = new QToolButton(controls_container_);
	stop_button_->setAutoRaise(true);
	stop_button_->setCursor(Qt::PointingHandCursor);
	stop_button_->setIcon(UiIcons::icon(UiIcons::Id::MediaStop, style()));
	stop_button_->setToolTip("Stop");

	const QSize icon_sz(18, 18);
	prev_button_->setIconSize(icon_sz);
	play_button_->setIconSize(icon_sz);
	next_button_->setIconSize(icon_sz);
	stop_button_->setIconSize(icon_sz);

	prev_button_->setFixedSize(32, 28);
	play_button_->setFixedSize(40, 28);
	next_button_->setFixedSize(32, 28);
	stop_button_->setFixedSize(32, 28);

	controls->addWidget(prev_button_);
	controls->addWidget(play_button_);
	controls->addWidget(next_button_);
	controls->addWidget(stop_button_);
	controls->addStretch(1);

	volume_scroll_ = new QScrollBar(Qt::Vertical, controls_container_);
	volume_scroll_->setRange(0, 100);
	volume_scroll_->setValue(80);
	volume_scroll_->setPageStep(10);
	volume_scroll_->setSingleStep(2);
	volume_scroll_->setFixedWidth(14);
	volume_scroll_->setFixedHeight(56);
	volume_scroll_->setInvertedAppearance(true);
	volume_scroll_->setToolTip("Volume");
	volume_scroll_->setStyleSheet(
		"QScrollBar { background: transparent; }"
		"QScrollBar::add-line, QScrollBar::sub-line { height: 0px; }"
		"QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");
	controls->addWidget(volume_scroll_, 0, Qt::AlignVCenter);

	controls_root->addLayout(controls);

	status_label_ = new QLabel(this);
	status_label_->setWordWrap(true);
	status_label_->setStyleSheet("color: rgba(180, 180, 180, 220);");
	root->addWidget(status_label_, 0);

	player_ = new QMediaPlayer(this);
	audio_output_ = new QAudioOutput(this);
	player_->setAudioOutput(audio_output_);
	player_->setVideoOutput(video_widget_);
	video_sink_ = video_widget_->videoSink();

	on_volume_changed(volume_scroll_ ? volume_scroll_->value() : 80);

	connect(prev_button_, &QToolButton::clicked, this, [this]() { emit request_previous_media(); });
	connect(next_button_, &QToolButton::clicked, this, [this]() { emit request_next_media(); });
	connect(play_button_, &QToolButton::clicked, this, [this]() {
		if (!player_) {
			return;
		}
		if (player_->playbackState() == QMediaPlayer::PlayingState) {
			pause();
		} else {
			play();
		}
	});
	connect(stop_button_, &QToolButton::clicked, this, [this]() { stop(); });

	connect(position_slider_, &QSlider::sliderPressed, this, &VideoPlayerWidget::on_slider_pressed);
	connect(position_slider_, &QSlider::sliderReleased, this, &VideoPlayerWidget::on_slider_released);
	connect(position_slider_, &QSlider::sliderMoved, this, [this](int value) {
		if (!user_scrubbing_ || !player_) {
			return;
		}
		player_->setPosition(static_cast<qint64>(value));
	});

	connect(volume_scroll_, &QScrollBar::valueChanged, this, &VideoPlayerWidget::on_volume_changed);

	if (video_sink_) {
		connect(video_sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
			if (!frame.isValid()) {
				return;
			}
			const QSize sz = frame.size();
			if (sz.isValid()) {
				current_video_size_ = sz;
			}
			if (prefetch_first_frame_) {
				stop_prefetch_first_frame();
			}
			update_status_auto();
			emit media_info_changed();
		});
	}

	connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
		if (position_slider_) {
			position_slider_->setRange(0, static_cast<int>(qMax<qint64>(0, duration)));
			position_slider_->setEnabled(duration > 0);
		}
		update_status_auto();
		emit media_info_changed();
	});
	connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
		if (!user_scrubbing_ && position_slider_) {
			position_slider_->setValue(static_cast<int>(position));
		}
		update_status_auto();
	});
	connect(player_, &QMediaPlayer::metaDataChanged, this, [this]() {
		update_status_auto();
		emit media_info_changed();
	});
	connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
		if (status == QMediaPlayer::InvalidMedia) {
			stop_prefetch_first_frame();
			const QString err = player_ ? player_->errorString().trimmed() : QString();
			set_status_text(err.isEmpty() ? "Unsupported/invalid media." : ("Unsupported/invalid media: " + err));
		} else if (status == QMediaPlayer::LoadingMedia) {
			clear_status_override();
			update_status_auto();
		}
		update_ui_state();
	});
	connect(player_, &QMediaPlayer::hasAudioChanged, this, [this](bool) {
		update_ui_state();
		update_status_auto();
		emit media_info_changed();
	});
	connect(player_, &QMediaPlayer::hasVideoChanged, this, [this](bool) {
		update_ui_state();
		update_status_auto();
		emit media_info_changed();
	});
	connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
		if (!play_button_) {
			return;
		}
		play_button_->setIcon(UiIcons::icon(
			state == QMediaPlayer::PlayingState ? UiIcons::Id::MediaPause : UiIcons::Id::MediaPlay, style()));
		update_status_auto();
	});
	connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& errorString) {
		stop_prefetch_first_frame();
		const QString msg = errorString.trimmed();
		set_status_text(msg.isEmpty() ? "Video error." : ("Video error: " + msg));
		update_ui_state();
	});
}

void VideoPlayerWidget::update_ui_state() {
	const bool has = has_media();

	if (prev_button_) {
		prev_button_->setEnabled(has);
	}
	if (next_button_) {
		next_button_->setEnabled(has);
	}
	if (stop_button_) {
		stop_button_->setEnabled(has);
	}
	if (play_button_) {
		play_button_->setEnabled(has);
		if (player_) {
			const bool playing = (player_->playbackState() == QMediaPlayer::PlayingState);
			play_button_->setIcon(UiIcons::icon(playing ? UiIcons::Id::MediaPause : UiIcons::Id::MediaPlay, style()));
		} else {
			play_button_->setIcon(UiIcons::icon(UiIcons::Id::MediaPlay, style()));
		}
	}
	if (position_slider_) {
		position_slider_->setEnabled(has && player_ && player_->duration() > 0);
	}
	if (volume_scroll_) {
		volume_scroll_->setEnabled(has && player_ && player_->hasAudio());
	}
}

void VideoPlayerWidget::set_status_text(const QString& text) {
	if (!status_label_) {
		return;
	}

	const QString trimmed = text.trimmed();
	if (!trimmed.isEmpty()) {
		status_override_ = trimmed;
		status_label_->setText(status_override_);
		return;
	}

	clear_status_override();
	update_status_auto();
}

void VideoPlayerWidget::update_status_auto() {
	if (!status_label_) {
		return;
	}
	if (!status_override_.isEmpty()) {
		status_label_->setText(status_override_);
		return;
	}

	QStringList parts;

	QSize res = current_video_size_;
	if ((!res.isValid() || res.isEmpty()) && player_) {
		const QVariant v = player_->metaData().value(QMediaMetaData::Resolution);
		if (v.canConvert<QSize>()) {
			res = v.toSize();
		}
	}
	if (res.isValid() && !res.isEmpty()) {
		parts << QString("%1x%2").arg(res.width()).arg(res.height());
	}

	if (player_) {
		const qint64 duration = player_->duration();
		const qint64 position = player_->position();
		if (duration > 0) {
			parts << QString("%1 / %2").arg(format_duration(position), format_duration(duration));
		} else if (position > 0) {
			parts << format_duration(position);
		}

		if (player_->hasAudio()) {
			parts << "Audio";
		}
	}

	if (!file_path_.isEmpty()) {
		parts << QFileInfo(file_path_).fileName();
	}

	status_label_->setText(parts.isEmpty() ? QString() : parts.join("  •  "));
}

void VideoPlayerWidget::clear_status_override() {
	status_override_.clear();
}

void VideoPlayerWidget::start_prefetch_first_frame() {
	if (!player_ || !audio_output_ || !has_media()) {
		return;
	}
	if (player_->playbackState() == QMediaPlayer::PlayingState) {
		return;
	}

	prefetch_first_frame_ = true;
	prefetch_saved_volume_ = audio_output_->volume();
	audio_output_->setVolume(0.0f);
	player_->setPosition(0);
	player_->play();
}

void VideoPlayerWidget::stop_prefetch_first_frame() {
	if (!prefetch_first_frame_) {
		return;
	}
	prefetch_first_frame_ = false;
	if (player_) {
		player_->pause();
	}
	if (audio_output_) {
		audio_output_->setVolume(prefetch_saved_volume_);
	}
}

void VideoPlayerWidget::play() {
	if (!player_ || !has_media()) {
		return;
	}
	stop_prefetch_first_frame();
	clear_status_override();
	player_->play();
	update_ui_state();
}

void VideoPlayerWidget::pause() {
	if (!player_ || !has_media()) {
		return;
	}
	stop_prefetch_first_frame();
	player_->pause();
	update_ui_state();
}

void VideoPlayerWidget::stop() {
	if (!player_ || !has_media()) {
		return;
	}
	stop_prefetch_first_frame();
	player_->stop();
	player_->setPosition(0);
	update_ui_state();
}

void VideoPlayerWidget::on_slider_pressed() {
	user_scrubbing_ = true;
	resume_after_scrub_ = player_ && (player_->playbackState() == QMediaPlayer::PlayingState);
	stop_prefetch_first_frame();
	if (player_) {
		player_->pause();
	}
}

void VideoPlayerWidget::on_slider_released() {
	if (!position_slider_) {
		user_scrubbing_ = false;
		resume_after_scrub_ = false;
		return;
	}

	const int value = position_slider_->value();
	if (player_) {
		player_->setPosition(static_cast<qint64>(value));
		if (resume_after_scrub_) {
			player_->play();
		}
	}

	user_scrubbing_ = false;
	resume_after_scrub_ = false;
}

void VideoPlayerWidget::on_volume_changed(int value) {
	const qreal vol = qBound<qreal>(0.0, static_cast<qreal>(value) / 100.0, 1.0);
	if (!audio_output_) {
		return;
	}
	if (prefetch_first_frame_) {
		prefetch_saved_volume_ = static_cast<float>(vol);
		audio_output_->setVolume(0.0f);
		return;
	}
	audio_output_->setVolume(static_cast<float>(vol));
}
