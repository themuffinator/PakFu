#include "ui/model_viewer_vulkan_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QSettings>
#include <QWheelEvent>
#include <QFocusEvent>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include "formats/image_loader.h"
#include "formats/model.h"
#include "formats/quake3_skin.h"

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
		qWarning() << "ModelViewerVulkanWidget: unable to open shader" << path;
		return {};
	}
	const QByteArray data = f.readAll();
	QShader shader = QShader::fromSerialized(data);
	if (!shader.isValid()) {
		qWarning() << "ModelViewerVulkanWidget: invalid shader" << path;
	}
	return shader;
}

quint32 aligned_uniform_stride(QRhi* rhi, quint32 size) {
	const quint32 align = rhi ? rhi->ubufAlignment() : 256u;
	return (size + align - 1u) & ~(align - 1u);
}
}  // namespace

ModelViewerVulkanWidget::ModelViewerVulkanWidget(QWidget* parent) : QRhiWidget(parent) {
	setApi(QRhiWidget::Api::Vulkan);
	setMinimumHeight(240);
	setFocusPolicy(Qt::StrongFocus);
	fly_timer_.setInterval(16);
	fly_timer_.setTimerType(Qt::PreciseTimer);
	connect(&fly_timer_, &QTimer::timeout, this, &ModelViewerVulkanWidget::on_fly_tick);
	setToolTip(
		"3D Controls:\n"
		"- Orbit: Middle-drag (Alt+Left-drag)\n"
		"- Pan: Shift+Middle-drag (Alt+Shift+Left-drag)\n"
		"- Dolly: Ctrl+Middle-drag (Alt+Ctrl+Left-drag)\n"
		"- Zoom: Mouse wheel\n"
		"- Fly: Hold Right Mouse + WASD (Q/E up/down, wheel adjusts speed, Shift faster, Ctrl slower)\n"
		"- Frame: F\n"
		"- Reset: R / Home");

	QSettings settings;
	texture_smoothing_ = settings.value("preview/model/textureSmoothing", false).toBool();
}

ModelViewerVulkanWidget::~ModelViewerVulkanWidget() {
	releaseResources();
}

void ModelViewerVulkanWidget::set_texture_smoothing(bool enabled) {
	if (texture_smoothing_ == enabled) {
		return;
	}
	texture_smoothing_ = enabled;
	pending_texture_upload_ = has_model();
	update();
}

void ModelViewerVulkanWidget::set_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette) {
	quake1_palette_ = quake1_palette;
	quake2_palette_ = quake2_palette;
}

void ModelViewerVulkanWidget::set_grid_mode(PreviewGridMode mode) {
	if (grid_mode_ == mode) {
		return;
	}
	grid_mode_ = mode;
	pending_ground_upload_ = true;
	update();
}

void ModelViewerVulkanWidget::set_background_mode(PreviewBackgroundMode mode, const QColor& custom_color) {
	if (bg_mode_ == mode && bg_custom_color_ == custom_color) {
		return;
	}
	bg_mode_ = mode;
	bg_custom_color_ = custom_color;
	update();
}

void ModelViewerVulkanWidget::set_wireframe_enabled(bool enabled) {
	if (wireframe_enabled_ == enabled) {
		return;
	}
	wireframe_enabled_ = enabled;
	pipeline_dirty_ = true;
	update();
}

void ModelViewerVulkanWidget::set_textured_enabled(bool enabled) {
	if (textured_enabled_ == enabled) {
		return;
	}
	textured_enabled_ = enabled;
	update();
}

void ModelViewerVulkanWidget::set_glow_enabled(bool enabled) {
	if (glow_enabled_ == enabled) {
		return;
	}
	glow_enabled_ = enabled;
	if (model_ && !last_model_path_.isEmpty()) {
		QString err;
		(void)load_file(last_model_path_, last_skin_path_, &err);
		return;
	}
	update();
}

void ModelViewerVulkanWidget::set_fov_degrees(int degrees) {
	const float clamped = std::clamp(static_cast<float>(degrees), 40.0f, 120.0f);
	if (std::abs(clamped - fov_y_deg_) < 0.001f) {
		return;
	}
	fov_y_deg_ = clamped;
	pending_ground_upload_ = true;
	update();
}

PreviewCameraState ModelViewerVulkanWidget::camera_state() const {
	PreviewCameraState state;
	state.center = center_;
	state.yaw_deg = yaw_deg_;
	state.pitch_deg = pitch_deg_;
	state.distance = distance_;
	state.valid = true;
	return state;
}

void ModelViewerVulkanWidget::set_camera_state(const PreviewCameraState& state) {
	if (!state.valid) {
		return;
	}
	center_ = state.center;
	yaw_deg_ = std::remainder(state.yaw_deg, 360.0f);
	pitch_deg_ = std::clamp(state.pitch_deg, -89.0f, 89.0f);
	distance_ = std::clamp(state.distance, orbit_min_distance(radius_), orbit_max_distance(radius_));
	pending_ground_upload_ = true;
	update();
}

bool ModelViewerVulkanWidget::load_file(const QString& file_path, QString* error) {
	return load_file(file_path, QString(), error);
}

bool ModelViewerVulkanWidget::load_file(const QString& file_path, const QString& skin_path, QString* error) {
	if (error) {
		error->clear();
	}

	const QFileInfo skin_info(skin_path);
	const bool skin_is_q3_skin =
		(!skin_path.isEmpty() && skin_info.suffix().compare("skin", Qt::CaseInsensitive) == 0);
	Quake3SkinMapping skin_mapping;
	if (skin_is_q3_skin) {
		QString skin_err;
		if (!parse_quake3_skin_file(skin_path, &skin_mapping, &skin_err)) {
			if (error) {
				*error = skin_err.isEmpty() ? "Unable to load .skin file." : skin_err;
			}
			unload();
			return false;
		}
	}

	const auto decode_options_for = [&](const QString& path) -> ImageDecodeOptions {
		ImageDecodeOptions opt;
		const QString leaf = QFileInfo(path).fileName();
		const QString ext = QFileInfo(leaf).suffix().toLower();
		if ((ext == "lmp" || ext == "mip") && quake1_palette_.size() == 256) {
			opt.palette = &quake1_palette_;
		} else if (ext == "wal" && quake2_palette_.size() == 256) {
			opt.palette = &quake2_palette_;
		}
		return opt;
	};

	const auto glow_path_for = [&](const QString& base_path) -> QString {
		if (base_path.isEmpty() || !glow_enabled_) {
			return {};
		}
		const QFileInfo fi(base_path);
		const QString base = fi.completeBaseName();
		if (base.isEmpty()) {
			return {};
		}
		return QDir(fi.absolutePath()).filePath(QString("%1_glow.png").arg(base));
	};

	const auto load_glow_for = [&](const QString& base_path) -> QImage {
		const QString glow_path = glow_path_for(base_path);
		if (glow_path.isEmpty() || !QFileInfo::exists(glow_path)) {
			return {};
		}
		const ImageDecodeResult decoded = decode_image_file(glow_path, ImageDecodeOptions{});
		return decoded.ok() ? decoded.image : QImage();
	};

	const auto decode_embedded_skin = [&](const LoadedModel& model) -> QImage {
		if (model.embedded_skin_width <= 0 || model.embedded_skin_height <= 0 || model.embedded_skin_indices.isEmpty()) {
			if (model.embedded_skin_width <= 0 || model.embedded_skin_height <= 0 || model.embedded_skin_rgba.isEmpty()) {
				return {};
			}
		}
		const qint64 pixel_count =
			static_cast<qint64>(model.embedded_skin_width) * static_cast<qint64>(model.embedded_skin_height);
		if (pixel_count <= 0) {
			return {};
		}
		QImage img(model.embedded_skin_width, model.embedded_skin_height, QImage::Format_ARGB32);
		if (img.isNull()) {
			return {};
		}
		const qint64 rgba_bytes = pixel_count * 4;
		if (rgba_bytes > 0 && rgba_bytes <= model.embedded_skin_rgba.size()) {
			const auto* src = reinterpret_cast<const unsigned char*>(model.embedded_skin_rgba.constData());
			for (int y = 0; y < model.embedded_skin_height; ++y) {
				QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
				const qint64 row_off = static_cast<qint64>(y) * static_cast<qint64>(model.embedded_skin_width) * 4;
				for (int x = 0; x < model.embedded_skin_width; ++x) {
					const qint64 px_off = row_off + static_cast<qint64>(x) * 4;
					row[x] = qRgba(src[px_off + 0], src[px_off + 1], src[px_off + 2], src[px_off + 3]);
				}
			}
			return img;
		}
		if (pixel_count > model.embedded_skin_indices.size()) {
			return {};
		}
		const bool has_palette = (quake1_palette_.size() == 256);
		const auto* src = reinterpret_cast<const unsigned char*>(model.embedded_skin_indices.constData());
		for (int y = 0; y < model.embedded_skin_height; ++y) {
			QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
			const qint64 row_off = static_cast<qint64>(y) * static_cast<qint64>(model.embedded_skin_width);
			for (int x = 0; x < model.embedded_skin_width; ++x) {
				const unsigned char idx = src[row_off + x];
				if (has_palette) {
					row[x] = quake1_palette_[idx];
				} else {
					row[x] = qRgba(idx, idx, idx, 255);
				}
			}
		}
		return img;
	};

	const auto decode_embedded_texture = [&](const EmbeddedTexture& tex) -> QImage {
		const qint64 pixel_count = static_cast<qint64>(tex.width) * static_cast<qint64>(tex.height);
		if (tex.width <= 0 || tex.height <= 0 || pixel_count <= 0) {
			return {};
		}
		if (tex.rgba.size() != pixel_count * 4) {
			return {};
		}
		QImage img(tex.width, tex.height, QImage::Format_ARGB32);
		if (img.isNull()) {
			return {};
		}
		const auto* src = reinterpret_cast<const unsigned char*>(tex.rgba.constData());
		for (int y = 0; y < tex.height; ++y) {
			QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
			const qint64 row_off = static_cast<qint64>(y) * static_cast<qint64>(tex.width) * 4;
			for (int x = 0; x < tex.width; ++x) {
				const qint64 px_off = row_off + static_cast<qint64>(x) * 4;
				row[x] = qRgba(src[px_off + 0], src[px_off + 1], src[px_off + 2], src[px_off + 3]);
			}
		}
		return img;
	};

	const auto parse_texture_slot = [](QString shader_ref) -> int {
		shader_ref = shader_ref.trimmed();
		if (!shader_ref.startsWith("texture_", Qt::CaseInsensitive)) {
			return -1;
		}
		bool ok = false;
		const int idx = shader_ref.mid(8).toInt(&ok);
		return (ok && idx >= 0) ? idx : -1;
	};

	const auto apply_embedded_surface_textures = [&]() {
		if (!model_ || model_->embedded_textures.isEmpty() || surfaces_.isEmpty()) {
			return;
		}

		QVector<QImage> decoded;
		decoded.resize(model_->embedded_textures.size());
		QHash<QString, int> by_name;
		by_name.reserve(model_->embedded_textures.size() * 2);
		for (int i = 0; i < model_->embedded_textures.size(); ++i) {
			const EmbeddedTexture& tex = model_->embedded_textures[i];
			decoded[i] = decode_embedded_texture(tex);
			if (decoded[i].isNull()) {
				continue;
			}

			QString key = tex.name.trimmed();
			key.replace('\\', '/');
			while (key.startsWith('/')) {
				key.remove(0, 1);
			}
			if (key.isEmpty()) {
				continue;
			}
			by_name.insert(key.toLower(), i);
			const QString leaf = QFileInfo(key).fileName().toLower();
			if (!leaf.isEmpty()) {
				by_name.insert(leaf, i);
			}
		}

		for (DrawSurface& s : surfaces_) {
			int tex_idx = -1;

			const int idx_from_hint = parse_texture_slot(s.shader_hint);
			if (idx_from_hint >= 0 && idx_from_hint < decoded.size()) {
				tex_idx = idx_from_hint;
			}
			if (tex_idx < 0) {
				const int idx_from_leaf = parse_texture_slot(s.shader_leaf);
				if (idx_from_leaf >= 0 && idx_from_leaf < decoded.size()) {
					tex_idx = idx_from_leaf;
				}
			}
			if (tex_idx < 0) {
				QString key = s.shader_hint.trimmed();
				key.replace('\\', '/');
				while (key.startsWith('/')) {
					key.remove(0, 1);
				}
				if (!key.isEmpty()) {
					tex_idx = by_name.value(key.toLower(), -1);
				}
			}
			if (tex_idx < 0) {
				QString key = s.shader_leaf.trimmed();
				key.replace('\\', '/');
				while (key.startsWith('/')) {
					key.remove(0, 1);
				}
				if (!key.isEmpty()) {
					tex_idx = by_name.value(key.toLower(), -1);
				}
			}

			if (tex_idx < 0 || tex_idx >= decoded.size() || decoded[tex_idx].isNull()) {
				continue;
			}
			s.image = decoded[tex_idx];
			if (s.shader_hint.isEmpty()) {
				s.shader_hint = model_->embedded_textures[tex_idx].name;
			}
			if (s.shader_leaf.isEmpty()) {
				s.shader_leaf = QFileInfo(model_->embedded_textures[tex_idx].name).fileName();
			}
		}
	};

	QString err;
	model_ = load_model_file(file_path, &err);
	if (!model_) {
		if (error) {
			*error = err.isEmpty() ? "Unable to load model." : err;
		}
		unload();
		return false;
	}
	last_model_path_ = file_path;
	last_skin_path_ = skin_path;
	const QFileInfo model_info(file_path);
	const QString model_dir = model_info.absolutePath();
	const QString model_base = model_info.completeBaseName();
	const QString model_format = model_->format.toLower();

	const auto score_auto_skin = [&](const QFileInfo& fi) -> int {
		const QString ext = fi.suffix().toLower();
		if (ext.isEmpty()) {
			return std::numeric_limits<int>::min();
		}
		const QString base = fi.completeBaseName();
		const QString base_lower = base.toLower();
		const QString model_base_lower = model_base.toLower();

		int score = 0;
		if (!model_base_lower.isEmpty()) {
			if (base_lower == model_base_lower) {
				score += 140;
			} else if (base_lower.startsWith(model_base_lower + "_")) {
				score += 95;
			}
		}
		if (base_lower == "skin") {
			score += 80;
		}
		if (base_lower.contains("default")) {
			score += 30;
		}
		if (base_lower.endsWith("_glow")) {
			score -= 200;
		}

		if (model_format == "mdl" && !model_base_lower.isEmpty()) {
			const QString mdl_prefix = model_base_lower + "_";
			if (base_lower == model_base_lower + "_00_00") {
				score += 220;
			} else if (base_lower.startsWith(mdl_prefix)) {
				const QString suffix = base_lower.mid(mdl_prefix.size());
				const bool two_by_two_numeric = (suffix.size() == 5 && suffix[2] == '_' && suffix[0].isDigit() &&
												 suffix[1].isDigit() && suffix[3].isDigit() && suffix[4].isDigit());
				score += two_by_two_numeric ? 180 : 120;
			}
		}

		if (ext == "png") {
			score += 20;
		} else if (ext == "tga") {
			score += 18;
		} else if (ext == "jpg" || ext == "jpeg") {
			score += 16;
		} else if (ext == "ftx") {
			score += 21;
		} else if (ext == "pcx") {
			score += 14;
		} else if (ext == "wal") {
			score += 12;
		} else if (ext == "swl") {
			score += 12;
		} else if (ext == "dds") {
			score += 10;
		} else if (ext == "lmp") {
			score += (model_format == "mdl") ? 26 : 12;
		} else if (ext == "mip") {
			score += (model_format == "mdl") ? 24 : 11;
		} else {
			score -= 1000;
		}
		return score;
	};

	const auto find_auto_skin_on_disk = [&]() -> QString {
		if (model_dir.isEmpty()) {
			return {};
		}
		QDir d(model_dir);
		if (!d.exists()) {
			return {};
		}
		const QStringList files = d.entryList(QStringList() << "*.png"
													<< "*.tga"
													<< "*.jpg"
													<< "*.jpeg"
													<< "*.pcx"
													<< "*.wal"
													<< "*.swl"
													<< "*.dds"
													<< "*.lmp"
													<< "*.mip"
													<< "*.ftx",
									QDir::Files,
									QDir::Name);
		QString best_name;
		int best_score = std::numeric_limits<int>::min();
		for (const QString& name : files) {
			const int score = score_auto_skin(QFileInfo(name));
			if (score > best_score || (score == best_score && name.compare(best_name, Qt::CaseInsensitive) < 0)) {
				best_score = score;
				best_name = name;
			}
		}
		if (best_score < 40) {
			return {};
		}
		return best_name.isEmpty() ? QString() : d.filePath(best_name);
	};

	const auto try_apply_skin = [&](const QString& candidate_path) -> bool {
		if (candidate_path.isEmpty()) {
			return false;
		}
		const ImageDecodeResult decoded = decode_image_file(candidate_path, decode_options_for(candidate_path));
		if (!decoded.ok()) {
			return false;
		}
		skin_image_ = decoded.image;
		if (glow_enabled_) {
			skin_glow_image_ = load_glow_for(candidate_path);
		}
		last_skin_path_ = candidate_path;
		return !skin_image_.isNull();
	};

	surfaces_.clear();
	const int total_indices = model_->mesh.indices.size();
	if (model_->surfaces.isEmpty()) {
		DrawSurface s;
		s.first_index = 0;
		s.index_count = total_indices;
		s.name = "model";
		surfaces_.push_back(std::move(s));
	} else {
		surfaces_.reserve(model_->surfaces.size());
		for (const ModelSurface& ms : model_->surfaces) {
			const qint64 first = ms.first_index;
			const qint64 count = ms.index_count;
			if (first < 0 || count <= 0 || first >= total_indices || (first + count) > total_indices) {
				continue;
			}
			DrawSurface s;
			s.first_index = static_cast<int>(first);
			s.index_count = static_cast<int>(count);
			s.name = ms.name;
			s.shader_hint = ms.shader;
			s.shader_leaf = QFileInfo(ms.shader).fileName();
			surfaces_.push_back(std::move(s));
		}
		if (surfaces_.isEmpty()) {
			DrawSurface s;
			s.first_index = 0;
			s.index_count = total_indices;
			s.name = "model";
			surfaces_.push_back(std::move(s));
		}
	}

	skin_image_ = {};
	skin_glow_image_ = {};
	has_texture_ = false;
	has_glow_ = false;
	pending_texture_upload_ = false;
	if (!skin_is_q3_skin && !skin_path.isEmpty()) {
		(void)try_apply_skin(skin_path);
	}
	if (skin_image_.isNull() && !skin_is_q3_skin) {
		const QString auto_skin = find_auto_skin_on_disk();
		(void)try_apply_skin(auto_skin);
	}
	if (skin_image_.isNull() && model_) {
		skin_image_ = decode_embedded_skin(*model_);
	}

	if (skin_is_q3_skin && !skin_mapping.surface_to_shader.isEmpty()) {
		for (DrawSurface& s : surfaces_) {
			const QString key = s.name.trimmed().toLower();
			if (!skin_mapping.surface_to_shader.contains(key)) {
				continue;
			}
			const QString shader = skin_mapping.surface_to_shader.value(key).trimmed();
			s.shader_hint = shader;
			s.shader_leaf = shader.isEmpty() ? QString() : QFileInfo(shader).fileName();
			s.image = {};
			s.glow_image = {};
		}
	}

	apply_embedded_surface_textures();

	if (!model_dir.isEmpty()) {
		const QStringList exts = {"png", "tga", "jpg", "jpeg", "pcx", "wal", "swl", "dds", "lmp", "mip", "ftx"};

		const auto try_find_in_dir = [&](const QString& base_or_file) -> QString {
			if (base_or_file.isEmpty()) {
				return {};
			}
			const QFileInfo fi(base_or_file);
			const QString base = fi.completeBaseName();
			const QString file = fi.fileName();
			if (!file.isEmpty() && QFileInfo::exists(QDir(model_dir).filePath(file))) {
				return QDir(model_dir).filePath(file);
			}
			if (!base.isEmpty()) {
				for (const QString& ext : exts) {
					const QString cand = QDir(model_dir).filePath(QString("%1.%2").arg(base, ext));
					if (QFileInfo::exists(cand)) {
						return cand;
					}
				}
			}
			// Case-insensitive basename match (helps when extracted filenames differ in case).
			QDir d(model_dir);
			const QStringList files = d.entryList(QStringList() << "*.png"
																<< "*.tga"
																<< "*.jpg"
																<< "*.jpeg"
																<< "*.pcx"
																<< "*.wal"
																<< "*.swl"
																<< "*.dds"
																<< "*.lmp"
																<< "*.mip"
																<< "*.ftx",
													QDir::Files);
			const QString want = base.toLower();
			for (const QString& f : files) {
				if (QFileInfo(f).completeBaseName().toLower() == want) {
					return QDir(model_dir).filePath(f);
				}
			}
			return {};
		};

		// Try to resolve per-surface textures.
		for (DrawSurface& s : surfaces_) {
			if (s.shader_leaf.isEmpty()) {
				continue;
			}
			const QString found = try_find_in_dir(s.shader_leaf);
			if (found.isEmpty()) {
				continue;
			}
			const ImageDecodeResult decoded = decode_image_file(found, decode_options_for(found));
			if (decoded.ok()) {
				s.image = decoded.image;
				if (glow_enabled_) {
					s.glow_image = load_glow_for(found);
				}
			}
		}
	}

	pending_upload_ = true;
	pending_texture_upload_ = true;
	reset_camera_from_mesh();
	update();
	return true;
}

void ModelViewerVulkanWidget::unload() {
	model_.reset();
	last_model_path_.clear();
	last_skin_path_.clear();
	pending_upload_ = false;
	pending_texture_upload_ = false;
	surfaces_.clear();
	skin_image_ = {};
	skin_glow_image_ = {};
	has_texture_ = false;
	has_glow_ = false;
	reset_camera_from_mesh();
	destroy_mesh_resources();
	update();
}

void ModelViewerVulkanWidget::initialize(QRhiCommandBuffer*) {
	vert_shader_ = load_shader(":/assets/shaders/model_preview.vert.qsb");
	frag_shader_ = load_shader(":/assets/shaders/model_preview.frag.qsb");
	grid_vert_shader_ = load_shader(":/assets/shaders/grid_lines.vert.qsb");
	grid_frag_shader_ = load_shader(":/assets/shaders/grid_lines.frag.qsb");

	if (rhi()) {
		rebuild_sampler();
		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
		black_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		black_tex_->create();
	}

	ensure_pipeline();
}

void ModelViewerVulkanWidget::render(QRhiCommandBuffer* cb) {
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
	if (model_) {
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
	const bool draw_surfaces = (model_ && index_count_ > 0 && vbuf_ && ibuf_);
	const int surface_count = draw_surfaces ? (surfaces_.isEmpty() ? 1 : surfaces_.size()) : 0;

	const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
	const QVector3D dir = spherical_dir(yaw_deg_, pitch_deg_).normalized();
	const QVector3D cam_pos = center_ + dir * distance_;
	const QVector3D view_target = center_;
	QVector3D scene_center = center_;
	if (model_) {
		scene_center = (model_->mesh.mins + model_->mesh.maxs) * 0.5f;
	}
	const float dist_to_scene = (cam_pos - scene_center).length();

	const float near_plane = std::clamp(radius_ * 0.0005f, 0.05f, 16.0f);
	const float far_plane = std::max(near_plane + 10.0f, dist_to_scene + radius_ * 3.0f);

	QMatrix4x4 proj;
	proj.perspective(fov_y_deg_, aspect, near_plane, far_plane);

	QMatrix4x4 view;
	view.lookAt(cam_pos, view_target, QVector3D(0, 0, 1));

	QMatrix4x4 model_m;
	model_m.setToIdentity();

	const QMatrix4x4 mvp = rhi()->clipSpaceCorrMatrix() * proj * view * model_m;
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

	if (grid_mode_ == PreviewGridMode::Grid && model_) {
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

	auto write_uniform = [&](int i, bool has_tex, bool has_glow, bool is_ground, bool is_background) {
		UniformBlock u;
		u.mvp = is_background ? bg_mvp : mvp;
		u.model = model_m;
		u.cam_pos = QVector4D(cam_pos, 0.0f);
		u.light_dir = QVector4D(0.4f, 0.25f, 1.0f, 0.0f);
		u.fill_dir = QVector4D(-0.65f, -0.15f, 0.8f, 0.0f);
		u.base_color = QVector4D(0.75f, 0.78f, 0.82f, 0.0f);
		u.ground_color = QVector4D(bg_base, 0.0f);
		u.shadow_center = QVector4D(center_.x(), center_.y(), ground_z_, 0.0f);
		u.shadow_params = QVector4D(std::max(0.05f, radius_ * 1.45f), 0.55f, 2.4f, is_ground ? 1.0f : 0.0f);
		u.grid_params = QVector4D(0.0f, grid_scale_, 0.0f, 0.0f);
		u.grid_color = QVector4D(grid_color, 0.0f);
		u.axis_color_x = QVector4D(axis_x, 0.0f);
		u.axis_color_y = QVector4D(axis_y, 0.0f);
		u.bg_top = QVector4D(bg_top, 0.0f);
		u.bg_bottom = QVector4D(bg_bottom, 0.0f);
		u.misc = QVector4D(has_tex ? 1.0f : 0.0f, has_glow ? 1.0f : 0.0f, is_background ? 1.0f : 0.0f, 0.0f);
		std::memcpy(udata.data() + static_cast<int>(i * ubuf_stride_), &u, sizeof(UniformBlock));
	};

	int uidx = 0;
	write_uniform(uidx++, false, false, false, true);
	if (draw_ground) {
		write_uniform(uidx++, false, false, true, false);
	}
	if (draw_grid) {
		write_uniform(uidx++, false, false, false, false);
	}

	if (draw_surfaces) {
		if (surfaces_.isEmpty()) {
			const bool has_tex = textured_enabled_ && (skin_texture_ != nullptr);
			const bool has_glow = textured_enabled_ && (skin_glow_texture_ != nullptr);
			write_uniform(uidx++, has_tex, has_glow, false, false);
		} else {
			for (const DrawSurface& s : surfaces_) {
				const bool has_tex = textured_enabled_ && ((s.texture_handle != nullptr) || (skin_texture_ != nullptr));
				const bool has_glow = textured_enabled_ && ((s.glow_texture_handle != nullptr) || (skin_glow_texture_ != nullptr));
				write_uniform(uidx++, has_tex, has_glow, false, false);
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
		int surface_idx = 0;
		for (const DrawSurface& s : surfaces_) {
			const quint32 offset = ubuf_stride_ * static_cast<quint32>(base_offset + surface_idx);
			QRhiCommandBuffer::DynamicOffset dyn = {0, offset};
			QRhiShaderResourceBindings* srb = default_srb_;
			if (s.srb) {
				srb = s.srb;
			} else if (skin_srb_) {
				srb = skin_srb_;
			}
			cb->setShaderResources(srb, 1, &dyn);
			const qint64 first = s.first_index;
			const qint64 count = s.index_count;
			if (first < 0 || count <= 0 || first >= index_count_ || (first + count) > index_count_) {
				++surface_idx;
				continue;
			}
			cb->drawIndexed(static_cast<quint32>(count), 1, static_cast<quint32>(first), 0, 0);
			++surface_idx;
		}
	}

	cb->endPass();
}

void ModelViewerVulkanWidget::releaseResources() {
	destroy_mesh_resources();
	destroy_pipeline_resources();
	vert_shader_ = {};
	frag_shader_ = {};
	grid_vert_shader_ = {};
	grid_frag_shader_ = {};
}

void ModelViewerVulkanWidget::resizeEvent(QResizeEvent* event) {
	QRhiWidget::resizeEvent(event);
	pipeline_dirty_ = true;
	update();
}

void ModelViewerVulkanWidget::mousePressEvent(QMouseEvent* event) {
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

void ModelViewerVulkanWidget::mouseMoveEvent(QMouseEvent* event) {
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
		pending_ground_upload_ = true;
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

void ModelViewerVulkanWidget::mouseReleaseEvent(QMouseEvent* event) {
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

void ModelViewerVulkanWidget::wheelEvent(QWheelEvent* event) {
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

void ModelViewerVulkanWidget::keyPressEvent(QKeyEvent* event) {
	if (!event) {
		QRhiWidget::keyPressEvent(event);
		return;
	}

	if (event->key() == Qt::Key_F) {
		frame_mesh();
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

void ModelViewerVulkanWidget::keyReleaseEvent(QKeyEvent* event) {
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

void ModelViewerVulkanWidget::focusOutEvent(QFocusEvent* event) {
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

void ModelViewerVulkanWidget::reset_camera_from_mesh() {
	yaw_deg_ = 45.0f;
	pitch_deg_ = 20.0f;
	if (!model_) {
		center_ = QVector3D(0, 0, 0);
		radius_ = 1.0f;
		distance_ = 3.0f;
		ground_z_ = 0.0f;
	} else {
		center_ = (model_->mesh.mins + model_->mesh.maxs) * 0.5f;
		const QVector3D half_extents = (model_->mesh.maxs - model_->mesh.mins) * 0.5f;
		radius_ = std::max(0.01f, half_extents.length());
		const float aspect = (height() > 0) ? (static_cast<float>(width()) / static_cast<float>(height())) : 1.0f;
		const QVector3D view_forward = (-spherical_dir(yaw_deg_, pitch_deg_)).normalized();
		const float fit_dist = fit_distance_for_aabb(half_extents, view_forward, aspect, fov_y_deg_);
		distance_ = std::clamp(fit_dist * 1.05f, orbit_min_distance(radius_), orbit_max_distance(radius_));
		ground_z_ = model_->mesh.mins.z() - ground_pad(radius_);
	}
	fly_speed_ = std::clamp(std::max(640.0f, radius_ * 0.25f), kFlySpeedMin, kFlySpeedMax);
	pending_ground_upload_ = true;
}

void ModelViewerVulkanWidget::frame_mesh() {
	reset_camera_from_mesh();
	update();
}

void ModelViewerVulkanWidget::pan_by_pixels(const QPoint& delta) {
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

void ModelViewerVulkanWidget::dolly_by_pixels(const QPoint& delta) {
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

void ModelViewerVulkanWidget::on_fly_tick() {
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
	pipeline_dirty_ = true;
	update();
}

void ModelViewerVulkanWidget::set_fly_key(int key, bool down) {
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

void ModelViewerVulkanWidget::upload_mesh(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi() || !model_) {
		return;
	}

	QVector<GpuVertex> gpu;
	gpu.resize(model_->mesh.vertices.size());
	for (int i = 0; i < model_->mesh.vertices.size(); ++i) {
		const ModelVertex& v = model_->mesh.vertices[i];
		gpu[i] = GpuVertex{v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v};
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
							  model_->mesh.indices.size() * sizeof(std::uint32_t));
	ibuf_->create();

	updates->uploadStaticBuffer(vbuf_, gpu.constData());
	updates->uploadStaticBuffer(ibuf_, model_->mesh.indices.constData());

	index_count_ = model_->mesh.indices.size();
}

void ModelViewerVulkanWidget::upload_textures(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi()) {
		return;
	}

	rebuild_sampler();

	for (DrawSurface& s : surfaces_) {
		if (s.texture_handle) {
			delete s.texture_handle;
			s.texture_handle = nullptr;
		}
		if (s.glow_texture_handle) {
			delete s.glow_texture_handle;
			s.glow_texture_handle = nullptr;
		}
		if (s.srb) {
			delete s.srb;
			s.srb = nullptr;
		}
		s.has_texture = false;
		s.has_glow = false;
	}

	if (skin_texture_) {
		delete skin_texture_;
		skin_texture_ = nullptr;
	}
	if (skin_glow_texture_) {
		delete skin_glow_texture_;
		skin_glow_texture_ = nullptr;
	}
	if (skin_srb_) {
		delete skin_srb_;
		skin_srb_ = nullptr;
	}

	if (default_srb_) {
		delete default_srb_;
		default_srb_ = nullptr;
	}
	if (ground_srb_) {
		delete ground_srb_;
		ground_srb_ = nullptr;
	}

	ensure_uniform_buffer(std::max(1, static_cast<int>(surfaces_.size()) + 1));

	if (!white_tex_) {
		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
	}
	const QImage white(1, 1, QImage::Format_RGBA8888);
	updates->uploadTexture(white_tex_, white);
	if (!black_tex_) {
		black_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		black_tex_->create();
	}
	QImage black(1, 1, QImage::Format_RGBA8888);
	black.fill(Qt::black);
	updates->uploadTexture(black_tex_, black);

	ensure_default_srb(updates);

	auto upload = [&](const QImage& src, QRhiTexture** out_tex) -> bool {
		if (!out_tex) {
			return false;
		}
		*out_tex = nullptr;
		if (src.isNull()) {
			return false;
		}
		QImage img = src.convertToFormat(QImage::Format_RGBA8888).flipped(Qt::Vertical);
		if (img.isNull()) {
			return false;
		}
		QRhiTexture* tex = rhi()->newTexture(QRhiTexture::RGBA8, img.size(), 1);
		tex->create();
		updates->uploadTexture(tex, img);
		*out_tex = tex;
		return true;
	};

	if (upload(skin_image_, &skin_texture_)) {
		has_texture_ = true;
	} else {
		has_texture_ = false;
	}
	if (upload(skin_glow_image_, &skin_glow_texture_)) {
		has_glow_ = true;
	} else {
		has_glow_ = false;
	}

	if (skin_texture_ || skin_glow_texture_) {
		QRhiTexture* base_tex = skin_texture_ ? skin_texture_ : white_tex_;
		QRhiTexture* glow_tex = skin_glow_texture_ ? skin_glow_texture_ : black_tex_;
		skin_srb_ = rhi()->newShaderResourceBindings();
		skin_srb_->setBindings({
			QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, ubuf_, sizeof(UniformBlock)),
			QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, base_tex, sampler_),
			QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, glow_tex, sampler_),
		});
		skin_srb_->create();
	}

	for (DrawSurface& s : surfaces_) {
		if (upload(s.image, &s.texture_handle)) {
			s.has_texture = true;
		}
		if (upload(s.glow_image, &s.glow_texture_handle)) {
			s.has_glow = true;
		}

		if (s.texture_handle || s.glow_texture_handle) {
			QRhiTexture* base_tex = s.texture_handle ? s.texture_handle : white_tex_;
			QRhiTexture* glow_tex = s.glow_texture_handle ? s.glow_texture_handle : black_tex_;
			s.srb = rhi()->newShaderResourceBindings();
			s.srb->setBindings({
				QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, ubuf_, sizeof(UniformBlock)),
				QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, base_tex, sampler_),
				QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, glow_tex, sampler_),
			});
			s.srb->create();
		}
	}
}

void ModelViewerVulkanWidget::update_ground_mesh_if_needed(QRhiResourceUpdateBatch* updates) {
	if (!model_ || !updates || !rhi()) {
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
	ground_vertices_.push_back(GpuVertex{minx, miny, z, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
	ground_vertices_.push_back(GpuVertex{maxx, miny, z, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f});
	ground_vertices_.push_back(GpuVertex{maxx, maxy, z, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
	ground_vertices_.push_back(GpuVertex{minx, maxy, z, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f});

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

void ModelViewerVulkanWidget::update_grid_lines_if_needed(QRhiResourceUpdateBatch* updates, const QVector3D& cam_pos, float aspect) {
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
	verts.reserve(line_count * 2 * 2);

	const auto push_line = [&](float ax, float ay, float bx, float by, const QVector3D& c, float a) {
		verts.push_back(GridLineVertex{ax, ay, z, c.x(), c.y(), c.z(), a});
		verts.push_back(GridLineVertex{bx, by, z, c.x(), c.y(), c.z(), a});
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

void ModelViewerVulkanWidget::update_background_mesh_if_needed(QRhiResourceUpdateBatch* updates) {
	if (!updates || !rhi()) {
		return;
	}
	if (bg_vbuf_) {
		return;
	}

	bg_vertices_.clear();
	bg_vertices_.reserve(6);
	bg_vertices_.push_back(GpuVertex{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
	bg_vertices_.push_back(GpuVertex{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
	bg_vertices_.push_back(GpuVertex{ 1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
	bg_vertices_.push_back(GpuVertex{-1.0f,  1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f});

	bg_vbuf_ = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
								 bg_vertices_.size() * sizeof(GpuVertex));
	bg_vbuf_->create();
	updates->uploadStaticBuffer(bg_vbuf_, bg_vertices_.constData());
}

void ModelViewerVulkanWidget::update_grid_settings() {
	const float reference = std::max(distance_, radius_ * 0.25f);
	grid_scale_ = quantized_grid_scale(reference);
}

void ModelViewerVulkanWidget::update_background_colors(QVector3D* top, QVector3D* bottom, QVector3D* base) const {
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

void ModelViewerVulkanWidget::update_grid_colors(QVector3D* grid, QVector3D* axis_x, QVector3D* axis_y) const {
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

void ModelViewerVulkanWidget::destroy_mesh_resources() {
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
	if (grid_vbuf_) {
		delete grid_vbuf_;
		grid_vbuf_ = nullptr;
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
		if (s.glow_texture_handle) {
			delete s.glow_texture_handle;
			s.glow_texture_handle = nullptr;
		}
		if (s.srb) {
			delete s.srb;
			s.srb = nullptr;
		}
	}
	if (skin_texture_) {
		delete skin_texture_;
		skin_texture_ = nullptr;
	}
	if (skin_glow_texture_) {
		delete skin_glow_texture_;
		skin_glow_texture_ = nullptr;
	}
	if (skin_srb_) {
		delete skin_srb_;
		skin_srb_ = nullptr;
	}
	if (default_srb_) {
		delete default_srb_;
		default_srb_ = nullptr;
	}
	if (ground_srb_) {
		delete ground_srb_;
		ground_srb_ = nullptr;
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
}

void ModelViewerVulkanWidget::destroy_pipeline_resources() {
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
	if (black_tex_) {
		delete black_tex_;
		black_tex_ = nullptr;
	}
}

void ModelViewerVulkanWidget::ensure_pipeline() {
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
		QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float2, offsetof(GpuVertex, u)),
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
	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.opColor = QRhiGraphicsPipeline::Add;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.opAlpha = QRhiGraphicsPipeline::Add;
	pipeline_->setTargetBlends({blend});
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
		grid_pipeline_->setTargetBlends({blend});
		grid_pipeline_->create();
	}

	pipeline_dirty_ = false;
}

void ModelViewerVulkanWidget::ensure_uniform_buffer(int draw_count) {
	if (!rhi()) {
		return;
	}
	const quint32 stride = aligned_uniform_stride(rhi(), sizeof(UniformBlock));
	const quint32 required = stride * static_cast<quint32>(std::max(1, draw_count));
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
	if (skin_srb_) {
		delete skin_srb_;
		skin_srb_ = nullptr;
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
	pipeline_dirty_ = true;
}

void ModelViewerVulkanWidget::ensure_default_srb(QRhiResourceUpdateBatch* updates) {
	if (!rhi()) {
		return;
	}
	if (!sampler_) {
		rebuild_sampler();
	}
	if (!white_tex_) {
		white_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		white_tex_->create();
		if (updates) {
			const QImage white(1, 1, QImage::Format_RGBA8888);
			updates->uploadTexture(white_tex_, white);
		}
	}
	if (!black_tex_) {
		black_tex_ = rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1);
		black_tex_->create();
		if (updates) {
			QImage black(1, 1, QImage::Format_RGBA8888);
			black.fill(Qt::black);
			updates->uploadTexture(black_tex_, black);
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
		QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, black_tex_, sampler_),
	});
	default_srb_->create();
	pipeline_dirty_ = true;
}

void ModelViewerVulkanWidget::rebuild_sampler() {
	if (!rhi()) {
		return;
	}
	if (sampler_) {
		delete sampler_;
		sampler_ = nullptr;
	}
	const QRhiSampler::Filter filter = texture_smoothing_ ? QRhiSampler::Linear : QRhiSampler::Nearest;
	sampler_ = rhi()->newSampler(filter,
								  filter,
								  QRhiSampler::None,
								  QRhiSampler::Repeat,
								  QRhiSampler::Repeat);
	sampler_->create();
	if (default_srb_) {
		delete default_srb_;
		default_srb_ = nullptr;
	}
	if (skin_srb_) {
		delete skin_srb_;
		skin_srb_ = nullptr;
	}
	for (DrawSurface& s : surfaces_) {
		if (s.srb) {
			delete s.srb;
			s.srb = nullptr;
		}
	}
	pipeline_dirty_ = true;
}
