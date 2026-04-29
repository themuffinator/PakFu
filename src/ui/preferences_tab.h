#pragma once

#include <QWidget>

#include "ui/theme_manager.h"
#include "ui/preview_renderer.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;

class PreferencesTab : public QWidget {
	Q_OBJECT

public:
	explicit PreferencesTab(QWidget* parent = nullptr);

signals:
	void theme_changed(AppTheme theme);
	void model_texture_smoothing_changed(bool enabled);
	void image_texture_smoothing_changed(bool enabled);
	void preview_fov_changed(int degrees);
	void pure_pak_protector_changed(bool enabled);
	void preview_renderer_changed(PreviewRenderer renderer);
	void preview_preferences_changed();
	void recent_file_limit_changed(int limit);

private:
	void build_ui();
	QWidget* build_appearance_tab();
	QWidget* build_archives_tab();
	QWidget* build_previews_tab();
	QWidget* build_images_text_tab();
	QWidget* build_updates_tab();
	void load_settings();
	void apply_theme_from_combo();
	void choose_image_background_color();
	void choose_3d_background_color();
	void update_color_buttons();
	void reset_appearance_settings();
	void reset_archive_settings();
	void reset_preview_settings();
	void reset_image_text_settings();
	void reset_update_settings();
	void notify_preview_preferences_changed();

	bool loading_ = false;
	QTabWidget* tabs_ = nullptr;
	QComboBox* theme_combo_ = nullptr;
	QComboBox* renderer_combo_ = nullptr;
	QLabel* renderer_status_ = nullptr;
	QComboBox* archive_open_action_combo_ = nullptr;
	QCheckBox* archive_open_always_ask_ = nullptr;
	QSpinBox* recent_file_limit_spin_ = nullptr;
	QCheckBox* model_texture_smoothing_ = nullptr;
	QSlider* preview_fov_slider_ = nullptr;
	QLabel* preview_fov_value_label_ = nullptr;
	QComboBox* three_d_grid_combo_ = nullptr;
	QComboBox* three_d_background_combo_ = nullptr;
	QPushButton* three_d_background_color_button_ = nullptr;
	QCheckBox* three_d_wireframe_ = nullptr;
	QCheckBox* three_d_textured_ = nullptr;
	QCheckBox* bsp_lightmapping_ = nullptr;
	QCheckBox* model_animation_playing_ = nullptr;
	QCheckBox* model_animation_loop_ = nullptr;
	QCheckBox* model_skeleton_overlay_ = nullptr;
	QComboBox* model_animation_speed_combo_ = nullptr;
	QCheckBox* image_texture_smoothing_ = nullptr;
	QComboBox* image_background_combo_ = nullptr;
	QPushButton* image_background_color_button_ = nullptr;
	QComboBox* image_layout_combo_ = nullptr;
	QCheckBox* image_reveal_transparency_ = nullptr;
	QCheckBox* text_word_wrap_ = nullptr;
	QCheckBox* pure_pak_protector_ = nullptr;
	QCheckBox* updates_auto_check_ = nullptr;
};
