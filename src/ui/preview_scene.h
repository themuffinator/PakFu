#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>

#include "formats/bsp_preview.h"
#include "ui/preview_3d_options.h"

class QWidget;
class BspPreviewWidget;
class BspPreviewVulkanWidget;
class ModelViewerWidget;
class ModelViewerVulkanWidget;

struct BspPreviewScene {
	BspMesh mesh;
	QHash<QString, QImage> textures;

	[[nodiscard]] bool is_valid() const {
		return !mesh.vertices.isEmpty() && !mesh.indices.isEmpty();
	}

	void clear() {
		mesh = {};
		textures.clear();
	}
};

struct ModelPreviewScene {
	QString file_path;
	QString skin_path;

	[[nodiscard]] bool is_valid() const { return !file_path.isEmpty(); }
	void clear() {
		file_path.clear();
		skin_path.clear();
	}
};

struct ModelAnimationState {
	bool valid = false;
	int frame_count = 0;
	int current_frame = 0;
	bool playing = false;
	bool loop_enabled = true;
	float speed_multiplier = 1.0f;
	bool skeleton_enabled = false;
	bool has_native_animation = false;
	bool has_native_skeleton = false;
	int skeleton_joint_count = 0;
};

[[nodiscard]] BspPreviewWidget* as_gl_bsp_widget(QWidget* widget);
[[nodiscard]] BspPreviewVulkanWidget* as_vk_bsp_widget(QWidget* widget);
[[nodiscard]] ModelViewerWidget* as_gl_model_widget(QWidget* widget);
[[nodiscard]] ModelViewerVulkanWidget* as_vk_model_widget(QWidget* widget);

void apply_bsp_lightmap(QWidget* widget, bool enabled);
void apply_bsp_scene_to_widget(QWidget* widget, const BspPreviewScene& scene);
[[nodiscard]] bool load_model_scene_into_widget(QWidget* widget, const ModelPreviewScene& scene, QString* error = nullptr);

[[nodiscard]] PreviewCameraState camera_state_for_widget(QWidget* widget);
void apply_camera_state(QWidget* widget, const PreviewCameraState& state);

void apply_3d_visual_settings(QWidget* widget,
                              PreviewGridMode grid_mode,
                              PreviewBackgroundMode bg_mode,
                              const QColor& bg_color,
                              bool wireframe_enabled,
                              bool textured_enabled,
                              int fov_degrees,
                              bool glow_enabled);

[[nodiscard]] ModelAnimationState model_animation_state_for_widget(QWidget* widget);
void apply_model_animation_settings(QWidget* widget, const ModelAnimationState& state);
