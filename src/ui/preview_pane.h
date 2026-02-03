#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QVector>
#include <QWidget>

#include <memory>

#include "formats/image_loader.h"

class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QAudioOutput;
class QMediaPlayer;
class QSlider;
class QScrollBar;
class QToolButton;
class CinematicPlayerWidget;
class QSyntaxHighlighter;
class ModelViewerWidget;

class PreviewPane : public QWidget {
	Q_OBJECT

public:
	enum class TextSyntax {
		None,
		Cfg,
		Json,
		QuakeTxtBlocks,
		Quake3Menu,
		Quake3Shader,
	};

	explicit PreviewPane(QWidget* parent = nullptr);
	~PreviewPane() override;

	void set_model_texture_smoothing(bool enabled);
	void set_model_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette);

	void show_placeholder();
	void show_message(const QString& title, const QString& body);
	void show_text(const QString& title, const QString& subtitle, const QString& text);
	void show_txt(const QString& title, const QString& subtitle, const QString& text);
	void show_cfg(const QString& title, const QString& subtitle, const QString& text);
	void show_json(const QString& title, const QString& subtitle, const QString& text);
	void show_menu(const QString& title, const QString& subtitle, const QString& text);
	void show_shader(const QString& title, const QString& subtitle, const QString& text);
	void show_binary(const QString& title, const QString& subtitle, const QByteArray& bytes, bool truncated);
	void show_image_from_bytes(const QString& title, const QString& subtitle, const QByteArray& bytes, const ImageDecodeOptions& options = {});
	void show_image_from_file(const QString& title, const QString& subtitle, const QString& file_path, const ImageDecodeOptions& options = {});
	void show_audio_from_file(const QString& title, const QString& subtitle, const QString& file_path);
	void show_cinematic_from_file(const QString& title, const QString& subtitle, const QString& file_path);
	void show_model_from_file(const QString& title, const QString& subtitle, const QString& file_path, const QString& skin_path = {});
	void start_playback_from_beginning();

signals:
	void request_previous_audio();
	void request_next_audio();
	void request_previous_video();
	void request_next_video();

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void build_ui();
	void set_header(const QString& title, const QString& subtitle);
	void set_image_pixmap(const QPixmap& pixmap);
	void set_image_qimage(const QImage& image);
	void apply_image_background();
	void update_image_bg_button();
	void set_text_highlighter(TextSyntax syntax);
	void stop_audio_playback();
	void stop_cinematic_playback();
	void stop_model_preview();
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
	QToolButton* image_checkerboard_button_ = nullptr;
	QToolButton* image_bg_color_button_ = nullptr;
	QColor image_bg_color_;
	bool image_bg_checkerboard_ = true;
	QImage image_original_;
	QPixmap image_source_pixmap_;

	QWidget* text_page_ = nullptr;
	QPlainTextEdit* text_view_ = nullptr;
	std::unique_ptr<QSyntaxHighlighter> text_highlighter_;
	TextSyntax current_text_syntax_ = TextSyntax::None;

	QWidget* audio_page_ = nullptr;
	QMediaPlayer* audio_player_ = nullptr;
	QAudioOutput* audio_output_ = nullptr;
	QToolButton* audio_prev_button_ = nullptr;
	QToolButton* audio_play_button_ = nullptr;
	QToolButton* audio_next_button_ = nullptr;
	QSlider* audio_position_slider_ = nullptr;
	QScrollBar* audio_volume_scroll_ = nullptr;
	QToolButton* audio_info_button_ = nullptr;
	QString audio_file_path_;
	bool audio_user_scrubbing_ = false;

	QWidget* cinematic_page_ = nullptr;
	CinematicPlayerWidget* cinematic_widget_ = nullptr;

	QWidget* model_page_ = nullptr;
	ModelViewerWidget* model_widget_ = nullptr;
};
