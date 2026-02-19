#include "ui/bsp_preview_vulkan_widget.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QFocusEvent>

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

constexpr float kOrbitSensitivityDegPerPixel = 0.45f;
constexpr float kFlyLookSensitivityDegPerPixel = 0.30f;
constexpr float kFlySpeedWheelFactor = 1.15f;
constexpr float kFlySpeedMin = 1.0f;
constexpr float kFlySpeedMax = 250000.0f;
constexpr float kFlySpeedShiftMul = 4.0f;
constexpr float kFlySpeedCtrlMul = 0.25f;

constexpr int kFlyMoveForward = 1 << 0;
constexpr int kFlyMoveBackward = 1 << 1;
constexpr int kFlyMoveLeft = 1 << 2;
constexpr int kFlyMoveRight = 1 << 3;
constexpr int kFlyMoveUp = 1 << 4;
constexpr int kFlyMoveDown = 1 << 5;

float ground_pad(float radius) {
	const float safe_radius = std::max(radius, 1.0f);
	return std::clamp(safe_radius * 0.002f, 0.5f, 32.0f);
}

float orbit_min_distance(float radius) {
	return std::max(0.01f, radius * 0.001f);
}

float orbit_max_distance(float radius) {
	const float min_dist = orbit_min_distance(radius);
	return std::max(min_dist * 2.0f, std::max(radius, 1.0f) * 500.0f);
}

QVector3D safe_right_from_forward(const QVector3D& forward) {
	QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 0.0f, 1.0f));
	if (right.lengthSquared() < 1e-6f) {
		right = QVector3D(1.0f, 0.0f, 0.0f);
	} else {
		right.normalize();
	}
	return right;
}

float fit_distance_for_aabb(const QVector3D& half_extents,
							const QVector3D& view_forward,
							float aspect,
							float fov_y_deg) {
	constexpr float kPi = 3.14159265358979323846f;
	const QVector3D safe_half(std::max(half_extents.x(), 0.001f),
							  std::max(half_extents.y(), 0.001f),
							  std::max(half_extents.z(), 0.001f));
	const float safe_aspect = std::max(aspect, 0.01f);
	const float fov_y = fov_y_deg * kPi / 180.0f;
	const float tan_half_y = std::tan(fov_y * 0.5f);
	const float tan_half_x = std::max(0.001f, tan_half_y * safe_aspect);
	const float safe_tan_half_y = std::max(0.001f, tan_half_y);

	const QVector3D fwd = view_forward.normalized();
	const QVector3D right = safe_right_from_forward(fwd);
	const QVector3D up = QVector3D::crossProduct(right, fwd).normalized();

	const auto projected_radius = [&](const QVector3D& axis) -> float {
		return std::abs(axis.x()) * safe_half.x() +
			   std::abs(axis.y()) * safe_half.y() +
			   std::abs(axis.z()) * safe_half.z();
	};

	const float radius_x = projected_radius(right);
	const float radius_y = projected_radius(up);
	const float radius_z = projected_radius(fwd);
	const float dist_x = radius_x / tan_half_x;
	const float dist_y = radius_y / safe_tan_half_y;
	return radius_z + std::max(dist_x, dist_y);
}

void apply_orbit_zoom(float factor,
					  float min_dist,
					  float max_dist,
					  float* distance,
					  QVector3D* center,
					  float yaw_deg,
					  float pitch_deg) {
	if (!distance || !center) {
		return;
	}
	const float safe_factor = std::clamp(factor, 0.01f, 100.0f);
	const float target_distance = (*distance) * safe_factor;
	if (target_distance < min_dist) {
		const float push = min_dist - target_distance;
		if (push > 0.0f) {
			const QVector3D forward = (-spherical_dir(yaw_deg, pitch_deg)).normalized();
			*center += forward * push;
		}
		*distance = min_dist;
		return;
	}
	*distance = std::clamp(target_distance, min_dist, max_dist);
}

float quantized_grid_scale(float reference_distance) {
	const float target = std::max(reference_distance / 16.0f, 1.0f);
	const float exponent = std::floor(std::log10(target));
	const float base = std::pow(10.0f, exponent);
	const float n = target / std::max(base, 1e-6f);
	float step = base;
	if (n >= 5.0f) {
		step = 5.0f * base;
	} else if (n >= 2.0f) {
		step = 2.0f * base;
	}
	return std::max(step, 1.0f);
}

float quantized_grid_step(float target_step) {
	const float safe = std::max(target_step, 1.0f);
	const float exp2 = std::floor(std::log2(safe));
	float step = std::pow(2.0f, exp2);
	const float n = safe / std::max(step, 1e-6f);
	if (n > 1.5f) {
		step *= 2.0f;
	}
	return std::max(step, 1.0f);
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
	fly_timer_.setInterval(16);
	fly_timer_.setTimerType(Qt::PreciseTimer);
	connect(&fly_timer_, &QTimer::timeout, this, &BspPreviewVulkanWidget::on_fly_tick);
	setToolTip(
		"3D Controls:\n"
		"- Orbit: Middle-drag (Alt+Left-drag)\n"
		"- Pan: Shift+Middle-drag (Alt+Shift+Left-drag)\n"
		"- Dolly: Ctrl+Middle-drag (Alt+Ctrl+Left-drag)\n"
		"- Zoom: Mouse wheel\n"
		"- Fly: Hold Right Mouse + WASD (Q/E up/down, wheel adjusts speed, Shift faster, Ctrl slower)\n"
		"- Reference: Player box 32x32x56 (Grid mode)\n"
		"- Frame: F\n"
		"- Reset: R / Home");
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
		ds.lightmap_index = s.lightmap_index;
		surfaces_.push_back(std::move(ds));
	}

	pending_upload_ = has_mesh_;
	pending_texture_upload_ = has_mesh_;
	if (grid_vbuf_) {
		delete grid_vbuf_;
		grid_vbuf_ = nullptr;
	}
	grid_vertex_count_ = 0;
	grid_line_step_ = 0.0f;
	grid_line_center_i_ = 0;
	grid_line_center_j_ = 0;
	grid_line_half_lines_ = 0;
	grid_line_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	axis_x_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	axis_y_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	reset_camera_from_mesh();
	camera_fit_pending_ = has_mesh_;
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

void BspPreviewVulkanWidget::set_fov_degrees(int degrees) {
	const float clamped = std::clamp(static_cast<float>(degrees), 40.0f, 120.0f);
	if (std::abs(clamped - fov_y_deg_) < 0.001f) {
		return;
	}
	fov_y_deg_ = clamped;
	pending_ground_upload_ = true;
	uniform_dirty_ = true;
	update();
}

PreviewCameraState BspPreviewVulkanWidget::camera_state() const {
	PreviewCameraState state;
	state.center = center_;
	state.yaw_deg = yaw_deg_;
	state.pitch_deg = pitch_deg_;
	state.distance = distance_;
	state.valid = true;
	return state;
}

void BspPreviewVulkanWidget::set_camera_state(const PreviewCameraState& state) {
	if (!state.valid) {
		return;
	}
	center_ = state.center;
	yaw_deg_ = std::remainder(state.yaw_deg, 360.0f);
	pitch_deg_ = std::clamp(state.pitch_deg, -89.0f, 89.0f);
	distance_ = std::clamp(state.distance, orbit_min_distance(radius_), orbit_max_distance(radius_));
	camera_fit_pending_ = false;
	pending_ground_upload_ = true;
	uniform_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::clear() {
	has_mesh_ = false;
	camera_fit_pending_ = false;
	pending_upload_ = false;
	pending_texture_upload_ = false;
	textures_.clear();
	surfaces_.clear();
	mesh_ = BspMesh{};
	destroy_mesh_resources();
	update();
}

void BspPreviewVulkanWidget::initialize(QRhiCommandBuffer* cb) {
	vert_shader_ = load_shader(":/assets/shaders/bsp_preview.vert.qsb");
	frag_shader_ = load_shader(":/assets/shaders/bsp_preview.frag.qsb");
	grid_vert_shader_ = load_shader(":/assets/shaders/grid_lines.vert.qsb");
	grid_frag_shader_ = load_shader(":/assets/shaders/grid_lines.frag.qsb");

	if (rhi()) {
		sampler_ = rhi()->newSampler(QRhiSampler::Linear,
									  QRhiSampler::Linear,
									  QRhiSampler::None,
									  QRhiSampler::Repeat,
									  QRhiSampler::Repeat);
		sampler_->create();

		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
		if (cb) {
			QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();
			QImage white(1, 1, QImage::Format_RGBA8888);
			white.fill(Qt::white);
			updates->uploadTexture(white_tex_, white);
			cb->resourceUpdate(updates);
		}
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
	if (camera_fit_pending_ && has_mesh_ && width() > 0 && height() > 0) {
		frame_mesh();
		camera_fit_pending_ = false;
	}

	const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
	const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
	const QVector3D cam_pos = center_ + dir * distance_;
	const QVector3D view_target = center_;
	QVector3D scene_center = center_;
	if (has_mesh_) {
		scene_center = (mesh_.mins + mesh_.maxs) * 0.5f;
	}
	const float dist_to_scene = (cam_pos - scene_center).length();

	const float near_plane = std::clamp(radius_ * 0.0005f, 0.05f, 16.0f);
	const float far_plane = std::max(near_plane + 10.0f, dist_to_scene + radius_ * 3.0f);

	QMatrix4x4 proj;
	proj.perspective(fov_y_deg_, aspect, near_plane, far_plane);

	QMatrix4x4 view;
	view.lookAt(cam_pos, view_target, QVector3D(0, 0, 1));

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

	if (grid_mode_ == PreviewGridMode::Grid && has_mesh_) {
		QRhiResourceUpdateBatch* grid_updates = rhi()->nextResourceUpdateBatch();
		update_grid_lines_if_needed(grid_updates, cam_pos, aspect);
		cb->resourceUpdate(grid_updates);
	}

	const bool draw_grid = (grid_mode_ == PreviewGridMode::Grid && grid_vbuf_ && grid_vertex_count_ > 0);
	const int draw_count = 1 + (draw_ground ? 1 : 0) + (draw_grid ? 1 : 0) + surface_count;
	ensure_uniform_buffer(draw_count);
	if (pipeline_dirty_) {
		ensure_pipeline();
	}

	QByteArray udata;
	udata.resize(static_cast<int>(ubuf_stride_ * draw_count));
	udata.fill(0);

	auto write_uniform = [&](int i,
						 const QVector2D& tex_scale,
						 const QVector2D& tex_offset,
						 bool has_tex,
						 bool has_lightmap,
						 bool is_ground,
						 bool is_background) {
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
		u.grid_params = QVector4D(0.0f, grid_scale_, 0.0f, 0.0f);
		u.grid_color = QVector4D(grid_color, 0.0f);
		u.axis_color_x = QVector4D(axis_x, 0.0f);
		u.axis_color_y = QVector4D(axis_y, 0.0f);
		u.bg_top = QVector4D(bg_top, 0.0f);
		u.bg_bottom = QVector4D(bg_bottom, 0.0f);
		u.misc = QVector4D(lightmap_enabled_ ? 1.0f : 0.0f,
						  has_tex ? 1.0f : 0.0f,
						  is_background ? 1.0f : 0.0f,
						  has_lightmap ? 1.0f : 0.0f);
		std::memcpy(udata.data() + static_cast<int>(i * ubuf_stride_), &u, sizeof(UniformBlock));
	};

	int uidx = 0;
	write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, false, false, true);
	if (draw_ground) {
		write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, false, true, false);
	}
	if (draw_grid) {
		write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, false, false, false);
	}
	if (draw_surfaces) {
		if (surfaces_.isEmpty()) {
			write_uniform(uidx++, QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f), false, false, false, false);
		} else {
			for (int i = 0; i < surfaces_.size(); ++i) {
				const DrawSurface& s = surfaces_[i];
				const bool use_tex = textured_enabled_ && s.has_texture;
				const bool use_lightmap = lightmap_enabled_ && s.has_lightmap;
				write_uniform(uidx++, s.tex_scale, s.tex_offset, use_tex, use_lightmap, false, false);
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

	if (draw_grid && grid_pipeline_ && grid_srb_ && grid_vbuf_) {
		cb->setGraphicsPipeline(grid_pipeline_);
		cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));
		const QRhiCommandBuffer::VertexInput bindings[] = {
			{grid_vbuf_, 0},
		};
		const quint32 offset = ubuf_stride_ * static_cast<quint32>(1 + (draw_ground ? 1 : 0));
		QRhiCommandBuffer::DynamicOffset dyn = {0, offset};
		cb->setVertexInput(0, 1, bindings);
		cb->setShaderResources(grid_srb_, 1, &dyn);
		cb->draw(static_cast<quint32>(grid_vertex_count_));
		cb->setGraphicsPipeline(pipeline_);
		cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));
	}

	const QRhiCommandBuffer::VertexInput bindings[] = {
		{vbuf_, 0},
	};
	cb->setVertexInput(0, 1, bindings, ibuf_, 0, QRhiCommandBuffer::IndexUInt32);

	const int base_offset = 1 + (draw_ground ? 1 : 0) + (draw_grid ? 1 : 0);
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
	grid_vert_shader_ = {};
	grid_frag_shader_ = {};
}

void BspPreviewVulkanWidget::resizeEvent(QResizeEvent* event) {
	QRhiWidget::resizeEvent(event);
	if (camera_fit_pending_ && has_mesh_ && width() > 0 && height() > 0) {
		frame_mesh();
		camera_fit_pending_ = false;
	}
	pipeline_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::mousePressEvent(QMouseEvent* event) {
	if (!event) {
		QRhiWidget::mousePressEvent(event);
		return;
	}

	const Qt::MouseButton button = event->button();
	const Qt::KeyboardModifiers mods = event->modifiers();
	const bool rmb = (button == Qt::RightButton);
	const bool mmb = (button == Qt::MiddleButton);
	const bool alt_lmb = (button == Qt::LeftButton && (mods & Qt::AltModifier));
	const bool alt_rmb = (rmb && (mods & Qt::AltModifier));
	if (rmb && !alt_rmb) {
		setFocus(Qt::MouseFocusReason);
		last_mouse_pos_ = event->pos();
		drag_mode_ = DragMode::Look;
		drag_buttons_ = button;
		grabMouse(QCursor(Qt::BlankCursor));
		fly_elapsed_.restart();
		fly_last_nsecs_ = fly_elapsed_.nsecsElapsed();
		fly_timer_.start();
		event->accept();
		return;
	}
	if (mmb || alt_lmb) {
		setFocus(Qt::MouseFocusReason);
		last_mouse_pos_ = event->pos();
		drag_mode_ = DragMode::Orbit;
		if (mods & Qt::ControlModifier) {
			drag_mode_ = DragMode::Dolly;
		} else if (mods & Qt::ShiftModifier) {
			drag_mode_ = DragMode::Pan;
		}
		drag_buttons_ = button;
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

	if (drag_mode_ == DragMode::None || drag_buttons_ == Qt::NoButton ||
		(event->buttons() & drag_buttons_) != drag_buttons_) {
		if (drag_mode_ == DragMode::Look) {
			fly_timer_.stop();
			fly_move_mask_ = 0;
			releaseMouse();
			unsetCursor();
		}
		drag_mode_ = DragMode::None;
		drag_buttons_ = Qt::NoButton;
		QRhiWidget::mouseMoveEvent(event);
		return;
	}

	const QPoint delta = event->pos() - last_mouse_pos_;
	last_mouse_pos_ = event->pos();

	if (drag_mode_ == DragMode::Look) {
		const QVector3D old_dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
		const QVector3D cam_pos = center_ + old_dir * distance_;
		yaw_deg_ += static_cast<float>(delta.x()) * kFlyLookSensitivityDegPerPixel;
		pitch_deg_ = std::clamp(pitch_deg_ - static_cast<float>(delta.y()) * kFlyLookSensitivityDegPerPixel, -89.0f, 89.0f);
		const QVector3D new_dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
		center_ = cam_pos - new_dir * distance_;
		update();
		event->accept();
		return;
	}

	if (drag_mode_ == DragMode::Orbit) {
		yaw_deg_ += static_cast<float>(delta.x()) * kOrbitSensitivityDegPerPixel;
		pitch_deg_ = std::clamp(pitch_deg_ - static_cast<float>(delta.y()) * kOrbitSensitivityDegPerPixel, -89.0f, 89.0f);
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

	if (drag_mode_ == DragMode::Dolly) {
		dolly_by_pixels(delta);
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

	if (drag_mode_ != DragMode::None && drag_buttons_ != Qt::NoButton &&
		(Qt::MouseButtons(event->button()) & drag_buttons_) &&
		(event->buttons() & drag_buttons_) != drag_buttons_) {
		if (drag_mode_ == DragMode::Look) {
			fly_timer_.stop();
			fly_move_mask_ = 0;
			releaseMouse();
			unsetCursor();
		}
		drag_mode_ = DragMode::None;
		drag_buttons_ = Qt::NoButton;
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

	if (drag_mode_ == DragMode::Look) {
		const QPoint num_deg = event->angleDelta() / 8;
		if (!num_deg.isNull()) {
			const float steps = static_cast<float>(num_deg.y()) / 15.0f;
			const float factor = std::pow(kFlySpeedWheelFactor, steps);
			fly_speed_ = std::clamp(fly_speed_ * factor, kFlySpeedMin, kFlySpeedMax);
			event->accept();
			return;
		}
	}

	const QPoint num_deg = event->angleDelta() / 8;
	if (!num_deg.isNull()) {
		const float factor = std::pow(0.85f, static_cast<float>(num_deg.y()) / 15.0f);
		apply_orbit_zoom(factor,
						orbit_min_distance(radius_),
						orbit_max_distance(radius_),
						&distance_,
						&center_,
						yaw_deg_,
						pitch_deg_);
		pending_ground_upload_ = true;
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
		update();
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_R || event->key() == Qt::Key_Home) {
		reset_camera_from_mesh();
		update();
		event->accept();
		return;
	}

	if (drag_mode_ == DragMode::Look) {
		const int before = fly_move_mask_;
		set_fly_key(event->key(), true);
		if (fly_move_mask_ != before) {
			event->accept();
			return;
		}
	}

	QRhiWidget::keyPressEvent(event);
}

void BspPreviewVulkanWidget::keyReleaseEvent(QKeyEvent* event) {
	if (!event) {
		QRhiWidget::keyReleaseEvent(event);
		return;
	}

	if (drag_mode_ == DragMode::Look) {
		const int before = fly_move_mask_;
		set_fly_key(event->key(), false);
		if (fly_move_mask_ != before) {
			event->accept();
			return;
		}
	}

	QRhiWidget::keyReleaseEvent(event);
}

void BspPreviewVulkanWidget::focusOutEvent(QFocusEvent* event) {
	fly_timer_.stop();
	fly_move_mask_ = 0;
	if (drag_mode_ == DragMode::Look) {
		releaseMouse();
		unsetCursor();
		drag_mode_ = DragMode::None;
		drag_buttons_ = Qt::NoButton;
	}
	QRhiWidget::focusOutEvent(event);
}

void BspPreviewVulkanWidget::reset_camera_from_mesh() {
	yaw_deg_ = 45.0f;
	pitch_deg_ = 55.0f;
	camera_fit_pending_ = false;
	frame_mesh();
	fly_speed_ = std::clamp(std::max(640.0f, radius_ * 0.25f), kFlySpeedMin, kFlySpeedMax);
}

void BspPreviewVulkanWidget::frame_mesh() {
	if (!has_mesh_) {
		center_ = QVector3D(0, 0, 0);
		radius_ = 1.0f;
		distance_ = 3.0f;
		ground_z_ = 0.0f;
		ground_extent_ = 0.0f;
		pending_ground_upload_ = true;
		return;
	}
	center_ = (mesh_.mins + mesh_.maxs) * 0.5f;
	const QVector3D half_extents = (mesh_.maxs - mesh_.mins) * 0.5f;
	radius_ = std::max(0.01f, half_extents.length());
	const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
	const QVector3D view_forward = (-spherical_dir(yaw_deg_, pitch_deg_)).normalized();
	const float fit_dist = fit_distance_for_aabb(half_extents, view_forward, aspect, fov_y_deg_);
	distance_ = std::clamp(fit_dist * 1.05f, orbit_min_distance(radius_), orbit_max_distance(radius_));
	ground_z_ = mesh_.mins.z() - ground_pad(radius_);
	ground_extent_ = 0.0f;
	pending_ground_upload_ = true;
}

void BspPreviewVulkanWidget::pan_by_pixels(const QPoint& delta) {
	if (height() <= 0) {
		return;
	}

	constexpr float kPi = 3.14159265358979323846f;
	const float fov_rad = fov_y_deg_ * kPi / 180.0f;
	const float units_per_px =
		(2.0f * distance_ * std::tan(fov_rad * 0.5f)) / std::max(1.0f, static_cast<float>(height()));

	const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
	const QVector3D forward = (-dir).normalized();
	QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 0.0f, 1.0f));
	if (right.lengthSquared() < 1e-6f) {
		right = QVector3D(1.0f, 0.0f, 0.0f);
	} else {
		right.normalize();
	}
	const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
	center_ += (-right * static_cast<float>(delta.x()) + up * static_cast<float>(delta.y())) * units_per_px;
	pending_ground_upload_ = true;
}

void BspPreviewVulkanWidget::dolly_by_pixels(const QPoint& delta) {
	const float factor = std::pow(1.01f, static_cast<float>(delta.y()));
	apply_orbit_zoom(factor,
					orbit_min_distance(radius_),
					orbit_max_distance(radius_),
					&distance_,
					&center_,
					yaw_deg_,
					pitch_deg_);
	pending_ground_upload_ = true;
}

void BspPreviewVulkanWidget::on_fly_tick() {
	if (drag_mode_ != DragMode::Look) {
		fly_timer_.stop();
		fly_move_mask_ = 0;
		return;
	}

	if (!fly_elapsed_.isValid()) {
		fly_elapsed_.start();
		fly_last_nsecs_ = fly_elapsed_.nsecsElapsed();
		return;
	}

	const qint64 now = fly_elapsed_.nsecsElapsed();
	const qint64 delta_nsecs = now - fly_last_nsecs_;
	fly_last_nsecs_ = now;

	float dt = static_cast<float>(delta_nsecs) * 1e-9f;
	if (dt <= 0.0f) {
		return;
	}
	dt = std::min(dt, 0.05f);

	if (fly_move_mask_ == 0) {
		return;
	}

	const float forward_amt = (fly_move_mask_ & kFlyMoveForward ? 1.0f : 0.0f) - (fly_move_mask_ & kFlyMoveBackward ? 1.0f : 0.0f);
	const float right_amt = (fly_move_mask_ & kFlyMoveRight ? 1.0f : 0.0f) - (fly_move_mask_ & kFlyMoveLeft ? 1.0f : 0.0f);
	const float up_amt = (fly_move_mask_ & kFlyMoveUp ? 1.0f : 0.0f) - (fly_move_mask_ & kFlyMoveDown ? 1.0f : 0.0f);

	const QVector3D forward = (-spherical_dir(yaw_deg_, 0.0f)).normalized();
	const QVector3D right = safe_right_from_forward(forward);
	const QVector3D up(0.0f, 0.0f, 1.0f);

	QVector3D move = forward * forward_amt + right * right_amt + up * up_amt;
	if (move.lengthSquared() < 1e-6f) {
		return;
	}
	move.normalize();

	float speed = std::clamp(fly_speed_, kFlySpeedMin, kFlySpeedMax);
	const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
	if (mods & Qt::ShiftModifier) {
		speed *= kFlySpeedShiftMul;
	}
	if (mods & Qt::ControlModifier) {
		speed *= kFlySpeedCtrlMul;
	}

	center_ += move * (speed * dt);
	pending_ground_upload_ = true;
	uniform_dirty_ = true;
	update();
}

void BspPreviewVulkanWidget::set_fly_key(int key, bool down) {
	int mask = 0;
	switch (key) {
		case Qt::Key_W:
		case Qt::Key_Up:
			mask = kFlyMoveForward;
			break;
		case Qt::Key_S:
		case Qt::Key_Down:
			mask = kFlyMoveBackward;
			break;
		case Qt::Key_A:
		case Qt::Key_Left:
			mask = kFlyMoveLeft;
			break;
		case Qt::Key_D:
		case Qt::Key_Right:
			mask = kFlyMoveRight;
			break;
		case Qt::Key_E:
		case Qt::Key_Space:
		case Qt::Key_PageUp:
			mask = kFlyMoveUp;
			break;
		case Qt::Key_Q:
		case Qt::Key_C:
		case Qt::Key_PageDown:
			mask = kFlyMoveDown;
			break;
		default:
			return;
	}

	if (down) {
		fly_move_mask_ |= mask;
	} else {
		fly_move_mask_ &= ~mask;
	}
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
			v.uv.x(), v.uv.y(),
			v.lightmap_uv.x(), v.lightmap_uv.y()};
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
		s.has_lightmap = false;
		s.tex_scale = QVector2D(1.0f, 1.0f);
		s.tex_offset = QVector2D(0.0f, 0.0f);
	}
	for (QRhiTexture*& lm : lightmap_textures_) {
		if (lm) {
			delete lm;
			lm = nullptr;
		}
	}
	lightmap_textures_.clear();

	const int surface_slots = std::max(1, static_cast<int>(surfaces_.size()));
	ensure_uniform_buffer(surface_slots + 2);

	if (!white_tex_) {
		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
		QImage white(1, 1, QImage::Format_RGBA8888);
		white.fill(Qt::white);
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
	}

	lightmap_textures_.resize(mesh_.lightmaps.size());
	for (int i = 0; i < mesh_.lightmaps.size(); ++i) {
		QImage converted = mesh_.lightmaps[i].convertToFormat(QImage::Format_RGBA8888);
		if (converted.isNull()) {
			lightmap_textures_[i] = nullptr;
			continue;
		}
		QRhiTexture* lm_tex = rhi()->newTexture(QRhiTexture::RGBA8, converted.size(), 1);
		lm_tex->create();
		updates->uploadTexture(lm_tex, converted);
		lightmap_textures_[i] = lm_tex;
	}

	for (DrawSurface& s : surfaces_) {
		if (s.lightmap_index >= 0 && s.lightmap_index < lightmap_textures_.size()) {
			s.has_lightmap = (lightmap_textures_[s.lightmap_index] != nullptr);
		}

		QRhiTexture* diffuse_tex = (s.has_texture && s.texture_handle) ? s.texture_handle : white_tex_;
		QRhiTexture* lm_tex = white_tex_;
		if (s.has_lightmap && s.lightmap_index >= 0 && s.lightmap_index < lightmap_textures_.size()) {
			QRhiTexture* handle = lightmap_textures_[s.lightmap_index];
			if (handle) {
				lm_tex = handle;
			}
		}

		if (!diffuse_tex || !lm_tex) {
			continue;
		}

		s.srb = rhi()->newShaderResourceBindings();
		s.srb->setBindings({
			QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, ubuf_, sizeof(UniformBlock)),
			QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, diffuse_tex, sampler_),
			QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, lm_tex, sampler_),
		});
		s.srb->create();
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
	ground_vertices_.push_back(GpuVertex{minx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
	ground_vertices_.push_back(GpuVertex{maxx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f});
	ground_vertices_.push_back(GpuVertex{maxx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
	ground_vertices_.push_back(GpuVertex{minx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f});

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

void BspPreviewVulkanWidget::update_grid_lines_if_needed(QRhiResourceUpdateBatch* updates, const QVector3D& cam_pos, float aspect) {
	if (!updates || !rhi() || grid_mode_ != PreviewGridMode::Grid) {
		return;
	}

	constexpr float kGridPixelSpacing = 45.0f;
	constexpr int kMajorDiv = 8;
	constexpr int kMaxHalfLines = 200;
	constexpr float kAlphaMinor = 0.18f;
	constexpr float kAlphaMajor = 0.35f;
	constexpr float kAlphaAxis = 0.85f;

	const float dist_to_plane = std::max(0.01f, std::abs(cam_pos.z() - ground_z_));

	constexpr float kPi = 3.14159265358979323846f;
	const float fov_rad = fov_y_deg_ * kPi / 180.0f;
	const float units_per_px =
		(2.0f * dist_to_plane * std::tan(fov_rad * 0.5f)) / std::max(1.0f, static_cast<float>(height()));

	const float target_step = std::max(1.0f, units_per_px * kGridPixelSpacing);
	const float step = quantized_grid_step(target_step);

	const float half_h = dist_to_plane * std::tan(fov_rad * 0.5f);
	const float half_w = half_h * std::max(aspect, 0.01f);
	const float desired_extent = std::max(half_w, half_h) * 1.25f;
	const int half_lines = std::clamp(static_cast<int>(std::ceil(desired_extent / step)) + 2, 8, kMaxHalfLines);

	const int center_i = static_cast<int>(std::floor(cam_pos.x() / step));
	const int center_j = static_cast<int>(std::floor(cam_pos.y() / step));

	QVector3D grid_color;
	QVector3D axis_x;
	QVector3D axis_y;
	update_grid_colors(&grid_color, &axis_x, &axis_y);

	const bool colors_same =
		(grid_color == grid_line_color_cached_ && axis_x == axis_x_color_cached_ && axis_y == axis_y_color_cached_);
	if (std::abs(step - grid_line_step_) < 0.0001f &&
		center_i == grid_line_center_i_ &&
		center_j == grid_line_center_j_ &&
		half_lines == grid_line_half_lines_ &&
		colors_same &&
		grid_vbuf_ &&
		grid_vertex_count_ > 0) {
		return;
	}

	grid_line_step_ = step;
	grid_line_center_i_ = center_i;
	grid_line_center_j_ = center_j;
	grid_line_half_lines_ = half_lines;
	grid_line_color_cached_ = grid_color;
	axis_x_color_cached_ = axis_x;
	axis_y_color_cached_ = axis_y;

	const float z_offset = std::clamp(step * 0.0005f, 0.01f, 0.25f);
	const float z = ground_z_ + z_offset;

	const int i_min = center_i - half_lines;
	const int i_max = center_i + half_lines;
	const int j_min = center_j - half_lines;
	const int j_max = center_j + half_lines;

	const float x_min = static_cast<float>(i_min) * step;
	const float x_max = static_cast<float>(i_max) * step;
	const float y_min = static_cast<float>(j_min) * step;
	const float y_max = static_cast<float>(j_max) * step;

	QVector<GridLineVertex> verts;
	const int line_count = (2 * half_lines + 1);
	verts.reserve(line_count * 2 * 2 + 24);

	const auto push_line = [&](float ax, float ay, float bx, float by, const QVector3D& c, float a) {
		verts.push_back(GridLineVertex{ax, ay, z, c.x(), c.y(), c.z(), a});
		verts.push_back(GridLineVertex{bx, by, z, c.x(), c.y(), c.z(), a});
	};
	const auto push_line3 = [&](float ax,
								float ay,
								float az,
								float bx,
								float by,
								float bz,
								const QVector3D& c,
								float a) {
		verts.push_back(GridLineVertex{ax, ay, az, c.x(), c.y(), c.z(), a});
		verts.push_back(GridLineVertex{bx, by, bz, c.x(), c.y(), c.z(), a});
	};

	for (int i = i_min; i <= i_max; ++i) {
		const float x = static_cast<float>(i) * step;
		if (i == 0) {
			push_line(x, y_min, x, y_max, axis_x, kAlphaAxis);
		} else if ((i % kMajorDiv) == 0) {
			push_line(x, y_min, x, y_max, grid_color, kAlphaMajor);
		} else {
			push_line(x, y_min, x, y_max, grid_color, kAlphaMinor);
		}
	}

	for (int j = j_min; j <= j_max; ++j) {
		const float y = static_cast<float>(j) * step;
		if (j == 0) {
			push_line(x_min, y, x_max, y, axis_y, kAlphaAxis);
		} else if ((j % kMajorDiv) == 0) {
			push_line(x_min, y, x_max, y, grid_color, kAlphaMajor);
		} else {
			push_line(x_min, y, x_max, y, grid_color, kAlphaMinor);
		}
	}

	if (has_mesh_) {
		constexpr float kPlayerHalfWidth = 16.0f;
		constexpr float kPlayerHeight = 56.0f;
		constexpr float kPlayerAlpha = 0.90f;
		const QVector3D mesh_center = (mesh_.mins + mesh_.maxs) * 0.5f;
		const float bx0 = mesh_center.x() - kPlayerHalfWidth;
		const float bx1 = mesh_center.x() + kPlayerHalfWidth;
		const float by0 = mesh_center.y() - kPlayerHalfWidth;
		const float by1 = mesh_center.y() + kPlayerHalfWidth;
		const float bz0 = z;
		const float bz1 = z + kPlayerHeight;

		push_line3(bx0, by0, bz0, bx1, by0, bz0, axis_x, kPlayerAlpha);
		push_line3(bx1, by0, bz0, bx1, by1, bz0, axis_y, kPlayerAlpha);
		push_line3(bx1, by1, bz0, bx0, by1, bz0, axis_x, kPlayerAlpha);
		push_line3(bx0, by1, bz0, bx0, by0, bz0, axis_y, kPlayerAlpha);

		push_line3(bx0, by0, bz1, bx1, by0, bz1, axis_x, kPlayerAlpha);
		push_line3(bx1, by0, bz1, bx1, by1, bz1, axis_y, kPlayerAlpha);
		push_line3(bx1, by1, bz1, bx0, by1, bz1, axis_x, kPlayerAlpha);
		push_line3(bx0, by1, bz1, bx0, by0, bz1, axis_y, kPlayerAlpha);

		push_line3(bx0, by0, bz0, bx0, by0, bz1, grid_color, kPlayerAlpha);
		push_line3(bx1, by0, bz0, bx1, by0, bz1, grid_color, kPlayerAlpha);
		push_line3(bx1, by1, bz0, bx1, by1, bz1, grid_color, kPlayerAlpha);
		push_line3(bx0, by1, bz0, bx0, by1, bz1, grid_color, kPlayerAlpha);
	}

	if (grid_vbuf_) {
		delete grid_vbuf_;
		grid_vbuf_ = nullptr;
	}
	grid_vertex_count_ = 0;
	if (verts.isEmpty()) {
		return;
	}

	grid_vbuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
								  verts.size() * sizeof(GridLineVertex));
	grid_vbuf_->create();
	updates->uploadStaticBuffer(grid_vbuf_, verts.constData());
	grid_vertex_count_ = verts.size();
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
	bg_vertices_.push_back(GpuVertex{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
	bg_vertices_.push_back(GpuVertex{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
	bg_vertices_.push_back(GpuVertex{-1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f});

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
	const float reference = std::max(distance_, radius_ * 0.25f);
	grid_scale_ = quantized_grid_scale(reference);
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
		s.has_lightmap = false;
	}
	for (QRhiTexture*& lm : lightmap_textures_) {
		if (lm) {
			delete lm;
			lm = nullptr;
		}
	}
	lightmap_textures_.clear();
	if (default_srb_) {
		delete default_srb_;
		default_srb_ = nullptr;
	}
	if (grid_srb_) {
		delete grid_srb_;
		grid_srb_ = nullptr;
	}
	index_count_ = 0;
	ground_index_count_ = 0;
	grid_vertex_count_ = 0;
	grid_line_step_ = 0.0f;
	grid_line_center_i_ = 0;
	grid_line_center_j_ = 0;
	grid_line_half_lines_ = 0;
	grid_line_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	axis_x_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	axis_y_color_cached_ = QVector3D(0.0f, 0.0f, 0.0f);
	if (grid_vbuf_) {
		delete grid_vbuf_;
		grid_vbuf_ = nullptr;
	}
}

void BspPreviewVulkanWidget::destroy_pipeline_resources() {
	if (pipeline_) {
		delete pipeline_;
		pipeline_ = nullptr;
	}
	if (grid_pipeline_) {
		delete grid_pipeline_;
		grid_pipeline_ = nullptr;
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
	if (grid_pipeline_) {
		delete grid_pipeline_;
		grid_pipeline_ = nullptr;
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
		QRhiVertexInputAttribute(0, 4, QRhiVertexInputAttribute::Float2, offsetof(GpuVertex, lu)),
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

	if (grid_vert_shader_.isValid() && grid_frag_shader_.isValid()) {
		if (!grid_srb_) {
			if (!ubuf_) {
				ensure_uniform_buffer(1);
			}
			grid_srb_ = rhi()->newShaderResourceBindings();
			grid_srb_->setBindings({
				QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
																		 QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
																		 ubuf_,
																		 sizeof(UniformBlock)),
			});
			grid_srb_->create();
		}

		grid_pipeline_ = rhi()->newGraphicsPipeline();
		grid_pipeline_->setShaderStages({
			{QRhiShaderStage::Vertex, grid_vert_shader_},
			{QRhiShaderStage::Fragment, grid_frag_shader_},
		});

		QRhiVertexInputLayout grid_input_layout;
		grid_input_layout.setBindings({
			QRhiVertexInputBinding(sizeof(GridLineVertex)),
		});
		grid_input_layout.setAttributes({
			QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, offsetof(GridLineVertex, px)),
			QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float4, offsetof(GridLineVertex, r)),
		});
		grid_pipeline_->setVertexInputLayout(grid_input_layout);
		grid_pipeline_->setShaderResourceBindings(grid_srb_);
		grid_pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
		grid_pipeline_->setDepthTest(true);
		grid_pipeline_->setDepthWrite(false);
		grid_pipeline_->setCullMode(QRhiGraphicsPipeline::None);
		grid_pipeline_->setSampleCount(sampleCount());
		grid_pipeline_->setTopology(QRhiGraphicsPipeline::Lines);
		QRhiGraphicsPipeline::TargetBlend blend;
		blend.enable = true;
		blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
		blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
		blend.opColor = QRhiGraphicsPipeline::Add;
		blend.srcAlpha = QRhiGraphicsPipeline::One;
		blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
		blend.opAlpha = QRhiGraphicsPipeline::Add;
		grid_pipeline_->setTargetBlends({blend});
		grid_pipeline_->create();
	}

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
			QImage white(1, 1, QImage::Format_RGBA8888);
			white.fill(Qt::white);
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
		QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, white_tex_, sampler_),
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
	if (grid_srb_) {
		delete grid_srb_;
		grid_srb_ = nullptr;
	}
	for (DrawSurface& s : surfaces_) {
		if (s.srb) {
			delete s.srb;
			s.srb = nullptr;
		}
	}
	pending_texture_upload_ = has_mesh_;
	pipeline_dirty_ = true;
}
