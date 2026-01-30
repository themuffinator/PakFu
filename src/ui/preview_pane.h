#pragma once

#include <QByteArray>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QAudioOutput;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QToolButton;

class PreviewPane : public QWidget {
	Q_OBJECT

public:
	explicit PreviewPane(QWidget* parent = nullptr);

	void show_placeholder();
	void show_message(const QString& title, const QString& body);
	void show_text(const QString& title, const QString& subtitle, const QString& text);
	void show_binary(const QString& title, const QString& subtitle, const QByteArray& bytes, bool truncated);
	void show_image_from_bytes(const QString& title, const QString& subtitle, const QByteArray& bytes);
	void show_image_from_file(const QString& title, const QString& subtitle, const QString& file_path);
	void show_audio_from_file(const QString& title, const QString& subtitle, const QString& file_path);

signals:
	void request_previous_audio();
	void request_next_audio();

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void build_ui();
	void set_header(const QString& title, const QString& subtitle);
	void set_image_pixmap(const QPixmap& pixmap);
	void stop_audio_playback();
	void set_audio_source(const QString& file_path);
	void sync_audio_controls();
	void update_audio_tooltip();

	QLabel* title_label_ = nullptr;
	QLabel* subtitle_label_ = nullptr;
	QStackedWidget* stack_ = nullptr;

	QWidget* placeholder_page_ = nullptr;
	QLabel* placeholder_label_ = nullptr;

	QWidget* message_page_ = nullptr;
	QLabel* message_label_ = nullptr;

	QWidget* image_page_ = nullptr;
	QScrollArea* image_scroll_ = nullptr;
	QLabel* image_label_ = nullptr;
	QPixmap original_pixmap_;

	QWidget* text_page_ = nullptr;
	QPlainTextEdit* text_view_ = nullptr;

	QWidget* audio_page_ = nullptr;
	QMediaPlayer* audio_player_ = nullptr;
	QAudioOutput* audio_output_ = nullptr;
	QPushButton* audio_prev_button_ = nullptr;
	QPushButton* audio_play_button_ = nullptr;
	QPushButton* audio_next_button_ = nullptr;
	QSlider* audio_position_slider_ = nullptr;
	QSlider* audio_volume_slider_ = nullptr;
	QToolButton* audio_info_button_ = nullptr;
	QString audio_file_path_;
	bool audio_user_scrubbing_ = false;
};
