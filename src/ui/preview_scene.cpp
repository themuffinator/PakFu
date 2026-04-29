#include "ui/preview_scene.h"

#include <algorithm>

#include "ui/bsp_preview_vulkan_widget.h"
#include "ui/bsp_preview_widget.h"
#include "ui/model_viewer_vulkan_widget.h"
#include "ui/model_viewer_widget.h"

BspPreviewWidget* as_gl_bsp_widget(QWidget* widget) {
	return dynamic_cast<BspPreviewWidget*>(widget);
}

BspPreviewVulkanWidget* as_vk_bsp_widget(QWidget* widget) {
	return dynamic_cast<BspPreviewVulkanWidget*>(widget);
}

ModelViewerWidget* as_gl_model_widget(QWidget* widget) {
	return dynamic_cast<ModelViewerWidget*>(widget);
}

ModelViewerVulkanWidget* as_vk_model_widget(QWidget* widget) {
	return dynamic_cast<ModelViewerVulkanWidget*>(widget);
}

void apply_bsp_lightmap(QWidget* widget, bool enabled) {
	if (auto* gl = as_gl_bsp_widget(widget)) {
		gl->set_lightmap_enabled(enabled);
		return;
	}
	if (auto* vk = as_vk_bsp_widget(widget)) {
		vk->set_lightmap_enabled(enabled);
	}
}

void apply_bsp_scene_to_widget(QWidget* widget, const BspPreviewScene& scene) {
	if (!widget) {
		return;
	}
	if (auto* gl = as_gl_bsp_widget(widget)) {
		gl->set_mesh(scene.mesh, scene.textures);
		return;
	}
	if (auto* vk = as_vk_bsp_widget(widget)) {
		vk->set_mesh(scene.mesh, scene.textures);
	}
}

bool load_model_scene_into_widget(QWidget* widget, const ModelPreviewScene& scene, QString* error) {
	if (error) {
		error->clear();
	}
	if (!widget || !scene.is_valid()) {
		if (error) {
			*error = "No model scene is available.";
		}
		return false;
	}
	if (auto* gl = as_gl_model_widget(widget)) {
		return scene.skin_path.isEmpty() ? gl->load_file(scene.file_path, error)
		                                 : gl->load_file(scene.file_path, scene.skin_path, error);
	}
	if (auto* vk = as_vk_model_widget(widget)) {
		return scene.skin_path.isEmpty() ? vk->load_file(scene.file_path, error)
		                                 : vk->load_file(scene.file_path, scene.skin_path, error);
	}
	if (error) {
		*error = "Model preview widget is not available.";
	}
	return false;
}

PreviewCameraState camera_state_for_widget(QWidget* widget) {
	if (!widget) {
		return {};
	}
	if (auto* gl = as_gl_bsp_widget(widget)) {
		return gl->camera_state();
	}
	if (auto* vk = as_vk_bsp_widget(widget)) {
		return vk->camera_state();
	}
	if (auto* gl = as_gl_model_widget(widget)) {
		return gl->camera_state();
	}
	if (auto* vk = as_vk_model_widget(widget)) {
		return vk->camera_state();
	}
	return {};
}

void apply_camera_state(QWidget* widget, const PreviewCameraState& state) {
	if (!widget || !state.valid) {
		return;
	}
	if (auto* gl = as_gl_bsp_widget(widget)) {
		gl->set_camera_state(state);
		return;
	}
	if (auto* vk = as_vk_bsp_widget(widget)) {
		vk->set_camera_state(state);
		return;
	}
	if (auto* gl = as_gl_model_widget(widget)) {
		gl->set_camera_state(state);
		return;
	}
	if (auto* vk = as_vk_model_widget(widget)) {
		vk->set_camera_state(state);
	}
}

void apply_3d_visual_settings(QWidget* widget,
                              PreviewGridMode grid_mode,
                              PreviewBackgroundMode bg_mode,
                              const QColor& bg_color,
                              bool wireframe_enabled,
                              bool textured_enabled,
                              int fov_degrees,
                              bool glow_enabled) {
	if (!widget) {
		return;
	}
	const int fov = std::clamp(fov_degrees, 40, 120);
	if (auto* gl = as_gl_bsp_widget(widget)) {
		gl->set_grid_mode(grid_mode);
		gl->set_background_mode(bg_mode, bg_color);
		gl->set_wireframe_enabled(wireframe_enabled);
		gl->set_textured_enabled(textured_enabled);
		gl->set_fov_degrees(fov);
		return;
	}
	if (auto* vk = as_vk_bsp_widget(widget)) {
		vk->set_grid_mode(grid_mode);
		vk->set_background_mode(bg_mode, bg_color);
		vk->set_wireframe_enabled(wireframe_enabled);
		vk->set_textured_enabled(textured_enabled);
		vk->set_fov_degrees(fov);
		return;
	}
	if (auto* gl = as_gl_model_widget(widget)) {
		gl->set_grid_mode(grid_mode);
		gl->set_background_mode(bg_mode, bg_color);
		gl->set_wireframe_enabled(wireframe_enabled);
		gl->set_textured_enabled(textured_enabled);
		gl->set_fov_degrees(fov);
		gl->set_glow_enabled(glow_enabled);
		return;
	}
	if (auto* vk = as_vk_model_widget(widget)) {
		vk->set_grid_mode(grid_mode);
		vk->set_background_mode(bg_mode, bg_color);
		vk->set_wireframe_enabled(wireframe_enabled);
		vk->set_textured_enabled(textured_enabled);
		vk->set_fov_degrees(fov);
		vk->set_glow_enabled(glow_enabled);
	}
}

ModelAnimationState model_animation_state_for_widget(QWidget* widget) {
	ModelAnimationState st;
	if (auto* gl = as_gl_model_widget(widget)) {
		st.valid = gl->has_model();
		st.frame_count = gl->animation_frame_count();
		st.current_frame = gl->animation_current_frame();
		st.playing = gl->animation_playing();
		st.loop_enabled = gl->animation_loop_enabled();
		st.speed_multiplier = gl->animation_speed_multiplier();
		st.skeleton_enabled = gl->skeleton_overlay_enabled();
		st.has_native_animation = gl->has_native_animation();
		st.has_native_skeleton = gl->has_native_skeleton();
		st.skeleton_joint_count = gl->skeleton_joint_count();
		return st;
	}
	if (auto* vk = as_vk_model_widget(widget)) {
		st.valid = vk->has_model();
		st.frame_count = vk->animation_frame_count();
		st.current_frame = vk->animation_current_frame();
		st.playing = vk->animation_playing();
		st.loop_enabled = vk->animation_loop_enabled();
		st.speed_multiplier = vk->animation_speed_multiplier();
		st.skeleton_enabled = vk->skeleton_overlay_enabled();
		st.has_native_animation = vk->has_native_animation();
		st.has_native_skeleton = vk->has_native_skeleton();
		st.skeleton_joint_count = vk->skeleton_joint_count();
		return st;
	}
	return st;
}

void apply_model_animation_settings(QWidget* widget, const ModelAnimationState& state) {
	if (!widget) {
		return;
	}
	const int clamped_frame = std::max(0, state.current_frame);
	const float speed = std::clamp(state.speed_multiplier, 0.1f, 8.0f);
	if (auto* gl = as_gl_model_widget(widget)) {
		gl->set_animation_loop_enabled(state.loop_enabled);
		gl->set_animation_speed_multiplier(speed);
		gl->set_skeleton_overlay_enabled(state.skeleton_enabled);
		gl->set_animation_frame(clamped_frame);
		gl->set_animation_playing(state.playing);
		return;
	}
	if (auto* vk = as_vk_model_widget(widget)) {
		vk->set_animation_loop_enabled(state.loop_enabled);
		vk->set_animation_speed_multiplier(speed);
		vk->set_skeleton_overlay_enabled(state.skeleton_enabled);
		vk->set_animation_frame(clamped_frame);
		vk->set_animation_playing(state.playing);
	}
}
