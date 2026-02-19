#pragma once

#include <QtWidgets/qrhiwidget.h>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QPoint>
#include <QTimer>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

#include <cstdint>
#include <optional>

#include <rhi/qshader.h>

#include "formats/model.h"
#include "ui/preview_3d_options.h"

class QKeyEvent;
class QFocusEvent;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

class ModelViewerVulkanWidget final : public QRhiWidget {
	Q_OBJECT

public:
	explicit ModelViewerVulkanWidget(QWidget* parent = nullptr);
	~ModelViewerVulkanWidget() override;

	[[nodiscard]] bool has_model() const { return model_.has_value(); }
	[[nodiscard]] QString model_format() const { return model_ ? model_->format : QString(); }
	[[nodiscard]] ModelMesh mesh() const { return model_ ? model_->mesh : ModelMesh{}; }

	void set_texture_smoothing(bool enabled);
	void set_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette);
	void set_grid_mode(PreviewGridMode mode);
	void set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color);
	void set_wireframe_enabled(bool enabled);
	void set_textured_enabled(bool enabled);
	void set_glow_enabled(bool enabled);
	void set_fov_degrees(int degrees);
	[[nodiscard]] PreviewCameraState camera_state() const;
	void set_camera_state(const PreviewCameraState& state);

	[[nodiscard]] bool load_file(const QString& file_path, QString* error = nullptr);
	[[nodiscard]] bool load_file(const QString& file_path, const QString& skin_path, QString* error);
	void unload();

protected:
	void initialize(QRhiCommandBuffer* cb) override;
	void render(QRhiCommandBuffer* cb) override;
	void releaseResources() override;

	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	struct GpuVertex {
		float px, py, pz;
		float nx, ny, nz;
		float u, v;
	};

	struct GridLineVertex {
		float px, py, pz;
		float r, g, b, a;
	};

	struct DrawSurface {
		int first_index = 0;
		int index_count = 0;
		QString name;
		QString shader_hint;
		QString shader_leaf;
		QImage image;
		QImage glow_image;
		QRhiTexture* texture_handle = nullptr;
		QRhiTexture* glow_texture_handle = nullptr;
		QRhiShaderResourceBindings* srb = nullptr;
		bool has_texture = false;
		bool has_glow = false;
	};

	struct UniformBlock {
		QMatrix4x4 mvp;
		QMatrix4x4 model;
		QVector4D cam_pos;
		QVector4D light_dir;
		QVector4D fill_dir;
		QVector4D base_color;
		QVector4D ground_color;
		QVector4D shadow_center;
		QVector4D shadow_params;
		QVector4D grid_params;
		QVector4D grid_color;
		QVector4D axis_color_x;
		QVector4D axis_color_y;
		QVector4D bg_top;
		QVector4D bg_bottom;
		QVector4D misc;
	};

	void reset_camera_from_mesh();
	void frame_mesh();
	void pan_by_pixels(const QPoint& delta);
	void dolly_by_pixels(const QPoint& delta);
	void on_fly_tick();
	void set_fly_key(int key, bool down);
	void update_grid_lines_if_needed(QRhiResourceUpdateBatch* updates, const QVector3D& cam_pos, float aspect);
	void upload_mesh(QRhiResourceUpdateBatch* updates);
	void upload_textures(QRhiResourceUpdateBatch* updates);
	void update_ground_mesh_if_needed(QRhiResourceUpdateBatch* updates);
	void update_background_mesh_if_needed(QRhiResourceUpdateBatch* updates);
	void destroy_mesh_resources();
	void destroy_pipeline_resources();
	void ensure_pipeline();
	void ensure_uniform_buffer(int draw_count);
	void ensure_default_srb(QRhiResourceUpdateBatch* updates = nullptr);
	void rebuild_sampler();
	void update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const;
	void update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const;
	void update_grid_settings();

	enum class DragMode {
		None,
		Orbit,
		Pan,
		Dolly,
		Look,
	};

	std::optional<LoadedModel> model_;
	QString last_model_path_;
	QString last_skin_path_;
	bool pending_upload_ = false;
	bool pending_texture_upload_ = false;
	bool pending_sampler_update_ = false;
	bool pending_ground_upload_ = false;
	bool pending_background_upload_ = false;

	QVector<DrawSurface> surfaces_;
	QImage skin_image_;
	QImage skin_glow_image_;
	QRhiTexture* skin_texture_ = nullptr;
	QRhiTexture* skin_glow_texture_ = nullptr;
	QRhiShaderResourceBindings* skin_srb_ = nullptr;
	bool has_texture_ = false;
	bool has_glow_ = false;

	QVector<QRgb> quake1_palette_;
	QVector<QRgb> quake2_palette_;
	bool texture_smoothing_ = false;
	bool textured_enabled_ = true;
	bool wireframe_enabled_ = false;
	bool glow_enabled_ = false;
	PreviewGridMode grid_mode_ = PreviewGridMode::Floor;
	PreviewBackgroundMode bg_mode_ = PreviewBackgroundMode::Themed;
	QColor bg_custom_color_;

	QVector3D center_ = QVector3D(0, 0, 0);
	float radius_ = 1.0f;
	float yaw_deg_ = 45.0f;
	float pitch_deg_ = 20.0f;
	float distance_ = 3.0f;
	float fov_y_deg_ = 100.0f;

	QPoint last_mouse_pos_;
	DragMode drag_mode_ = DragMode::None;
	Qt::MouseButtons drag_buttons_ = Qt::NoButton;

	QTimer fly_timer_;
	QElapsedTimer fly_elapsed_;
	qint64 fly_last_nsecs_ = 0;
	float fly_speed_ = 640.0f;
	int fly_move_mask_ = 0;

	QShader vert_shader_;
	QShader frag_shader_;
	QShader grid_vert_shader_;
	QShader grid_frag_shader_;

	QRhiBuffer* vbuf_ = nullptr;
	QRhiBuffer* ibuf_ = nullptr;
	QRhiBuffer* ground_vbuf_ = nullptr;
	QRhiBuffer* ground_ibuf_ = nullptr;
	QRhiBuffer* bg_vbuf_ = nullptr;
	QRhiBuffer* grid_vbuf_ = nullptr;
	QRhiBuffer* ubuf_ = nullptr;
	quint32 ubuf_stride_ = 0;
	int index_count_ = 0;
	int ground_index_count_ = 0;
	float ground_extent_ = 0.0f;
	float ground_z_ = 0.0f;
	float grid_scale_ = 1.0f;
	int grid_vertex_count_ = 0;
	float grid_line_step_ = 0.0f;
	int grid_line_center_i_ = 0;
	int grid_line_center_j_ = 0;
	int grid_line_half_lines_ = 0;
	QVector3D grid_line_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	QVector3D axis_x_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	QVector3D axis_y_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);

	QVector<GpuVertex> ground_vertices_;
	QVector<std::uint16_t> ground_indices_;
	QVector<GpuVertex> bg_vertices_;

	QRhiSampler* sampler_ = nullptr;
	QRhiTexture* white_tex_ = nullptr;
	QRhiTexture* black_tex_ = nullptr;
	QRhiShaderResourceBindings* default_srb_ = nullptr;
	QRhiShaderResourceBindings* ground_srb_ = nullptr;
	QRhiGraphicsPipeline* pipeline_ = nullptr;
	QRhiShaderResourceBindings* grid_srb_ = nullptr;
	QRhiGraphicsPipeline* grid_pipeline_ = nullptr;

	bool pipeline_dirty_ = true;
};
