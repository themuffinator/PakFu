#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QVector>
#include <QWidget>

#include <memory>

#include "formats/bsp_preview.h"
#include "formats/image_loader.h"
#include "formats/quake3_shader.h"
#include "ui/preview_3d_options.h"
#include "ui/preview_renderer.h"

class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QFormLayout;
class QFrame;
class QComboBox;
class QAudioOutput;
class QMediaPlayer;
class QSlider;
class QScrollBar;
class QToolButton;
class QTimer;
class CinematicPlayerWidget;
class VideoPlayerWidget;
class QSyntaxHighlighter;
class QTextOption;
class ShaderViewerWidget;

class PreviewPane : public QWidget {
	Q_OBJECT

public:
	enum class TextSyntax {
		None,
		C,
		Cfg,
		Json,
		QuakeTxtBlocks,
		Quake3Menu,
		Quake3Shader,
	};

	explicit PreviewPane(QWidget* parent = nullptr);
	~PreviewPane() override;

	void set_preview_renderer(PreviewRenderer renderer);
	void set_3d_fov_degrees(int degrees);
	void set_model_texture_smoothing(bool enabled);
	void set_image_texture_smoothing(bool enabled);
	void set_model_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette);
	void set_glow_enabled(bool enabled);
	void set_image_mip_controls(bool visible, int mip_level);
	[[nodiscard]] int image_mip_level() const { return image_mip_level_; }

	void set_current_file_info(const QString& pak_path, qint64 size, qint64 mtime_utc_secs);

	void show_placeholder();
	void show_message(const QString& title, const QString& body);
	void show_text(const QString& title, const QString& subtitle, const QString& text);
	void show_c(const QString& title, const QString& subtitle, const QString& text);
	void show_txt(const QString& title, const QString& subtitle, const QString& text);
	void show_cfg(const QString& title, const QString& subtitle, const QString& text);
	void show_json(const QString& title, const QString& subtitle, const QString& text);
	void show_menu(const QString& title, const QString& subtitle, const QString& text);
	void show_shader(const QString& title,
	                 const QString& subtitle,
	                 const QString& text,
	                 const Quake3ShaderDocument& document,
	                 QHash<QString, QImage> textures);
	void show_font_from_bytes(const QString& title, const QString& subtitle, const QByteArray& bytes);
	void show_binary(const QString& title, const QString& subtitle, const QByteArray& bytes, bool truncated);
	void show_image(const QString& title, const QString& subtitle, const QImage& image);
	void show_sprite(const QString& title,
	                 const QString& subtitle,
	                 const QVector<QImage>& frames,
	                 const QVector<int>& frame_durations_ms,
	                 const QString& details_text);
	void show_bsp(const QString& title, const QString& subtitle, BspMesh mesh, QHash<QString, QImage> textures = {});
	void show_image_from_bytes(const QString& title, const QString& subtitle, const QByteArray& bytes, const ImageDecodeOptions& options = {});
	void show_image_from_file(const QString& title, const QString& subtitle, const QString& file_path, const ImageDecodeOptions& options = {});
	void show_audio_from_file(const QString& title, const QString& subtitle, const QString& file_path);
	void show_cinematic_from_file(const QString& title, const QString& subtitle, const QString& file_path);
	void show_video_from_file(const QString& title, const QString& subtitle, const QString& file_path);
	void show_model_from_file(const QString& title, const QString& subtitle, const QString& file_path, const QString& skin_path = {});
	void start_playback_from_beginning();
	[[nodiscard]] bool is_shader_view_active() const;
	[[nodiscard]] QString selected_shader_blocks_text() const;

signals:
	void request_previous_audio();
	void request_next_audio();
	void request_previous_video();
	void request_next_video();
	void request_image_mip_level(int level);

protected:
	void resizeEvent(QResizeEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void build_ui();
	void set_header(const QString& title, const QString& subtitle);
	void set_image_pixmap(const QPixmap& pixmap);
	void set_image_qimage(const QImage& image);
	void apply_image_background();
	void apply_image_transparency_mode();
	void update_image_bg_button();
	void update_image_transparency_controls();
	void set_text_highlighter(TextSyntax syntax);
	void clear_overview_fields();
	void populate_basic_overview();
	void set_overview_value(const QString& label, const QString& value);
	void show_overview_block(bool show);
	void show_content_block(const QString& title, QWidget* page);
	void hide_content_block();
	void stop_sprite_animation();
	void apply_sprite_frame(int index);
	void schedule_next_sprite_frame();
	void update_audio_overview();
	void update_cinematic_overview();
	void update_video_overview();
	void stop_audio_playback();
	void stop_cinematic_playback();
	void stop_video_playback();
	void stop_model_preview();
	void rebuild_3d_widgets();
	void rebuild_bsp_widget();
	void rebuild_model_widget();
	void apply_3d_settings();
	void update_3d_bg_button();
	void apply_3d_bg_color_button_state();
	void update_3d_fullscreen_button();
	void toggle_3d_fullscreen();
	void enter_3d_fullscreen();
	void exit_3d_fullscreen();
	[[nodiscard]] QWidget* active_3d_widget() const;
	[[nodiscard]] QWidget* active_3d_page() const;
	void set_audio_source(const QString& file_path);
	void sync_audio_controls();
	void update_audio_tooltip();
	void update_audio_status_label();
	void clear_font_preview_font();
	void apply_text_wrap_mode();
	void update_shader_viewport_width();

	QLabel* title_label_ = nullptr;
	QLabel* subtitle_label_ = nullptr;
	QScrollArea* insights_scroll_ = nullptr;
	QWidget* insights_page_ = nullptr;
	QFrame* overview_card_ = nullptr;
	QFrame* image_card_ = nullptr;
	QFormLayout* overview_form_ = nullptr;
	QHash<QString, QLabel*> overview_values_;
	QFrame* content_card_ = nullptr;
	QLabel* content_title_label_ = nullptr;
	QStackedWidget* stack_ = nullptr;

	enum class ContentKind {
		None,
		Message,
		Text,
		Font,
		Audio,
		Cinematic,
		Video,
		Bsp,
		Model,
		Image,
		Sprite,
		Shader,
	};
	enum class ImageBackgroundMode {
		Transparent,
		Checkerboard,
		Solid,
	};
	enum class ImageLayoutMode {
		Fit,
		Tile,
	};
	ContentKind current_content_kind_ = ContentKind::None;
	QString current_pak_path_;
	qint64 current_file_size_ = -1;
	qint64 current_mtime_utc_secs_ = -1;

	QWidget* placeholder_page_ = nullptr;
	QLabel* placeholder_label_ = nullptr;

	QWidget* message_page_ = nullptr;
	QLabel* message_label_ = nullptr;

	QWidget* overview_image_container_ = nullptr;
	QScrollArea* image_scroll_ = nullptr;
	QLabel* image_label_ = nullptr;
	QComboBox* image_bg_mode_combo_ = nullptr;
	QComboBox* image_layout_combo_ = nullptr;
	QLabel* image_mip_label_ = nullptr;
	QComboBox* image_mip_combo_ = nullptr;
	QToolButton* image_bg_color_button_ = nullptr;
	QToolButton* image_reveal_transparency_button_ = nullptr;
	QColor image_bg_color_;
	ImageBackgroundMode image_bg_mode_ = ImageBackgroundMode::Checkerboard;
	ImageLayoutMode image_layout_mode_ = ImageLayoutMode::Fit;
	int image_mip_level_ = 0;
	bool image_reveal_transparency_ = false;
	bool image_texture_smoothing_ = false;
	QImage image_original_;
	QPixmap image_source_pixmap_;
	QVector<QImage> sprite_frames_;
	QVector<int> sprite_frame_durations_ms_;
	int sprite_frame_index_ = 0;
	QTimer* sprite_timer_ = nullptr;

	QWidget* text_page_ = nullptr;
	QPlainTextEdit* text_view_ = nullptr;
	QWidget* text_controls_ = nullptr;
	QToolButton* text_wrap_button_ = nullptr;
	std::unique_ptr<QSyntaxHighlighter> text_highlighter_;
	TextSyntax current_text_syntax_ = TextSyntax::None;
	bool text_word_wrap_enabled_ = false;

	QWidget* shader_page_ = nullptr;
	QScrollArea* shader_scroll_ = nullptr;
	ShaderViewerWidget* shader_widget_ = nullptr;

	QWidget* font_page_ = nullptr;
	QLabel* font_heading_label_ = nullptr;
	QLabel* font_subheading_label_ = nullptr;
	QLabel* font_notice_label_ = nullptr;
	QLabel* font_hero_label_ = nullptr;
	QLabel* font_pangram_label_ = nullptr;
	QLabel* font_upper_label_ = nullptr;
	QLabel* font_lower_label_ = nullptr;
	QLabel* font_digits_label_ = nullptr;
	QLabel* font_symbols_label_ = nullptr;
	QLabel* font_meta_label_ = nullptr;
	int font_preview_font_id_ = -1;

	QWidget* audio_page_ = nullptr;
	QMediaPlayer* audio_player_ = nullptr;
	QAudioOutput* audio_output_ = nullptr;
	QToolButton* audio_prev_button_ = nullptr;
	QToolButton* audio_play_button_ = nullptr;
	QToolButton* audio_stop_button_ = nullptr;
	QToolButton* audio_next_button_ = nullptr;
	QSlider* audio_position_slider_ = nullptr;
	QScrollBar* audio_volume_scroll_ = nullptr;
	QToolButton* audio_info_button_ = nullptr;
	QLabel* audio_status_label_ = nullptr;
	QString audio_file_path_;
	bool audio_user_scrubbing_ = false;

	QWidget* cinematic_page_ = nullptr;
	CinematicPlayerWidget* cinematic_widget_ = nullptr;

	QWidget* video_page_ = nullptr;
	VideoPlayerWidget* video_widget_ = nullptr;

	QWidget* bsp_page_ = nullptr;
	QWidget* bsp_widget_ = nullptr;
	QWidget* three_d_controls_ = nullptr;
	QComboBox* three_d_grid_combo_ = nullptr;
	QComboBox* three_d_bg_combo_ = nullptr;
	QToolButton* three_d_bg_color_button_ = nullptr;
	QToolButton* three_d_wireframe_button_ = nullptr;
	QToolButton* three_d_textured_button_ = nullptr;
	QToolButton* three_d_fullscreen_button_ = nullptr;
	QToolButton* bsp_lightmap_button_ = nullptr;
	bool bsp_lightmapping_enabled_ = true;
	PreviewGridMode three_d_grid_mode_ = PreviewGridMode::Floor;
	PreviewBackgroundMode three_d_bg_mode_ = PreviewBackgroundMode::Themed;
	QColor three_d_bg_color_;
	bool three_d_wireframe_enabled_ = false;
	bool three_d_textured_enabled_ = true;
	int three_d_fov_degrees_ = 100;
	bool glow_enabled_ = false;

	QWidget* model_page_ = nullptr;
	QWidget* model_widget_ = nullptr;
	QWidget* three_d_fullscreen_window_ = nullptr;
	QWidget* three_d_fullscreen_widget_ = nullptr;
	QWidget* three_d_fullscreen_page_ = nullptr;
	bool model_texture_smoothing_ = false;
	QVector<QRgb> model_palette_quake1_;
	QVector<QRgb> model_palette_quake2_;
	BspMesh cached_bsp_mesh_;
	QHash<QString, QImage> cached_bsp_textures_;
	bool has_cached_bsp_ = false;
	QString cached_model_file_path_;
	QString cached_model_skin_path_;
	bool has_cached_model_ = false;

	PreviewRenderer renderer_requested_ = PreviewRenderer::Vulkan;
	PreviewRenderer renderer_effective_ = PreviewRenderer::Vulkan;
};
