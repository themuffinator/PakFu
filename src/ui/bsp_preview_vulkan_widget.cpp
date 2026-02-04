#include "ui/bsp_preview_vulkan_widget.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDebug>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QWheelEvent>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

namespace {
QVector3D spherical_dir(float yaw_deg, float pitch_deg) {
	constexpr float kPi = 3.14159265358979323846f;
	const float yaw = yaw_deg * kPi / 180.0f;
	const float pitch = pitch_deg * kPi / 180.0f;
	const float cy = std::cos(yaw);
	const float sy = std::sin(yaw);
	const float cp = std::cos(pitch);
	const float sp = std::sin(pitch);
	return QVector3D(cp * cy, cp * sy, sp);
}

QShader load_shader(const QString& path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		qWarning() << "BspPreviewVulkanWidget: unable to open shader" << path;
		return {};
	}
	const QByteArray data = f.readAll();
	QShader shader = QShader::fromSerialized(data);
	if (!shader.isValid()) {
		qWarning() << "BspPreviewVulkanWidget: invalid shader" << path;
	}
	return shader;
}

quint32 aligned_uniform_stride(QRhi* rhi, quint32 size) {
	const quint32 align = rhi ? rhi->ubufAlignment() : 256u;
	return (size + align - 1u) & ~(align - 1u);
}
}  // namespace

BspPreviewVulkanWidget::BspPreviewVulkanWidget(QWidget* parent) : QRhiWidget(parent) {
	setApi(QRhiWidget::Api::Vulkan);
	setMinimumHeight(240);
	setFocusPolicy(Qt::StrongFocus);
	setToolTip(
		"3D Controls:\n"
		"- Orbit: Left-drag\n"
		"- Pan: Shift+Left-drag / Middle-drag / Right-drag\n"
		"- Zoom: Mouse wheel\n"
		"- Frame: F\n"
		"- Reset: R");
}

BspPreviewVulkanWidget::~BspPreviewVulkanWidget() {
	releaseResources();
}

void BspPreviewVulkanWidget::set_mesh(BspMesh mesh, QHash<QString, QImage> textures) {
	mesh_ = std::move(mesh);
	has_mesh_ = !mesh_.vertices.isEmpty() && !mesh_.indices.isEmpty();

	textures_.clear();
	if (!textures.isEmpty()) {
		textures_.reserve(textures.size());
		for (auto it = textures.begin(); it != textures.end(); ++it) {
			textures_.insert(it.key().toLower(), it.value());
		}
	}

	surfaces_.clear();
	surfaces_.reserve(mesh_.surfaces.size());
	for (const BspMeshSurface& s : mesh_.surfaces) {
		DrawSurface ds;
		ds.first_index = s.first_index;
		ds.index_count = s.index_count;
		ds.texture = s.texture;
		ds.uv_normalized = s.uv_normalized;
		surfaces_.push_back(std::move(ds));
	}

	pending_upload_ = has_mesh_;
	pending_texture_upload_ = has_mesh_;
	reset_camera_from_mesh();
	update();
}

void BspPreviewVulkanWidget::set_lightmap_enabled(bool enabled) {
	if (lightmap_enabled_ == enabled) {
		return;
	}
	lightmap_enabled_ = enabled;
	uniform_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::set_grid_mode(PreviewGridMode mode) {
	if (grid_mode_ == mode) {
		return;
	}
	grid_mode_ = mode;
	pending_ground_upload_ = true;
	uniform_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color) {
	if (bg_mode_ == mode && bg_custom_color_ == custom_color) {
		return;
	}
	bg_mode_ = mode;
	bg_custom_color_ = custom_color;
	uniform_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::set_wireframe_enabled(bool enabled) {
	if (wireframe_enabled_ == enabled) {
		return;
	}
	wireframe_enabled_ = enabled;
	pipeline_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::set_textured_enabled(bool enabled) {
	if (textured_enabled_ == enabled) {
		return;
	}
	textured_enabled_ = enabled;
	uniform_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::clear() {
	has_mesh_ = false;
	pending_upload_ = false;
	pending_texture_upload_ = false;
	textures_.clear();
	surfaces_.clear();
	mesh_ = BspMesh{};
	destroy_mesh_resources();
	update();
}

void BspPreviewVulkanWidget::initialize(QRhiCommandBuffer*) {
	vert_shader_ = load_shader(":/assets/shaders/bsp_preview.vert.qsb");
	frag_shader_ = load_shader(":/assets/shaders/bsp_preview.frag.qsb");

	if (rhi()) {
		sampler_ = rhi()->newSampler(QRhiSampler::Linear,
									  QRhiSampler::Linear,
									  QRhiSampler::None,
									  QRhiSampler::Repeat,
									  QRhiSampler::Repeat);
		sampler_->create();

		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
	}

	ensure_pipeline();
}

void BspPreviewVulkanWidget::render(QRhiCommandBuffer* cb) {
	if (!cb || !rhi()) {
		return;
	}

	const QRhiDepthStencilClearValue ds_clear = {1.0f, 0};
	QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();

	if (pending_upload_) {
		upload_mesh(updates);
		pending_upload_ = false;
	}
	if (pending_texture_upload_) {
		upload_textures(updates);
		pending_texture_upload_ = false;
	}
	if (has_mesh_) {
		update_ground_mesh_if_needed(updates);
	}
	update_background_mesh_if_needed(updates);

	if (pipeline_dirty_) {
		ensure_pipeline();
	}

	cb->beginPass(renderTarget(), QColor(0, 0, 0), ds_clear, updates);

	if (!pipeline_ || !bg_vbuf_) {
		cb->endPass();
		return;
	}

	const bool draw_ground = (grid_mode_ != PreviewGridMode::None && ground_index_count_ > 0 && ground_vbuf_ && ground_ibuf_);
	const bool draw_surfaces = (has_mesh_ && index_count_ > 0 && vbuf_ && ibuf_);
	const int surface_count = draw_surfaces ? (surfaces_.isEmpty() ? 1 : surfaces_.size()) : 0;
	const int draw_count = 1 + (draw_ground ? 1 : 0) + surface_count;
	ensure_uniform_buffer(draw_count);

	const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
	const float near_plane = std::max(radius_ * 0.01f, 0.01f);
	const float far_plane = std::max(radius_ * 200.0f, near_plane + 10.0f);

	QMatrix4x4 proj;
	proj.perspective(45.0f, aspect, near_plane, far_plane);

	const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
	const QVector3D cam_pos = center_ + dir * distance_;

	QMatrix4x4 view;
	view.lookAt(cam_pos, center_, QVector3D(0, 0, 1));

	QMatrix4x4 model;
	model.setToIdentity();

	const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * proj * view * model;
	const QMatrix4x4 bg_mvp = rhi()->clipSpaceCorrMatrix();

	QVector3D bg_top;
	QVector3D bg_bottom;
	QVector3D bg_base;
	update_background_colors(&bg_top, &bg_bottom, &bg_base);

	QVector3D grid_color;
	QVector3D axis_x;
	QVector3D axis_y;
	update_grid_colors(&grid_color, &axis_x, &axis_y);
	update_grid_settings();

	QByteArray udata;
	udata.resize(static_cast<int>(ubuf_stride_ * draw_count));
	udata.fill(0);

	auto write_uniform = [&](int i, const QVector2D& tex_scale, const QVector2D& tex_offset, bool has_tex, bool is_ground, bool is_background) {
		UniformBlock u;
		u.mvp = is_background ? bg_mvp : mvp;
		u.model = model;
		u.light_dir = QVector4D(-0.35f, -0.6f, 0.75f, 0.0f);
		u.fill_dir = QVector4D(0.75f, 0.2f, 0.45f, 0.0f);
		u.ambient = QVector4D(0.35f, 0.35f, 0.35f, 0.0f);
		u.tex_scale_offset = QVector4D(tex_scale.x(), tex_scale.y(), tex_offset.x(), tex_offset.y());
		u.ground_color = QVector4D(bg_base, 0.0f);
		u.shadow_center = QVector4D(center_.x(), center_.y(), ground_z_, 0.0f);
		u.shadow_params = QVector4D(std::max(0.05f, radius_ * 1.45f), 0.55f, 2.4f, is_ground ? 1.0f : 0.0f);
		u.grid_params = QVector4D(is_ground ? (grid_mode_ == PreviewGridMode::Grid ? 1.0f : 0.0f) : 0.0f, grid_scale_, 0.0f, 0.0f);
		u.grid_color = QVector4D(grid_color, 0.0f);
		u.axis_color_x = QVector4D(axis_x, 0.0f);
		u.axis_color_y = QVector4D(axis_y, 0.0f);
		u.bg_top = QVector4D(bg_top, 0.0f);
		u.bg_bottom = QVector4D(bg_bottom, 0.0f);
		u.misc = QVector4D(lightmap_enabled_ ? 1.0f : 0.0f, has_tex ? 1.0f : 0.0f, is_background ? 1.0f : 0.0f, 0.0f);
		std::memcpy(udata.data() + static_cast<int>(i * ubuf_stride_), &u, sizeof(UniformBlock));
	};

	int uidx = 0;
	write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, false, true);
	if (draw_ground) {
		write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, true, false);
	}
	if (draw_surfaces) {
		if (surfaces_.isEmpty()) {
			write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, false, false);
		} else {
			for (int i = 0; i < surfaces_.size(); ++i) {
				const DrawSurface& s = surfaces_[i];
				const bool use_tex = textured_enabled_ && s.has_texture;
				write_uniform(uidx++, s.tex_scale, s.tex_offset, use_tex, false, false);
			}
		}
	}

	updates = rhi()->nextResourceUpdateBatch();
	updates->updateDynamicBuffer(ubuf_, 0, udata.size(), udata.constData());
	cb->resourceUpdate(updates);

	cb->setGraphicsPipeline(pipeline_);
	cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));

	{
		const QRhiCommandBuffer::VertexInput bindings[] = {
			{bg_vbuf_, 0},
		};
		cb->setVertexInput(0, 1, bindings);
		QRhiCommandBuffer::DynamicOffset dyn = {0, 0};
		cb->setShaderResources(default_srb_, 1, &dyn);
		cb->draw(6);
	}

	if (!draw_surfaces) {
		cb->endPass();
		return;
	}

	if (draw_ground) {
		const QRhiCommandBuffer::VertexInput bindings[] = {
			{ground_vbuf_, 0},
		};
		const quint32 offset = ubuf_stride_ * 1u;
		QRhiCommandBuffer::DynamicOffset dyn = {0, offset};
		cb->setVertexInput(0, 1, bindings, ground_ibuf_, 0, QRhiCommandBuffer::IndexUInt16);
		cb->setShaderResources(default_srb_, 1, &dyn);
		cb->drawIndexed(ground_index_count_, 1, 0, 0, 0);
	}

	const QRhiCommandBuffer::VertexInput bindings[] = {
		{vbuf_, 0},
	};
	cb->setVertexInput(0, 1, bindings, ibuf_, 0, QRhiCommandBuffer::IndexUInt32);

	const int base_offset = 1 + (draw_ground ? 1 : 0);
	if (surfaces_.isEmpty()) {
		const quint32 offset = ubuf_stride_ * static_cast<quint32>(base_offset);
		QRhiCommandBuffer::DynamicOffset dyn = {0, offset};
		cb->setShaderResources(default_srb_, 1, &dyn);
		cb->drawIndexed(index_count_, 1, 0, 0, 0);
	} else {
		for (int i = 0; i < surfaces_.size(); ++i) {
			const DrawSurface& s = surfaces_[i];
			const quint32 offset = ubuf_stride_ * static_cast<quint32>(base_offset + i);
			QRhiCommandBuffer::DynamicOffset dyn = {0, offset};
			QRhiShaderResourceBindings* srb = s.srb ? s.srb : default_srb_;
			cb->setShaderResources(srb, 1, &dyn);
			cb->drawIndexed(s.index_count, 1, s.first_index, 0, 0);
		}
	}

	cb->endPass();
}

void BspPreviewVulkanWidget::releaseResources() {
	destroy_mesh_resources();
	destroy_pipeline_resources();
	vert_shader_ = {};
	frag_shader_ = {};
}

void BspPreviewVulkanWidget::resizeEvent(QResizeEvent* event) {
	QRhiWidget::resizeEvent(event);
	pipeline_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::mousePressEvent(QMouseEvent* event) {
	if (!event) {
		QRhiWidget::mousePressEvent(event);
		return;
	}

	if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
		setFocus(Qt::MouseFocusReason);
		last_mouse_pos_ = event->pos();
		drag_button_ = event->button();

		if (event->button() == Qt::LeftButton) {
			drag_mode_ = (event->modifiers() & Qt::ShiftModifier) ? DragMode::Pan : DragMode::Orbit;
		} else {
			drag_mode_ = DragMode::Pan;
		}
		event->accept();
		return;
	}

	QRhiWidget::mousePressEvent(event);
}

void BspPreviewVulkanWidget::mouseMoveEvent(QMouseEvent* event) {
	if (!event) {
		QRhiWidget::mouseMoveEvent(event);
		return;
	}

	if (drag_mode_ == DragMode::None) {
		QRhiWidget::mouseMoveEvent(event);
		return;
	}

	const QPoint delta = event->pos() - last_mouse_pos_;
	last_mouse_pos_ = event->pos();

	if (drag_mode_ == DragMode::Orbit) {
		yaw_deg_ += delta.x() * 0.4f;
		pitch_deg_ = std::clamp(pitch_deg_ - delta.y() * 0.4f, -89.0f, 89.0f);
		update();
		event->accept();
		return;
	}

	if (drag_mode_ == DragMode::Pan) {
		pan_by_pixels(delta);
		update();
		event->accept();
		return;
	}

	QRhiWidget::mouseMoveEvent(event);
}

void BspPreviewVulkanWidget::mouseReleaseEvent(QMouseEvent* event) {
	if (!event) {
		QRhiWidget::mouseReleaseEvent(event);
		return;
	}

	if (drag_mode_ != DragMode::None && event->button() == drag_button_) {
		drag_mode_ = DragMode::None;
		drag_button_ = Qt::NoButton;
		event->accept();
		return;
	}

	QRhiWidget::mouseReleaseEvent(event);
}

void BspPreviewVulkanWidget::wheelEvent(QWheelEvent* event) {
	if (!event) {
		QRhiWidget::wheelEvent(event);
		return;
	}

	const QPoint num_deg = event->angleDelta() / 8;
	if (!num_deg.isNull()) {
		distance_ *= std::pow(0.92f, num_deg.y() / 15.0f);
		distance_ = std::clamp(distance_, radius_ * 0.2f, radius_ * 20.0f);
		update();
		event->accept();
		return;
	}

	QRhiWidget::wheelEvent(event);
}

void BspPreviewVulkanWidget::keyPressEvent(QKeyEvent* event) {
	if (!event) {
		QRhiWidget::keyPressEvent(event);
		return;
	}

	if (event->key() == Qt::Key_F) {
		frame_mesh();
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_R) {
		reset_camera_from_mesh();
		event->accept();
		return;
	}

	QRhiWidget::keyPressEvent(event);
}

void BspPreviewVulkanWidget::reset_camera_from_mesh() {
	if (!has_mesh_) {
		center_ = QVector3D(0, 0, 0);
		radius_ = 1.0f;
		ground_z_ = 0.0f;
	} else {
		center_ = (mesh_.mins + mesh_.maxs) * 0.5f;
		const QVector3D ext = (mesh_.maxs - mesh_.mins);
		radius_ = std::max(0.01f, 0.5f * ext.length());
		ground_z_ = mesh_.mins.z() - radius_ * 0.02f;
	}
	yaw_deg_ = 45.0f;
	pitch_deg_ = 20.0f;
	distance_ = std::max(1.0f, radius_ * 2.0f);
	pending_ground_upload_ = true;
}

void BspPreviewVulkanWidget::frame_mesh() {
	reset_camera_from_mesh();
	update();
}

void BspPreviewVulkanWidget::pan_by_pixels(const QPoint& delta) {
	const float scale = std::max(0.001f, radius_ * 0.0025f);
	const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
	const QVector3D right = QVector3D::crossProduct(dir, QVector3D(0, 0, 1)).normalized();
	const QVector3D up = QVector3D::crossProduct(right, dir).normalized();
	center_ -= right * (delta.x() * scale);
	center_ += up * (delta.y() * scale);
	pending_ground_upload_ = true;
}

void BspPreviewVulkanWidget::upload_mesh(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi() || !has_mesh_) {
		return;
	}

	const int vcount = mesh_.vertices.size();
	const int icount = mesh_.indices.size();
	if (vcount <= 0 || icount <= 0) {
		return;
	}

	QVector<GpuVertex> gpu;
	gpu.resize(vcount);
	for (int i = 0; i < vcount; ++i) {
		const BspMeshVertex& v = mesh_.vertices[i];
		const QColor c = v.color.isValid() ? v.color : QColor(255, 255, 255);
		gpu[i] = GpuVertex{
			v.pos.x(), v.pos.y(), v.pos.z(),
			v.normal.x(), v.normal.y(), v.normal.z(),
			c.redF(), c.greenF(), c.blueF(),
			v.uv.x(), v.uv.y()};
	}

	if (vbuf_) {
		delete vbuf_;
		vbuf_ = nullptr;
	}
	if (ibuf_) {
		delete ibuf_;
		ibuf_ = nullptr;
	}

	vbuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
							  gpu.size() * sizeof(GpuVertex));
	vbuf_->create();
	ibuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
							  mesh_.indices.size() * sizeof(std::uint32_t));
	ibuf_->create();

	updates->uploadStaticBuffer(vbuf_, gpu.constData());
	updates->uploadStaticBuffer(ibuf_, mesh_.indices.constData());

	index_count_ = icount;
}

void BspPreviewVulkanWidget::upload_textures(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi()) {
		return;
	}

	for (DrawSurface& s : surfaces_) {
		if (s.texture_handle) {
			delete s.texture_handle;
			s.texture_handle = nullptr;
		}
		if (s.srb) {
			delete s.srb;
			s.srb = nullptr;
		}
		s.has_texture = false;
		s.tex_scale = QVector2D(1.0f, 1.0f);
		s.tex_offset = QVector2D(0.0f, 0.0f);
	}

	ensure_uniform_buffer(std::max(1, static_cast<int>(surfaces_.size())));

	if (!white_tex_) {
		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
		const QImage white(1, 1, QImage::Format_RGBA8888);
		updates->uploadTexture(white_tex_, white);
	}
	ensure_default_srb(updates);

	for (DrawSurface& s : surfaces_) {
		const QString key = s.texture.toLower();
		const QImage img = textures_.value(key);
		if (!img.isNull()) {
			QImage converted = img.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
			if (!converted.isNull()) {
				s.texture_handle = rhi()->newTexture(QRhiTexture::RGBA8, converted.size(), 1);
				s.texture_handle->create();
				updates->uploadTexture(s.texture_handle, converted);
				s.has_texture = true;
				if (s.uv_normalized) {
					s.tex_scale = QVector2D(1.0f, 1.0f);
					s.tex_offset = QVector2D(0.0f, 0.0f);
				} else {
					const float w = std::max(1, converted.width());
					const float h = std::max(1, converted.height());
					s.tex_scale = QVector2D(1.0f / w, 1.0f / h);
					s.tex_offset = QVector2D(0.0f, 0.0f);
				}
			}
		}

		if (s.has_texture && s.texture_handle) {
			s.srb = rhi()->newShaderResourceBindings();
			s.srb->setBindings({
				QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, ubuf_, sizeof(UniformBlock)),
				QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, s.texture_handle, sampler_),
			});
			s.srb->create();
		}
	}
}

void BspPreviewVulkanWidget::update_ground_mesh_if_needed(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi() || !has_mesh_) {
		return;
	}

	update_grid_settings();
	const float extent = std::max(radius_ * 2.6f, 1.0f);
	if (!pending_ground_upload_ && ground_index_count_ == 6 &&
		std::abs(extent - ground_extent_) < 0.001f && ground_vbuf_ && ground_ibuf_) {
		return;
	}

	pending_ground_upload_ = false;
	ground_extent_ = extent;
	const float z = ground_z_;
	const float minx = center_.x() - extent;
	const float maxx = center_.x() + extent;
	const float miny = center_.y() - extent;
	const float maxy = center_.y() + extent;

	ground_vertices_.clear();
	ground_vertices_.reserve(4);
	ground_vertices_.push_back(GpuVertex{minx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f});
	ground_vertices_.push_back(GpuVertex{maxx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f});
	ground_vertices_.push_back(GpuVertex{maxx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
	ground_vertices_.push_back(GpuVertex{minx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f});

	ground_indices_ = {0, 1, 2, 0, 2, 3};

	if (ground_vbuf_) {
		delete ground_vbuf_;
		ground_vbuf_ = nullptr;
	}
	if (ground_ibuf_) {
		delete ground_ibuf_;
		ground_ibuf_ = nullptr;
	}

	ground_vbuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
									 ground_vertices_.size() * sizeof(GpuVertex));
	ground_vbuf_->create();
	ground_ibuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
									 ground_indices_.size() * sizeof(std::uint16_t));
	ground_ibuf_->create();

	updates->uploadStaticBuffer(ground_vbuf_, ground_vertices_.constData());
	updates->uploadStaticBuffer(ground_ibuf_, ground_indices_.constData());

	ground_index_count_ = 6;
}

void BspPreviewVulkanWidget::update_background_mesh_if_needed(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi()) {
		return;
	}
	if (bg_vbuf_) {
		return;
	}

	bg_vertices_.clear();
	bg_vertices_.reserve(6);
	bg_vertices_.push_back(GpuVertex{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
	bg_vertices_.push_back(GpuVertex{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
	bg_vertices_.push_back(GpuVertex{-1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f});

	bg_vbuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
								 bg_vertices_.size() * sizeof(GpuVertex));
	bg_vbuf_->create();
	updates->uploadStaticBuffer(bg_vbuf_, bg_vertices_.constData());
}

void BspPreviewVulkanWidget::update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const {
	QColor base_color;
	if (bg_mode_ == PreviewBackgroundMode::Custom && bg_custom_color_.isValid()) {
		base_color = bg_custom_color_;
	} else if (bg_mode_ == PreviewBackgroundMode::Grey) {
		base_color = QColor(88, 88, 92);
	} else {
		base_color = palette().color(QPalette::Window);
	}
	if (!base_color.isValid()) {
		base_color = QColor(64, 64, 68);
	}

	QColor top_color = base_color.lighter(112);
	QColor bottom_color = base_color.darker(118);

	if (top) {
		*top = QVector3D(top_color.redF(), top_color.greenF(), top_color.blueF());
	}
	if (bottom) {
		*bottom = QVector3D(bottom_color.redF(), bottom_color.greenF(), bottom_color.blueF());
	}
	if (base) {
		*base = QVector3D(base_color.redF(), base_color.greenF(), base_color.blueF());
	}
}

void BspPreviewVulkanWidget::update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const {
	QVector3D base_vec;
	update_background_colors(nullptr, nullptr, &base_vec);
	QColor base_color = QColor::fromRgbF(base_vec.x(), base_vec.y(), base_vec.z());
	QColor grid_color = (base_color.lightness() < 128) ? base_color.lighter(140) : base_color.darker(140);

	QColor axis_x_color = palette().color(QPalette::Highlight);
	if (!axis_x_color.isValid()) {
		axis_x_color = QColor(220, 80, 80);
	}
	QColor axis_y_color = palette().color(QPalette::Link);
	if (!axis_y_color.isValid()) {
		axis_y_color = QColor(80, 180, 120);
	}

	if (grid) {
		*grid = QVector3D(grid_color.redF(), grid_color.greenF(), grid_color.blueF());
	}
	if (axis_x) {
		*axis_x = QVector3D(axis_x_color.redF(), axis_x_color.greenF(), axis_x_color.blueF());
	}
	if (axis_y) {
		*axis_y = QVector3D(axis_y_color.redF(), axis_y_color.greenF(), axis_y_color.blueF());
	}
}

void BspPreviewVulkanWidget::update_grid_settings() {
	grid_scale_ = std::max(radius_ * 0.35f, 1.0f);
}

void BspPreviewVulkanWidget::destroy_mesh_resources() {
	if (vbuf_) {
		delete vbuf_;
		vbuf_ = nullptr;
	}
	if (ibuf_) {
		delete ibuf_;
		ibuf_ = nullptr;
	}
	if (ground_vbuf_) {
		delete ground_vbuf_;
		ground_vbuf_ = nullptr;
	}
	if (ground_ibuf_) {
		delete ground_ibuf_;
		ground_ibuf_ = nullptr;
	}
	if (bg_vbuf_) {
		delete bg_vbuf_;
		bg_vbuf_ = nullptr;
	}
	if (ubuf_) {
		delete ubuf_;
		ubuf_ = nullptr;
	}
	for (DrawSurface& s : surfaces_) {
		if (s.texture_handle) {
			delete s.texture_handle;
			s.texture_handle = nullptr;
		}
		if (s.srb) {
			delete s.srb;
			s.srb = nullptr;
		}
	}
	if (default_srb_) {
		delete default_srb_;
		default_srb_ = nullptr;
	}
	index_count_ = 0;
	ground_index_count_ = 0;
}

void BspPreviewVulkanWidget::destroy_pipeline_resources() {
	if (pipeline_) {
		delete pipeline_;
		pipeline_ = nullptr;
	}
	if (sampler_) {
		delete sampler_;
		sampler_ = nullptr;
	}
	if (white_tex_) {
		delete white_tex_;
		white_tex_ = nullptr;
	}
}

void BspPreviewVulkanWidget::ensure_pipeline() {
	if (!rhi() || !vert_shader_.isValid() || !frag_shader_.isValid()) {
		return;
	}
	ensure_default_srb();
	if (pipeline_) {
		delete pipeline_;
		pipeline_ = nullptr;
	}

	pipeline_ = rhi()->newGraphicsPipeline();
	pipeline_->setShaderStages({
		{QRhiShaderStage::Vertex, vert_shader_},
		{QRhiShaderStage::Fragment, frag_shader_},
	});

	QRhiVertexInputLayout input_layout;
	input_layout.setBindings({
		QRhiVertexInputBinding(sizeof(GpuVertex)),
	});
	input_layout.setAttributes({
		QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, offsetof(GpuVertex, px)),
		QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, offsetof(GpuVertex, nx)),
		QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float3, offsetof(GpuVertex, r)),
		QRhiVertexInputAttribute(0, 3, QRhiVertexInputAttribute::Float2, offsetof(GpuVertex, u)),
	});
	pipeline_->setVertexInputLayout(input_layout);
	pipeline_->setShaderResourceBindings(default_srb_);
	pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
	pipeline_->setDepthTest(true);
	pipeline_->setDepthWrite(true);
	pipeline_->setCullMode(QRhiGraphicsPipeline::None);
	pipeline_->setSampleCount(sampleCount());
	if (wireframe_enabled_ && rhi()->isFeatureSupported(QRhi::NonFillPolygonMode)) {
		pipeline_->setPolygonMode(QRhiGraphicsPipeline::Line);
	} else {
		pipeline_->setPolygonMode(QRhiGraphicsPipeline::Fill);
	}
	pipeline_->create();

	pipeline_dirty_ = false;
}

void BspPreviewVulkanWidget::ensure_default_srb(QRhiResourceUpdateBatch* updates) {
	if (!rhi()) {
		return;
	}
	if (!sampler_) {
		sampler_ = rhi()->newSampler(QRhiSampler::Linear,
									  QRhiSampler::Linear,
									  QRhiSampler::None,
									  QRhiSampler::Repeat,
									  QRhiSampler::Repeat);
		sampler_->create();
	}
	if (!white_tex_) {
		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
		if (updates) {
			const QImage white(1, 1, QImage::Format_RGBA8888);
			updates->uploadTexture(white_tex_, white);
		}
	}
	if (default_srb_) {
		return;
	}
	if (!ubuf_) {
		ensure_uniform_buffer(1);
	}
	default_srb_ = rhi()->newShaderResourceBindings();
	default_srb_->setBindings({
		QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, ubuf_, sizeof(UniformBlock)),
		QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, white_tex_, sampler_),
	});
	default_srb_->create();
	pipeline_dirty_ = true;
}

void BspPreviewVulkanWidget::ensure_uniform_buffer(int surface_count) {
	if (!rhi()) {
		return;
	}
	const quint32 stride = aligned_uniform_stride(rhi(), sizeof(UniformBlock));
	const quint32 required = stride * static_cast<quint32>(std::max(1, surface_count));
	if (ubuf_ && ubuf_->size() >= static_cast<int>(required)) {
		ubuf_stride_ = stride;
		return;
	}
	if (ubuf_) {
		delete ubuf_;
		ubuf_ = nullptr;
	}
	ubuf_ = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, required);
	ubuf_->create();
	ubuf_stride_ = stride;

	if (default_srb_) {
		delete default_srb_;
		default_srb_ = nullptr;
	}
	pipeline_dirty_ = true;
}
