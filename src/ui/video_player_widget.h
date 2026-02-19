#pragma once

#include <QMediaMetaData>
#include <QSize>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QSlider;
class QScrollBar;
class QToolButton;
class QAudioOutput;
class QMediaPlayer;
class QVideoSink;
class QVideoWidget;
class QEnterEvent;
class QEvent;

class VideoPlayerWidget : public QWidget {
	Q_OBJECT

public:
	explicit VideoPlayerWidget(QWidget* parent = nullptr);
	~VideoPlayerWidget() override;

	[[nodiscard]] bool has_media() const;
	[[nodiscard]] QString file_path() const;
	[[nodiscard]] qint64 duration_ms() const;
	[[nodiscard]] QMediaMetaData meta_data() const;
	[[nodiscard]] QSize video_size() const;

	[[nodiscard]] bool load_file(const QString& file_path, QString* error = nullptr);
	void unload();
	void play_from_start();
	void set_texture_smoothing(bool enabled);

signals:
	void request_previous_media();
	void request_next_media();
	void media_info_changed();

protected:
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void build_ui();
	void unload_internal(bool notify_media_change);
	void update_ui_state();
	void set_status_text(const QString& text);
	void update_status_auto();
	void clear_status_override();
	void start_prefetch_first_frame();
	void stop_prefetch_first_frame();

	void play();
	void pause();
	void stop();
	void on_slider_pressed();
	void on_slider_released();
	void on_volume_changed(int value);

	QFrame* video_container_ = nullptr;
	QVideoWidget* video_widget_ = nullptr;
	QLabel* status_label_ = nullptr;
	QWidget* controls_container_ = nullptr;
	QToolButton* prev_button_ = nullptr;
	QToolButton* play_button_ = nullptr;
	QToolButton* next_button_ = nullptr;
	QToolButton* stop_button_ = nullptr;
	QSlider* position_slider_ = nullptr;
	QScrollBar* volume_scroll_ = nullptr;

	QMediaPlayer* player_ = nullptr;
	QAudioOutput* audio_output_ = nullptr;
	QVideoSink* video_sink_ = nullptr;

	QString file_path_;
	QSize current_video_size_;
	QString status_override_;
	bool user_scrubbing_ = false;
	bool resume_after_scrub_ = false;
	bool prefetch_first_frame_ = false;
	bool prefetch_stop_pending_ = false;
	float prefetch_saved_volume_ = 0.0f;
	bool texture_smoothing_ = false;
};
