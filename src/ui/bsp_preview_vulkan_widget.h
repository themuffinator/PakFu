#pragma once

#include <QtWidgets/qrhiwidget.h>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include "formats/bsp_preview.h"

class BspPreviewVulkanWidget final : public QRhiWidget {
	Q_OBJECT

public:
	explicit BspPreviewVulkanWidget(QWidget* parent = nullptr);
	~BspPreviewVulkanWidget() override;

	void set_mesh(BspMesh mesh, QHash<QString, QImage> textures);
	void set_lightmap_enabled(bool enabled);
	void clear();

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

private:
	struct GpuVertex {
		float px, py, pz;
		float nx, ny, nz;
		float r, g, b;
		float u, v;
	};

	struct DrawSurface {
		int first_index = 0;
		int index_count = 0;
		QString texture;
		bool uv_normalized = false;
		QVector2D tex_scale = QVector2D(1.0f, 1.0f);
		QVector2D tex_offset = QVector2D(0.0f, 0.0f);
		bool has_texture = false;
		QImage image;
		QRhiTexture* texture_handle = nullptr;
		QRhiShaderResourceBindings* srb = nullptr;
	};

	struct UniformBlock {
		QMatrix4x4 mvp;
		QMatrix4x4 model;
		QVector4D light_dir;
		QVector4D fill_dir;
		QVector4D ambient;
		QVector4D tex_scale_offset;
		QVector4D misc;
	};

	void reset_camera_from_mesh();
	void frame_mesh();
	void pan_by_pixels(const QPoint& delta);
	void upload_mesh(QRhiResourceUpdateBatch* updates);
	void upload_textures(QRhiResourceUpdateBatch* updates);
	void destroy_mesh_resources();
	void destroy_pipeline_resources();
	void ensure_pipeline();
	void ensure_uniform_buffer(int surface_count);

	enum class DragMode {
		None,
		Orbit,
		Pan,
	};

	BspMesh mesh_;
	bool has_mesh_ = false;
	bool pending_upload_ = false;
	bool pending_texture_upload_ = false;
	bool lightmap_enabled_ = true;

	QHash<QString, QImage> textures_;
	QVector<DrawSurface> surfaces_;

	QVector3D center_ = QVector3D(0, 0, 0);
	float radius_ = 1.0f;
	float yaw_deg_ = 45.0f;
	float pitch_deg_ = 20.0f;
	float distance_ = 3.0f;

	QPoint last_mouse_pos_;
	DragMode drag_mode_ = DragMode::None;
	Qt::MouseButton drag_button_ = Qt::NoButton;

	QShader vert_shader_;
	QShader frag_shader_;

	QRhiBuffer* vbuf_ = nullptr;
	QRhiBuffer* ibuf_ = nullptr;
	QRhiBuffer* ubuf_ = nullptr;
	quint32 ubuf_stride_ = 0;

	QRhiSampler* sampler_ = nullptr;
	QRhiTexture* white_tex_ = nullptr;
	QRhiShaderResourceBindings* default_srb_ = nullptr;
	QRhiGraphicsPipeline* pipeline_ = nullptr;

	bool pipeline_dirty_ = true;
	bool uniform_dirty_ = true;
};
