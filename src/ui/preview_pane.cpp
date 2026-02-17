#include "preview_pane.h"

#include <algorithm>

#include <QAudioOutput>
#include <QColorDialog>
#include <QComboBox>
#include <QFileInfo>
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QFont>
#include <QFontInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPalette>
#include <QIcon>
#include <QLayoutItem>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSettings>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QTimer>
#include <QTextOption>
#include <QUrl>
#include <QVBoxLayout>
#include <QStyle>

#include "formats/image_loader.h"
#include "ui/bsp_preview_vulkan_widget.h"
#include "ui/bsp_preview_widget.h"
#include "ui/cfg_syntax_highlighter.h"
#include "ui/cinematic_player_widget.h"
#include "ui/video_player_widget.h"
#include "ui/model_viewer_vulkan_widget.h"
#include "ui/model_viewer_widget.h"
#include "ui/shader_viewer_widget.h"
#include "ui/simple_syntax_highlighter.h"

namespace {
/*
=============
hex_dump

Format a byte buffer into a hex/ascii dump string for preview display.
=============
*/
QString hex_dump(const QByteArray& bytes, int max_lines) {
	const int kPerLine = 16;
	const int n = bytes.size();
	const int lines = (n + kPerLine - 1) / kPerLine;
	const int out_lines = qMin(lines, max_lines);

	QString out;
	out.reserve(out_lines * 80);
	for (int line = 0; line < out_lines; ++line) {
		const int base = line * kPerLine;
		out += QString("%1  ").arg(base, 8, 16, QLatin1Char('0'));
		for (int i = 0; i < kPerLine; ++i) {
			const int idx = base + i;
			if (idx < n) {
				out += QString("%1 ").arg(static_cast<unsigned char>(bytes[idx]), 2, 16, QLatin1Char('0'));
			} else {
				out += "   ";
			}
		}
		out += " ";
		for (int i = 0; i < kPerLine; ++i) {
			const int idx = base + i;
			if (idx >= n) {
				break;
			}
			const unsigned char c = static_cast<unsigned char>(bytes[idx]);
			out += (c >= 32 && c < 127) ? QChar(c) : QChar('.');
		}
		out += "\n";
	}
	return out;
}

/*
=============
format_duration

Format milliseconds as a mm:ss or hh:mm:ss time string.
=============
*/
QString format_duration(qint64 millis) {
	if (millis < 0) {
		return "--:--";
	}
	const qint64 total_seconds = millis / 1000;
	const qint64 seconds = total_seconds % 60;
	const qint64 minutes = (total_seconds / 60) % 60;
	const qint64 hours = total_seconds / 3600;
	if (hours > 0) {
		return QString("%1:%2:%3")
			.arg(hours, 2, 10, QLatin1Char('0'))
			.arg(minutes, 2, 10, QLatin1Char('0'))
			.arg(seconds, 2, 10, QLatin1Char('0'));
	}
	return QString("%1:%2")
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'));
}

QString format_size(qint64 bytes) {
	constexpr qint64 kKiB = 1024;
	constexpr qint64 kMiB = 1024 * 1024;
	constexpr qint64 kGiB = 1024 * 1024 * 1024;
	if (bytes < 0) {
		return {};
	}
	if (bytes >= kGiB) {
		return QString("%1 GiB").arg(QString::number(static_cast<double>(bytes) / static_cast<double>(kGiB), 'f', 1));
	}
	if (bytes >= kMiB) {
		return QString("%1 MiB").arg(QString::number(static_cast<double>(bytes) / static_cast<double>(kMiB), 'f', 1));
	}
	if (bytes >= kKiB) {
		return QString("%1 KiB").arg(QString::number(static_cast<double>(bytes) / static_cast<double>(kKiB), 'f', 1));
	}
	return QString("%1 B").arg(bytes);
}

QString format_mtime(qint64 utc_secs) {
	if (utc_secs <= 0) {
		return {};
	}
	const QDateTime dt = QDateTime::fromSecsSinceEpoch(utc_secs, QTimeZone::utc()).toLocalTime();
	return dt.toString("yyyy-MM-dd HH:mm:ss");
}

bool read_u16_be_at(const QByteArray& bytes, int offset, quint16* out) {
	if (!out || offset < 0 || offset + 2 > bytes.size()) {
		return false;
	}
	const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = (static_cast<quint16>(p[0]) << 8) | static_cast<quint16>(p[1]);
	return true;
}

bool read_u32_be_at(const QByteArray& bytes, int offset, quint32* out) {
	if (!out || offset < 0 || offset + 4 > bytes.size()) {
		return false;
	}
	const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
	*out = (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
	       (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
	return true;
}

QString sfnt_tag_text(quint32 tag) {
	QString out;
	out.reserve(4);
	for (int shift = 24; shift >= 0; shift -= 8) {
		const char c = static_cast<char>((tag >> shift) & 0xFFu);
		out += (c >= 32 && c <= 126) ? QChar(c) : QChar('.');
	}
	return out;
}

QString font_container_label(quint32 signature) {
	switch (signature) {
		case 0x00010000u:
		case 0x74727565u:  // "true"
			return "TrueType outlines";
		case 0x4F54544Fu:  // "OTTO"
			return "OpenType (CFF)";
		case 0x74746366u:  // "ttcf"
			return "TrueType/OpenType Collection";
		case 0x74797031u:  // "typ1"
			return "OpenType Type 1";
		default:
			return "Unknown/unsupported SFNT";
	}
}

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
}  // namespace

/*
=============
PreviewPane::PreviewPane

Create the preview pane widget and populate its stacked preview pages.
=============
*/
PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
	build_ui();
	show_placeholder();
}

PreviewPane::~PreviewPane() {
	clear_font_preview_font();
}

void PreviewPane::set_current_file_info(const QString& pak_path, qint64 size, qint64 mtime_utc_secs) {
	current_pak_path_ = pak_path;
	current_file_size_ = size;
	current_mtime_utc_secs_ = mtime_utc_secs;
}

/*
=============
PreviewPane::resizeEvent

Reflow the current preview when the pane size changes.
=============
*/
void PreviewPane::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
	if (image_card_ && image_card_->isVisible() && !image_source_pixmap_.isNull()) {
		set_image_pixmap(image_source_pixmap_);
	}
	update_shader_viewport_width();
}

bool PreviewPane::eventFilter(QObject* watched, QEvent* event) {
	if (watched == three_d_fullscreen_window_ && event && event->type() == QEvent::Close) {
		exit_3d_fullscreen();
		event->accept();
		return true;
	}
	if (shader_scroll_ && watched == shader_scroll_->viewport() && event && event->type() == QEvent::Resize) {
		update_shader_viewport_width();
	}
	return QWidget::eventFilter(watched, event);
}

QWidget* PreviewPane::active_3d_widget() const {
	if (stack_ && stack_->currentWidget() == bsp_page_) {
		return bsp_widget_;
	}
	if (stack_ && stack_->currentWidget() == model_page_) {
		return model_widget_;
	}
	return nullptr;
}

QWidget* PreviewPane::active_3d_page() const {
	if (stack_ && stack_->currentWidget() == bsp_page_) {
		return bsp_page_;
	}
	if (stack_ && stack_->currentWidget() == model_page_) {
		return model_page_;
	}
	return nullptr;
}

void PreviewPane::update_3d_fullscreen_button() {
	if (!three_d_fullscreen_button_) {
		return;
	}
	const bool active = (three_d_fullscreen_window_ != nullptr);
	const bool available = active || (active_3d_widget() != nullptr && active_3d_page() != nullptr);
	three_d_fullscreen_button_->setEnabled(available);
	three_d_fullscreen_button_->setChecked(active);
	three_d_fullscreen_button_->setText(active ? "Exit Fullscreen" : "Fullscreen");
	three_d_fullscreen_button_->setIcon(style()->standardIcon(active ? QStyle::SP_TitleBarNormalButton
	                                                               : QStyle::SP_TitleBarMaxButton));
}

void PreviewPane::toggle_3d_fullscreen() {
	if (three_d_fullscreen_window_) {
		exit_3d_fullscreen();
	} else {
		enter_3d_fullscreen();
	}
}

void PreviewPane::enter_3d_fullscreen() {
	if (three_d_fullscreen_window_) {
		return;
	}
	QWidget* page = active_3d_page();
	QWidget* source_widget = active_3d_widget();
	const PreviewCameraState source_camera = camera_state_for_widget(source_widget);
	if (!page) {
		update_3d_fullscreen_button();
		return;
	}

	auto* window = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
	window->setAttribute(Qt::WA_DeleteOnClose, false);
	window->setWindowTitle("PakFu 3D Preview");
	window->installEventFilter(this);

	auto* layout = new QVBoxLayout(window);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	QWidget* fullscreen_widget = nullptr;
	if (page == bsp_page_) {
		switch (renderer_effective_) {
			case PreviewRenderer::Vulkan:
				fullscreen_widget = new BspPreviewVulkanWidget(window);
				break;
			case PreviewRenderer::OpenGL:
				fullscreen_widget = new BspPreviewWidget(window);
				break;
		}
		if (!fullscreen_widget) {
			window->deleteLater();
			update_3d_fullscreen_button();
			return;
		}

		apply_bsp_lightmap(fullscreen_widget, bsp_lightmapping_enabled_);
		apply_3d_visual_settings(fullscreen_widget,
		                         three_d_grid_mode_,
		                         three_d_bg_mode_,
		                         three_d_bg_color_,
		                         three_d_wireframe_enabled_,
		                         three_d_textured_enabled_,
		                         three_d_fov_degrees_,
		                         glow_enabled_);

		if (has_cached_bsp_) {
			if (auto* gl = as_gl_bsp_widget(fullscreen_widget)) {
				gl->set_mesh(cached_bsp_mesh_, cached_bsp_textures_);
			} else if (auto* vk = as_vk_bsp_widget(fullscreen_widget)) {
				vk->set_mesh(cached_bsp_mesh_, cached_bsp_textures_);
			}
		}
	} else if (page == model_page_) {
		switch (renderer_effective_) {
			case PreviewRenderer::Vulkan:
				fullscreen_widget = new ModelViewerVulkanWidget(window);
				break;
			case PreviewRenderer::OpenGL:
				fullscreen_widget = new ModelViewerWidget(window);
				break;
		}
		if (!fullscreen_widget) {
			window->deleteLater();
			update_3d_fullscreen_button();
			return;
		}

		apply_3d_visual_settings(fullscreen_widget,
		                         three_d_grid_mode_,
		                         three_d_bg_mode_,
		                         three_d_bg_color_,
		                         three_d_wireframe_enabled_,
		                         three_d_textured_enabled_,
		                         three_d_fov_degrees_,
		                         glow_enabled_);

		if (auto* gl = as_gl_model_widget(fullscreen_widget)) {
			gl->set_texture_smoothing(model_texture_smoothing_);
			gl->set_palettes(model_palette_quake1_, model_palette_quake2_);
		} else if (auto* vk = as_vk_model_widget(fullscreen_widget)) {
			vk->set_texture_smoothing(model_texture_smoothing_);
			vk->set_palettes(model_palette_quake1_, model_palette_quake2_);
		}

		if (has_cached_model_) {
			QString err;
			const bool ok = [&]() {
				if (auto* gl = as_gl_model_widget(fullscreen_widget)) {
					return cached_model_skin_path_.isEmpty()
						       ? gl->load_file(cached_model_file_path_, &err)
						       : gl->load_file(cached_model_file_path_, cached_model_skin_path_, &err);
				}
				if (auto* vk = as_vk_model_widget(fullscreen_widget)) {
					return cached_model_skin_path_.isEmpty()
						       ? vk->load_file(cached_model_file_path_, &err)
						       : vk->load_file(cached_model_file_path_, cached_model_skin_path_, &err);
				}
				return false;
			}();
			if (!ok) {
				qWarning() << "PreviewPane: fullscreen model reload failed:" << err;
			}
		}
	} else {
		window->deleteLater();
		update_3d_fullscreen_button();
		return;
	}
	apply_camera_state(fullscreen_widget, source_camera);

	layout->addWidget(fullscreen_widget, 1);

	auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), window);
	connect(esc, &QShortcut::activated, this, [this]() {
		exit_3d_fullscreen();
	});

	three_d_fullscreen_window_ = window;
	three_d_fullscreen_widget_ = fullscreen_widget;
	three_d_fullscreen_page_ = page;

	window->showFullScreen();
	QTimer::singleShot(0, this, [this, window, fullscreen_widget]() {
		if (three_d_fullscreen_window_ == window && three_d_fullscreen_widget_ == fullscreen_widget) {
			fullscreen_widget->setFocus(Qt::OtherFocusReason);
		}
	});
	update_3d_fullscreen_button();
}

void PreviewPane::exit_3d_fullscreen() {
	if (!three_d_fullscreen_window_ || !three_d_fullscreen_widget_) {
		three_d_fullscreen_window_ = nullptr;
		three_d_fullscreen_widget_ = nullptr;
		three_d_fullscreen_page_ = nullptr;
		update_3d_fullscreen_button();
		return;
	}

	QWidget* window = three_d_fullscreen_window_;
	QWidget* widget = three_d_fullscreen_widget_;
	QWidget* page = three_d_fullscreen_page_;
	const PreviewCameraState fs_camera = camera_state_for_widget(widget);

	three_d_fullscreen_window_ = nullptr;
	three_d_fullscreen_widget_ = nullptr;
	three_d_fullscreen_page_ = nullptr;

	window->removeEventFilter(this);
	if (page == bsp_page_) {
		apply_camera_state(bsp_widget_, fs_camera);
	} else if (page == model_page_) {
		apply_camera_state(model_widget_, fs_camera);
	}

	window->hide();
	window->deleteLater();
	update_3d_fullscreen_button();
}

/*
=============
PreviewPane::build_ui

Construct the shared header and preview content pages.
=============
*/
void PreviewPane::build_ui() {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(14, 14, 14, 14);
	layout->setSpacing(10);

	renderer_requested_ = load_preview_renderer();
	renderer_effective_ = resolve_preview_renderer(renderer_requested_);

	auto* header = new QWidget(this);
	auto* header_layout = new QVBoxLayout(header);
	header_layout->setContentsMargins(8, 8, 8, 8);
	header_layout->setSpacing(2);

	title_label_ = new QLabel(header);
	title_label_->setWordWrap(false);
	QFont title_font = title_label_->font();
	title_font.setPointSize(title_font.pointSize() + 1);
	title_font.setWeight(QFont::DemiBold);
	title_label_->setFont(title_font);
	header_layout->addWidget(title_label_);

	subtitle_label_ = new QLabel(header);
	subtitle_label_->setWordWrap(true);
	subtitle_label_->setStyleSheet("color: rgba(180, 180, 180, 220);");
	header_layout->addWidget(subtitle_label_);

	header->setObjectName("previewHeader");
	header->setStyleSheet(
		"#previewHeader {"
		"  border: 1px solid rgba(120, 120, 120, 70);"
		"  border-radius: 10px;"
		"  background-color: rgba(255, 255, 255, 20);"
		"}");
	layout->addWidget(header, 0);

	insights_scroll_ = new QScrollArea(this);
	insights_scroll_->setWidgetResizable(true);
	insights_scroll_->setFrameShape(QFrame::NoFrame);
	layout->addWidget(insights_scroll_, 1);

	insights_page_ = new QWidget(insights_scroll_);
	auto* insights_layout = new QVBoxLayout(insights_page_);
	insights_layout->setContentsMargins(0, 0, 0, 0);
	insights_layout->setSpacing(10);
	insights_scroll_->setWidget(insights_page_);

	QFont card_title_font = title_label_->font();
	card_title_font.setWeight(QFont::DemiBold);

	overview_card_ = new QFrame(insights_page_);
	overview_card_->setObjectName("insightsOverviewCard");
	overview_card_->setStyleSheet(
		"#insightsOverviewCard {"
		"  border: 1px solid rgba(120, 120, 120, 70);"
		"  border-radius: 10px;"
		"  background-color: rgba(255, 255, 255, 16);"
		"}");
	auto* overview_layout = new QVBoxLayout(overview_card_);
	overview_layout->setContentsMargins(12, 12, 12, 12);
	overview_layout->setSpacing(8);

	auto* overview_title = new QLabel("File Overview", overview_card_);
	overview_title->setFont(card_title_font);
	overview_layout->addWidget(overview_title, 0);

	image_card_ = new QFrame(insights_page_);
	image_card_->setObjectName("insightsImageCard");
	image_card_->setStyleSheet(
		"#insightsImageCard {"
		"  border: 1px solid rgba(120, 120, 120, 70);"
		"  border-radius: 10px;"
		"  background-color: rgba(255, 255, 255, 16);"
		"}");
	image_card_->setVisible(false);
	auto* image_layout = new QVBoxLayout(image_card_);
	image_layout->setContentsMargins(12, 12, 12, 12);
	image_layout->setSpacing(8);

	auto* image_title = new QLabel("Image", image_card_);
	image_title->setFont(card_title_font);
	image_layout->addWidget(image_title, 0);

	overview_image_container_ = new QWidget(image_card_);
	auto* overview_image_layout = new QVBoxLayout(overview_image_container_);
	overview_image_layout->setContentsMargins(0, 0, 0, 0);
	overview_image_layout->setSpacing(6);

	auto* img_controls = new QWidget(overview_image_container_);
	auto* img_controls_layout = new QHBoxLayout(img_controls);
	img_controls_layout->setContentsMargins(6, 4, 6, 4);
	img_controls_layout->setSpacing(8);

	auto* bg_label = new QLabel("Background", img_controls);
	bg_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
	img_controls_layout->addWidget(bg_label);

	image_bg_mode_combo_ = new QComboBox(img_controls);
	image_bg_mode_combo_->addItem("Transparent", "transparent");
	image_bg_mode_combo_->addItem("Checkerboard", "checkerboard");
	image_bg_mode_combo_->addItem("Solid", "solid");
	image_bg_mode_combo_->setToolTip("Background shown behind transparent pixels.");
	img_controls_layout->addWidget(image_bg_mode_combo_);

	image_bg_color_button_ = new QToolButton(img_controls);
	image_bg_color_button_->setText("Color…");
	image_bg_color_button_->setToolTip("Choose the background color behind transparent pixels.");
	img_controls_layout->addWidget(image_bg_color_button_);

	auto* layout_label = new QLabel("Layout", img_controls);
	layout_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
	img_controls_layout->addWidget(layout_label);

	image_layout_combo_ = new QComboBox(img_controls);
	image_layout_combo_->addItem("Fit", "fit");
	image_layout_combo_->addItem("Tile", "tile");
	image_layout_combo_->setToolTip("Choose whether to fit the image or tile it across the preview area.");
	img_controls_layout->addWidget(image_layout_combo_);

	image_mip_label_ = new QLabel("Mip", img_controls);
	image_mip_label_->setStyleSheet("color: rgba(190, 190, 190, 220);");
	img_controls_layout->addWidget(image_mip_label_);

	image_mip_combo_ = new QComboBox(img_controls);
	image_mip_combo_->addItem("Mip 0 (Base)", 0);
	image_mip_combo_->addItem("Mip 1", 1);
	image_mip_combo_->addItem("Mip 2", 2);
	image_mip_combo_->addItem("Mip 3", 3);
	image_mip_combo_->setToolTip("Choose the mip level to display (0 = largest).");
	img_controls_layout->addWidget(image_mip_combo_);
	image_mip_label_->setVisible(false);
	image_mip_combo_->setVisible(false);

	img_controls_layout->addStretch();

	image_reveal_transparency_button_ = new QToolButton(img_controls);
	image_reveal_transparency_button_->setText("Reveal hidden pixels");
	image_reveal_transparency_button_->setCheckable(true);
	image_reveal_transparency_button_->setAutoRaise(true);
	image_reveal_transparency_button_->setCursor(Qt::PointingHandCursor);
	image_reveal_transparency_button_->setToolTip("Show the RGB values of fully transparent pixels.");
	img_controls_layout->addWidget(image_reveal_transparency_button_);

	overview_image_layout->addWidget(img_controls, 0);

	image_scroll_ = new QScrollArea(overview_image_container_);
	image_scroll_->setWidgetResizable(true);
	image_scroll_->setFrameShape(QFrame::NoFrame);
	image_scroll_->setMinimumHeight(240);
	if (QWidget* vp = image_scroll_->viewport()) {
		vp->setAutoFillBackground(true);
	}
	image_label_ = new QLabel(image_scroll_);
	image_label_->setAlignment(Qt::AlignCenter);
	image_label_->setScaledContents(false);
	image_label_->setStyleSheet("background: transparent;");
	image_scroll_->setWidget(image_label_);
	overview_image_layout->addWidget(image_scroll_, 1);

	auto* overview_fields = new QWidget(overview_card_);
	overview_form_ = new QFormLayout(overview_fields);
	overview_form_->setContentsMargins(0, 0, 0, 0);
	overview_form_->setHorizontalSpacing(12);
	overview_form_->setVerticalSpacing(4);
	overview_layout->addWidget(overview_fields, 0);

	insights_layout->addWidget(overview_card_, 0);
	image_layout->addWidget(overview_image_container_, 1);
	insights_layout->addWidget(image_card_, 0);

	content_card_ = new QFrame(insights_page_);
	content_card_->setObjectName("insightsContentCard");
	content_card_->setStyleSheet(
		"#insightsContentCard {"
		"  border: 1px solid rgba(120, 120, 120, 70);"
		"  border-radius: 10px;"
		"  background-color: rgba(255, 255, 255, 16);"
		"}");
	auto* content_layout = new QVBoxLayout(content_card_);
	content_layout->setContentsMargins(12, 12, 12, 12);
	content_layout->setSpacing(8);

	content_title_label_ = new QLabel(content_card_);
	content_title_label_->setFont(card_title_font);
	content_layout->addWidget(content_title_label_, 0);

	text_controls_ = new QWidget(content_card_);
	auto* text_controls_layout = new QHBoxLayout(text_controls_);
	text_controls_layout->setContentsMargins(6, 4, 6, 4);
	text_controls_layout->setSpacing(8);

	auto* text_label = new QLabel("Text", text_controls_);
	text_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
	text_controls_layout->addWidget(text_label);

	text_wrap_button_ = new QToolButton(text_controls_);
	text_wrap_button_->setText("Word Wrap");
	text_wrap_button_->setCheckable(true);
	text_wrap_button_->setAutoRaise(true);
	text_wrap_button_->setCursor(Qt::PointingHandCursor);
	text_wrap_button_->setToolTip("Wrap long lines at word boundaries in text previews.");
	text_controls_layout->addWidget(text_wrap_button_);
	text_controls_layout->addStretch();

	text_controls_->setVisible(false);
	content_layout->addWidget(text_controls_, 0);

	three_d_controls_ = new QWidget(content_card_);
	auto* three_d_layout = new QHBoxLayout(three_d_controls_);
	three_d_layout->setContentsMargins(6, 4, 6, 4);
	three_d_layout->setSpacing(8);

	auto* grid_label = new QLabel("Grid", three_d_controls_);
	grid_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
	three_d_layout->addWidget(grid_label);

	three_d_grid_combo_ = new QComboBox(three_d_controls_);
	three_d_grid_combo_->addItem("Floor + Shadow", "floor");
	three_d_grid_combo_->addItem("Grid + Axis", "grid");
	three_d_grid_combo_->addItem("None", "none");
	three_d_grid_combo_->setToolTip("Choose the XY grid or ground treatment for 3D previews.");
	three_d_layout->addWidget(three_d_grid_combo_);

	auto* three_d_bg_label = new QLabel("Background", three_d_controls_);
	three_d_bg_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
	three_d_layout->addWidget(three_d_bg_label);

	three_d_bg_combo_ = new QComboBox(three_d_controls_);
	three_d_bg_combo_->addItem("Themed", "themed");
	three_d_bg_combo_->addItem("Grey", "grey");
	three_d_bg_combo_->addItem("Custom", "custom");
	three_d_bg_combo_->setToolTip("Choose the background style for 3D previews.");
	three_d_layout->addWidget(three_d_bg_combo_);

	three_d_bg_color_button_ = new QToolButton(three_d_controls_);
	three_d_bg_color_button_->setText("Color…");
	three_d_bg_color_button_->setToolTip("Choose the custom background color for 3D previews.");
	three_d_layout->addWidget(three_d_bg_color_button_);

	auto* lighting_label = new QLabel("Lighting", three_d_controls_);
	lighting_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
	three_d_layout->addWidget(lighting_label);

	bsp_lightmap_button_ = new QToolButton(three_d_controls_);
	bsp_lightmap_button_->setText("Lightmaps");
	bsp_lightmap_button_->setCheckable(true);
	bsp_lightmap_button_->setAutoRaise(true);
	bsp_lightmap_button_->setCursor(Qt::PointingHandCursor);
	bsp_lightmap_button_->setToolTip("Toggle internal BSP lightmap rendering.");
	three_d_layout->addWidget(bsp_lightmap_button_);

	three_d_layout->addStretch();

	three_d_textured_button_ = new QToolButton(three_d_controls_);
	three_d_textured_button_->setText("Textured");
	three_d_textured_button_->setCheckable(true);
	three_d_textured_button_->setAutoRaise(true);
	three_d_textured_button_->setCursor(Qt::PointingHandCursor);
	three_d_textured_button_->setToolTip("Toggle textured rendering for 3D previews.");
	three_d_layout->addWidget(three_d_textured_button_);

	three_d_wireframe_button_ = new QToolButton(three_d_controls_);
	three_d_wireframe_button_->setText("Wireframe");
	three_d_wireframe_button_->setCheckable(true);
	three_d_wireframe_button_->setAutoRaise(true);
	three_d_wireframe_button_->setCursor(Qt::PointingHandCursor);
	three_d_wireframe_button_->setToolTip("Toggle wireframe rendering for 3D previews.");
	three_d_layout->addWidget(three_d_wireframe_button_);

	three_d_fullscreen_button_ = new QToolButton(three_d_controls_);
	three_d_fullscreen_button_->setText("Fullscreen");
	three_d_fullscreen_button_->setCheckable(true);
	three_d_fullscreen_button_->setAutoRaise(true);
	three_d_fullscreen_button_->setCursor(Qt::PointingHandCursor);
	three_d_fullscreen_button_->setToolTip("Open the current 3D viewport in fullscreen (Esc exits).");
	three_d_layout->addWidget(three_d_fullscreen_button_);

	three_d_controls_->setVisible(false);
	content_layout->addWidget(three_d_controls_, 0);

	stack_ = new QStackedWidget(content_card_);
	content_layout->addWidget(stack_, 1);

	insights_layout->addWidget(content_card_, 1);

	// Placeholder.
	placeholder_page_ = new QWidget(stack_);
	auto* ph_layout = new QVBoxLayout(placeholder_page_);
	ph_layout->setContentsMargins(18, 18, 18, 18);
	ph_layout->addStretch();
	placeholder_label_ = new QLabel("Select a file to view insights.", placeholder_page_);
	placeholder_label_->setAlignment(Qt::AlignCenter);
	placeholder_label_->setWordWrap(true);
	placeholder_label_->setStyleSheet("color: rgba(200, 200, 200, 190);");
	ph_layout->addWidget(placeholder_label_);
	ph_layout->addStretch();
	stack_->addWidget(placeholder_page_);

	// Message page.
	message_page_ = new QWidget(stack_);
	auto* msg_layout = new QVBoxLayout(message_page_);
	msg_layout->setContentsMargins(18, 18, 18, 18);
	msg_layout->addStretch();
	message_label_ = new QLabel(message_page_);
	message_label_->setAlignment(Qt::AlignCenter);
	message_label_->setWordWrap(true);
	msg_layout->addWidget(message_label_);
	msg_layout->addStretch();
	stack_->addWidget(message_page_);

	// Text/binary page (shared).
	text_page_ = new QWidget(stack_);
	auto* text_layout = new QVBoxLayout(text_page_);
	text_layout->setContentsMargins(0, 0, 0, 0);
	text_view_ = new QPlainTextEdit(text_page_);
	text_view_->setReadOnly(true);
	text_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
	QFont mono("Consolas");
	if (!mono.exactMatch()) {
		mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	}
	text_view_->setFont(mono);
	text_layout->addWidget(text_view_);
	stack_->addWidget(text_page_);

	// Shader page (tiled renderer + selection).
	shader_page_ = new QWidget(stack_);
	auto* shader_page_layout = new QVBoxLayout(shader_page_);
	shader_page_layout->setContentsMargins(0, 0, 0, 0);
	shader_page_layout->setSpacing(6);

	auto* shader_hint = new QLabel("Click tiles to select shader blocks. Use Ctrl/Cmd or Shift for multi-select.", shader_page_);
	shader_hint->setWordWrap(true);
	shader_hint->setStyleSheet("color: rgba(195, 195, 195, 210);");
	shader_page_layout->addWidget(shader_hint, 0);

	shader_scroll_ = new QScrollArea(shader_page_);
	shader_scroll_->setWidgetResizable(false);
	shader_scroll_->setFrameShape(QFrame::NoFrame);
	shader_page_layout->addWidget(shader_scroll_, 1);

	shader_widget_ = new ShaderViewerWidget(shader_scroll_);
	shader_scroll_->setWidget(shader_widget_);
	if (shader_scroll_->viewport()) {
		shader_scroll_->viewport()->installEventFilter(this);
	}

	stack_->addWidget(shader_page_);

	// Font page (specialized specimen view).
	font_page_ = new QWidget(stack_);
	auto* font_page_layout = new QVBoxLayout(font_page_);
	font_page_layout->setContentsMargins(0, 0, 0, 0);
	font_page_layout->setSpacing(0);

	auto* font_scroll = new QScrollArea(font_page_);
	font_scroll->setWidgetResizable(true);
	font_scroll->setFrameShape(QFrame::NoFrame);
	font_page_layout->addWidget(font_scroll, 1);

	auto* font_scroll_content = new QWidget(font_scroll);
	auto* font_scroll_layout = new QVBoxLayout(font_scroll_content);
	font_scroll_layout->setContentsMargins(4, 4, 4, 4);
	font_scroll_layout->setSpacing(0);

	auto* font_card = new QFrame(font_scroll_content);
	font_card->setObjectName("fontSpecimenCard");
	font_card->setStyleSheet(
		"#fontSpecimenCard {"
		"  border: 1px solid rgba(120, 120, 120, 70);"
		"  border-radius: 14px;"
		"  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
		"                              stop:0 rgba(36, 48, 68, 220),"
		"                              stop:1 rgba(18, 24, 36, 220));"
		"}"
		"#fontSpecimenHeading { color: rgba(238, 243, 252, 245); }"
		"#fontSpecimenSubheading { color: rgba(190, 201, 220, 230); }"
		"#fontSpecimenNotice { color: rgba(255, 196, 96, 235); }"
		"#fontSpecimenMeta { color: rgba(186, 198, 220, 225); }"
		"#fontSpecimenLine { color: rgba(232, 238, 250, 242); }");
	auto* font_card_layout = new QVBoxLayout(font_card);
	font_card_layout->setContentsMargins(20, 20, 20, 20);
	font_card_layout->setSpacing(10);

	font_heading_label_ = new QLabel("Font Specimen", font_card);
	font_heading_label_->setObjectName("fontSpecimenHeading");
	QFont heading_font = title_label_ ? title_label_->font() : font_heading_label_->font();
	heading_font.setPointSize(std::max(12, heading_font.pointSize() + 1));
	heading_font.setWeight(QFont::DemiBold);
	font_heading_label_->setFont(heading_font);
	font_heading_label_->setWordWrap(true);
	font_card_layout->addWidget(font_heading_label_);

	font_subheading_label_ = new QLabel(font_card);
	font_subheading_label_->setObjectName("fontSpecimenSubheading");
	font_subheading_label_->setWordWrap(true);
	font_card_layout->addWidget(font_subheading_label_);

	font_notice_label_ = new QLabel(font_card);
	font_notice_label_->setObjectName("fontSpecimenNotice");
	font_notice_label_->setWordWrap(true);
	font_notice_label_->setVisible(false);
	font_card_layout->addWidget(font_notice_label_);

	auto make_line_label = [&](const QString& text) -> QLabel* {
		auto* l = new QLabel(text, font_card);
		l->setObjectName("fontSpecimenLine");
		l->setWordWrap(true);
		l->setTextInteractionFlags(Qt::TextSelectableByMouse);
		return l;
	};

	font_hero_label_ = make_line_label("The quick brown fox jumps over the lazy dog");
	font_pangram_label_ = make_line_label("Sphinx of black quartz, judge my vow.");
	font_upper_label_ = make_line_label("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	font_lower_label_ = make_line_label("abcdefghijklmnopqrstuvwxyz");
	font_digits_label_ = make_line_label("0123456789");
	font_symbols_label_ = make_line_label("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");

	font_card_layout->addWidget(font_hero_label_);
	font_card_layout->addWidget(font_pangram_label_);
	font_card_layout->addWidget(font_upper_label_);
	font_card_layout->addWidget(font_lower_label_);
	font_card_layout->addWidget(font_digits_label_);
	font_card_layout->addWidget(font_symbols_label_);

	font_meta_label_ = new QLabel(font_card);
	font_meta_label_->setObjectName("fontSpecimenMeta");
	font_meta_label_->setWordWrap(true);
	font_meta_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	font_card_layout->addWidget(font_meta_label_);

	font_scroll_layout->addWidget(font_card, 0);
	font_scroll_layout->addStretch(1);
	font_scroll->setWidget(font_scroll_content);
	stack_->addWidget(font_page_);

	// Audio page.
	audio_page_ = new QWidget(stack_);
	auto* audio_layout = new QVBoxLayout(audio_page_);
	audio_layout->setContentsMargins(12, 12, 12, 12);
	audio_layout->setSpacing(10);

	auto* controls_layout = new QHBoxLayout();
	controls_layout->setSpacing(8);
	audio_prev_button_ = new QToolButton(audio_page_);
	audio_prev_button_->setAutoRaise(true);
	audio_prev_button_->setCursor(Qt::PointingHandCursor);
	audio_prev_button_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
	audio_prev_button_->setIconSize(QSize(18, 18));
	audio_prev_button_->setToolTip("Previous audio file");

	audio_play_button_ = new QToolButton(audio_page_);
	audio_play_button_->setAutoRaise(true);
	audio_play_button_->setCursor(Qt::PointingHandCursor);
	audio_play_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	audio_play_button_->setIconSize(QSize(18, 18));
	audio_play_button_->setToolTip("Play/Pause");

	audio_stop_button_ = new QToolButton(audio_page_);
	audio_stop_button_->setAutoRaise(true);
	audio_stop_button_->setCursor(Qt::PointingHandCursor);
	audio_stop_button_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
	audio_stop_button_->setIconSize(QSize(18, 18));
	audio_stop_button_->setToolTip("Stop");

	audio_next_button_ = new QToolButton(audio_page_);
	audio_next_button_->setAutoRaise(true);
	audio_next_button_->setCursor(Qt::PointingHandCursor);
	audio_next_button_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
	audio_next_button_->setIconSize(QSize(18, 18));
	audio_next_button_->setToolTip("Next audio file");
	audio_info_button_ = new QToolButton(audio_page_);
	audio_info_button_->setAutoRaise(true);
	audio_info_button_->setCursor(Qt::PointingHandCursor);
	audio_info_button_->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
	audio_info_button_->setIconSize(QSize(16, 16));
	audio_info_button_->setToolTip("Audio details will appear here once loaded.");

	controls_layout->addWidget(audio_prev_button_);
	controls_layout->addWidget(audio_play_button_);
	controls_layout->addWidget(audio_stop_button_);
	controls_layout->addWidget(audio_next_button_);
	controls_layout->addStretch();
	controls_layout->addWidget(audio_info_button_);

	audio_volume_scroll_ = new QScrollBar(Qt::Vertical, audio_page_);
	audio_volume_scroll_->setRange(0, 100);
	audio_volume_scroll_->setValue(80);
	audio_volume_scroll_->setPageStep(10);
	audio_volume_scroll_->setSingleStep(2);
	audio_volume_scroll_->setFixedWidth(14);
	audio_volume_scroll_->setFixedHeight(56);
	audio_volume_scroll_->setInvertedAppearance(true);
	audio_volume_scroll_->setToolTip("Volume");
	audio_volume_scroll_->setStyleSheet(
		"QScrollBar { background: transparent; }"
		"QScrollBar::add-line, QScrollBar::sub-line { height: 0px; }"
		"QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");
	controls_layout->addWidget(audio_volume_scroll_, 0, Qt::AlignVCenter);
	audio_layout->addLayout(controls_layout);

	audio_position_slider_ = new QSlider(Qt::Horizontal, audio_page_);
	audio_position_slider_->setRange(0, 0);
	audio_position_slider_->setToolTip("Seek");
	audio_layout->addWidget(audio_position_slider_);

	audio_status_label_ = new QLabel(audio_page_);
	audio_status_label_->setStyleSheet("color: rgba(180, 180, 180, 220);");
	audio_layout->addWidget(audio_status_label_);

	audio_player_ = new QMediaPlayer(this);
	audio_output_ = new QAudioOutput(this);
	audio_output_->setVolume(static_cast<float>(audio_volume_scroll_->value()) / 100.0f);
	audio_player_->setAudioOutput(audio_output_);

	connect(audio_prev_button_, &QToolButton::clicked, this, &PreviewPane::request_previous_audio);
	connect(audio_next_button_, &QToolButton::clicked, this, &PreviewPane::request_next_audio);
	connect(audio_play_button_, &QToolButton::clicked, this, [this]() {
		if (!audio_player_) {
			return;
		}
		if (audio_player_->playbackState() == QMediaPlayer::PlayingState) {
			audio_player_->pause();
		} else {
			audio_player_->play();
		}
	});
	connect(audio_stop_button_, &QToolButton::clicked, this, [this]() {
		if (!audio_player_) {
			return;
		}
		audio_player_->stop();
		audio_player_->setPosition(0);
		update_audio_status_label();
	});
	connect(audio_volume_scroll_, &QScrollBar::valueChanged, this, [this](int value) {
		if (audio_output_) {
			audio_output_->setVolume(static_cast<float>(value) / 100.0f);
		}
	});
	connect(audio_position_slider_, &QSlider::sliderPressed, this, [this]() {
		audio_user_scrubbing_ = true;
	});
	connect(audio_position_slider_, &QSlider::sliderReleased, this, [this]() {
		audio_user_scrubbing_ = false;
		if (audio_player_) {
			audio_player_->setPosition(audio_position_slider_->value());
		}
	});
	connect(audio_position_slider_, &QSlider::sliderMoved, this, [this](int value) {
		if (audio_user_scrubbing_ && audio_player_) {
			audio_player_->setPosition(value);
		}
	});
	connect(audio_player_, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
		if (audio_position_slider_) {
			audio_position_slider_->setRange(0, static_cast<int>(duration));
		}
		update_audio_tooltip();
		update_audio_overview();
		update_audio_status_label();
	});
	connect(audio_player_, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
		if (!audio_user_scrubbing_ && audio_position_slider_) {
			audio_position_slider_->setValue(static_cast<int>(position));
		}
		update_audio_status_label();
	});
	connect(audio_player_, &QMediaPlayer::metaDataChanged, this, [this]() {
		update_audio_tooltip();
		update_audio_overview();
		update_audio_status_label();
	});
	connect(audio_player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
		if (!audio_play_button_) {
			return;
		}
		audio_play_button_->setIcon(
			style()->standardIcon(state == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
	});
	stack_->addWidget(audio_page_);

	// Cinematic/video page (CIN/ROQ).
	cinematic_page_ = new QWidget(stack_);
	auto* cin_layout = new QVBoxLayout(cinematic_page_);
	cin_layout->setContentsMargins(0, 0, 0, 0);
	cinematic_widget_ = new CinematicPlayerWidget(cinematic_page_);
	cin_layout->addWidget(cinematic_widget_, 1);
	stack_->addWidget(cinematic_page_);

	connect(cinematic_widget_, &CinematicPlayerWidget::request_previous_media, this, &PreviewPane::request_previous_video);
	connect(cinematic_widget_, &CinematicPlayerWidget::request_next_media, this, &PreviewPane::request_next_video);

	// Video page (Qt Multimedia).
	video_page_ = new QWidget(stack_);
	auto* video_layout = new QVBoxLayout(video_page_);
	video_layout->setContentsMargins(0, 0, 0, 0);
	video_widget_ = new VideoPlayerWidget(video_page_);
	video_layout->addWidget(video_widget_, 1);
	stack_->addWidget(video_page_);

	connect(video_widget_, &VideoPlayerWidget::request_previous_media, this, &PreviewPane::request_previous_video);
	connect(video_widget_, &VideoPlayerWidget::request_next_media, this, &PreviewPane::request_next_video);
	connect(video_widget_, &VideoPlayerWidget::media_info_changed, this, &PreviewPane::update_video_overview);

	// BSP/map page.
	bsp_page_ = new QWidget(stack_);
	auto* bsp_layout = new QVBoxLayout(bsp_page_);
	bsp_layout->setContentsMargins(0, 0, 0, 0);
	bsp_layout->setSpacing(6);
	stack_->addWidget(bsp_page_);
	rebuild_bsp_widget();

	// Model page (MDL/MD2/MD3/IQM/MD5/LWO/OBJ).
	model_page_ = new QWidget(stack_);
	auto* model_layout = new QVBoxLayout(model_page_);
	model_layout->setContentsMargins(0, 0, 0, 0);
	stack_->addWidget(model_page_);
	rebuild_model_widget();

	QSettings settings;
	const QString bg_mode = settings.value("preview/image/backgroundMode").toString().trimmed().toLower();
	if (bg_mode == "solid") {
		image_bg_mode_ = ImageBackgroundMode::Solid;
	} else if (bg_mode == "transparent") {
		image_bg_mode_ = ImageBackgroundMode::Transparent;
	} else if (bg_mode == "checkerboard") {
		image_bg_mode_ = ImageBackgroundMode::Checkerboard;
	} else {
		const bool checker = settings.value("preview/image/checkerboard", true).toBool();
		image_bg_mode_ = checker ? ImageBackgroundMode::Checkerboard : ImageBackgroundMode::Solid;
	}
	const QString layout_mode = settings.value("preview/image/layout").toString().trimmed().toLower();
	image_layout_mode_ = (layout_mode == "tile") ? ImageLayoutMode::Tile : ImageLayoutMode::Fit;
	image_reveal_transparency_ = settings.value("preview/image/revealTransparent", false).toBool();
	text_word_wrap_enabled_ = settings.value("preview/text/wordWrap", false).toBool();
	image_texture_smoothing_ = settings.value("preview/image/textureSmoothing", false).toBool();
	model_texture_smoothing_ = settings.value("preview/model/textureSmoothing", false).toBool();
	bsp_lightmapping_enabled_ = settings.value("preview/bsp/lightmapping", true).toBool();
	{
		const QString grid_mode = settings.value("preview/3d/gridMode", "floor").toString().trimmed().toLower();
		if (grid_mode == "grid") {
			three_d_grid_mode_ = PreviewGridMode::Grid;
		} else if (grid_mode == "none") {
			three_d_grid_mode_ = PreviewGridMode::None;
		} else {
			three_d_grid_mode_ = PreviewGridMode::Floor;
		}
	}
	{
		const QString bg_mode = settings.value("preview/3d/backgroundMode", "themed").toString().trimmed().toLower();
		if (bg_mode == "grey") {
			three_d_bg_mode_ = PreviewBackgroundMode::Grey;
		} else if (bg_mode == "custom") {
			three_d_bg_mode_ = PreviewBackgroundMode::Custom;
		} else {
			three_d_bg_mode_ = PreviewBackgroundMode::Themed;
		}
	}
	three_d_wireframe_enabled_ = settings.value("preview/3d/wireframe", false).toBool();
	three_d_textured_enabled_ = settings.value("preview/3d/textured", true).toBool();
	three_d_fov_degrees_ = std::clamp(settings.value("preview/3d/fov", 100).toInt(), 40, 120);

	{
		QVariant bg = settings.value("preview/image/backgroundColor");
		QColor c;
		if (bg.canConvert<QColor>()) {
			c = bg.value<QColor>();
		} else {
			c = QColor(bg.toString());
		}

		if (!c.isValid()) {
			const QColor base = palette().color(QPalette::Window);
			c = (base.lightness() < 128) ? QColor(64, 64, 64) : QColor(224, 224, 224);
		}
		image_bg_color_ = c;
	}

	{
		QVariant bg = settings.value("preview/3d/backgroundColor");
		QColor c;
		if (bg.canConvert<QColor>()) {
			c = bg.value<QColor>();
		} else {
			c = QColor(bg.toString());
		}
		if (!c.isValid()) {
			const QColor base = palette().color(QPalette::Window);
			c = (base.lightness() < 128) ? QColor(40, 40, 44) : QColor(210, 210, 210);
		}
		three_d_bg_color_ = c;
	}

	if (image_bg_mode_combo_) {
		const QString want = (image_bg_mode_ == ImageBackgroundMode::Solid) ? "solid"
			: (image_bg_mode_ == ImageBackgroundMode::Transparent) ? "transparent" : "checkerboard";
		const int idx = image_bg_mode_combo_->findData(want);
		if (idx >= 0) {
			image_bg_mode_combo_->setCurrentIndex(idx);
		}
		connect(image_bg_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (!image_bg_mode_combo_) {
				return;
			}
			const QString data = image_bg_mode_combo_->itemData(index).toString().trimmed().toLower();
			if (data == "solid") {
				image_bg_mode_ = ImageBackgroundMode::Solid;
			} else if (data == "transparent") {
				image_bg_mode_ = ImageBackgroundMode::Transparent;
			} else {
				image_bg_mode_ = ImageBackgroundMode::Checkerboard;
			}
			QSettings s;
			s.setValue("preview/image/backgroundMode", data.isEmpty() ? "transparent" : data);
			apply_image_background();
		});
	}

	if (text_wrap_button_) {
		text_wrap_button_->setChecked(text_word_wrap_enabled_);
		connect(text_wrap_button_, &QToolButton::toggled, this, [this](bool checked) {
			text_word_wrap_enabled_ = checked;
			QSettings s;
			s.setValue("preview/text/wordWrap", text_word_wrap_enabled_);
			apply_text_wrap_mode();
		});
	}

	if (image_layout_combo_) {
		const QString want = (image_layout_mode_ == ImageLayoutMode::Tile) ? "tile" : "fit";
		const int idx = image_layout_combo_->findData(want);
		if (idx >= 0) {
			image_layout_combo_->setCurrentIndex(idx);
		}
		connect(image_layout_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (!image_layout_combo_) {
				return;
			}
			const QString data = image_layout_combo_->itemData(index).toString().trimmed().toLower();
			image_layout_mode_ = (data == "tile") ? ImageLayoutMode::Tile : ImageLayoutMode::Fit;
			QSettings s;
			s.setValue("preview/image/layout", data.isEmpty() ? "fit" : data);
			apply_image_transparency_mode();
		});
	}

	if (image_mip_combo_) {
		const int idx = image_mip_combo_->findData(image_mip_level_);
		if (idx >= 0) {
			image_mip_combo_->setCurrentIndex(idx);
		}
		connect(image_mip_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (!image_mip_combo_) {
				return;
			}
			bool ok = false;
			const int level = image_mip_combo_->itemData(index).toInt(&ok);
			if (!ok) {
				return;
			}
			const int clamped = qBound(0, level, 3);
			if (image_mip_level_ == clamped) {
				return;
			}
			image_mip_level_ = clamped;
			emit request_image_mip_level(image_mip_level_);
		});
	}

	if (image_bg_color_button_) {
		update_image_bg_button();
		connect(image_bg_color_button_, &QToolButton::clicked, this, [this]() {
			const QColor chosen = QColorDialog::getColor(image_bg_color_, this, "Choose Transparency Background");
			if (!chosen.isValid()) {
				return;
			}
			image_bg_color_ = chosen;
			QSettings s;
			s.setValue("preview/image/backgroundColor", image_bg_color_);
			update_image_bg_button();
			apply_image_background();
		});
	}

	if (image_reveal_transparency_button_) {
		image_reveal_transparency_button_->setChecked(image_reveal_transparency_);
		connect(image_reveal_transparency_button_, &QToolButton::toggled, this, [this](bool checked) {
			image_reveal_transparency_ = checked;
			QSettings s;
			s.setValue("preview/image/revealTransparent", image_reveal_transparency_);
			apply_image_transparency_mode();
		});
	}

	if (three_d_grid_combo_) {
		const QString want = (three_d_grid_mode_ == PreviewGridMode::Grid) ? "grid"
			: (three_d_grid_mode_ == PreviewGridMode::None) ? "none" : "floor";
		const int idx = three_d_grid_combo_->findData(want);
		if (idx >= 0) {
			three_d_grid_combo_->setCurrentIndex(idx);
		}
		connect(three_d_grid_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (!three_d_grid_combo_) {
				return;
			}
			const QString data = three_d_grid_combo_->itemData(index).toString().trimmed().toLower();
			if (data == "grid") {
				three_d_grid_mode_ = PreviewGridMode::Grid;
			} else if (data == "none") {
				three_d_grid_mode_ = PreviewGridMode::None;
			} else {
				three_d_grid_mode_ = PreviewGridMode::Floor;
			}
			QSettings s;
			s.setValue("preview/3d/gridMode", data.isEmpty() ? "floor" : data);
			apply_3d_settings();
		});
	}

	if (three_d_bg_combo_) {
		const QString want = (three_d_bg_mode_ == PreviewBackgroundMode::Grey) ? "grey"
			: (three_d_bg_mode_ == PreviewBackgroundMode::Custom) ? "custom" : "themed";
		const int idx = three_d_bg_combo_->findData(want);
		if (idx >= 0) {
			three_d_bg_combo_->setCurrentIndex(idx);
		}
		connect(three_d_bg_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (!three_d_bg_combo_) {
				return;
			}
			const QString data = three_d_bg_combo_->itemData(index).toString().trimmed().toLower();
			if (data == "grey") {
				three_d_bg_mode_ = PreviewBackgroundMode::Grey;
			} else if (data == "custom") {
				three_d_bg_mode_ = PreviewBackgroundMode::Custom;
			} else {
				three_d_bg_mode_ = PreviewBackgroundMode::Themed;
			}
			QSettings s;
			s.setValue("preview/3d/backgroundMode", data.isEmpty() ? "themed" : data);
			apply_3d_bg_color_button_state();
			apply_3d_settings();
		});
	}

	if (three_d_bg_color_button_) {
		update_3d_bg_button();
		apply_3d_bg_color_button_state();
		connect(three_d_bg_color_button_, &QToolButton::clicked, this, [this]() {
			const QColor chosen = QColorDialog::getColor(three_d_bg_color_, this, "Choose 3D Background Color");
			if (!chosen.isValid()) {
				return;
			}
			three_d_bg_color_ = chosen;
			QSettings s;
			s.setValue("preview/3d/backgroundColor", three_d_bg_color_);
			update_3d_bg_button();
			apply_3d_settings();
		});
	}

	if (three_d_wireframe_button_) {
		three_d_wireframe_button_->setChecked(three_d_wireframe_enabled_);
		connect(three_d_wireframe_button_, &QToolButton::toggled, this, [this](bool checked) {
			three_d_wireframe_enabled_ = checked;
			QSettings s;
			s.setValue("preview/3d/wireframe", three_d_wireframe_enabled_);
			apply_3d_settings();
		});
	}

	if (three_d_textured_button_) {
		three_d_textured_button_->setChecked(three_d_textured_enabled_);
		connect(three_d_textured_button_, &QToolButton::toggled, this, [this](bool checked) {
			three_d_textured_enabled_ = checked;
			QSettings s;
			s.setValue("preview/3d/textured", three_d_textured_enabled_);
			apply_3d_settings();
		});
	}

	if (three_d_fullscreen_button_) {
		three_d_fullscreen_button_->setChecked(false);
		connect(three_d_fullscreen_button_, &QToolButton::clicked, this, [this]() {
			toggle_3d_fullscreen();
		});
		update_3d_fullscreen_button();
	}

	if (bsp_lightmap_button_) {
		bsp_lightmap_button_->blockSignals(true);
		bsp_lightmap_button_->setChecked(bsp_lightmapping_enabled_);
		bsp_lightmap_button_->blockSignals(false);
		connect(bsp_lightmap_button_, &QToolButton::toggled, this, [this](bool checked) {
			bsp_lightmapping_enabled_ = checked;
			QSettings s;
			s.setValue("preview/bsp/lightmapping", bsp_lightmapping_enabled_);
			if (bsp_widget_) {
				apply_bsp_lightmap(bsp_widget_, bsp_lightmapping_enabled_);
			}
		});
	}

	if (bsp_widget_) {
		apply_bsp_lightmap(bsp_widget_, bsp_lightmapping_enabled_);
	}

	sprite_timer_ = new QTimer(this);
	sprite_timer_->setSingleShot(true);
	connect(sprite_timer_, &QTimer::timeout, this, [this]() {
		if (sprite_frames_.isEmpty()) {
			return;
		}
		const int next = (sprite_frame_index_ + 1) % sprite_frames_.size();
		apply_sprite_frame(next);
		schedule_next_sprite_frame();
	});

	apply_3d_settings();
	apply_image_background();
	apply_text_wrap_mode();
	update_shader_viewport_width();
}

/*
=============
PreviewPane::set_preview_renderer

Switch the renderer preference (effective change recreates 3D widgets).
=============
*/
void PreviewPane::set_preview_renderer(PreviewRenderer renderer) {
	if (renderer_requested_ == renderer) {
		return;
	}
	renderer_requested_ = renderer;
	const PreviewRenderer effective = resolve_preview_renderer(renderer_requested_);
	if (renderer_effective_ == effective) {
		return;
	}
	renderer_effective_ = effective;
	rebuild_3d_widgets();

	if (current_content_kind_ == ContentKind::Bsp || current_content_kind_ == ContentKind::Model) {
		show_message("Renderer changed", "Select the file again to re-render with the new backend.");
	}
}

void PreviewPane::set_3d_fov_degrees(int degrees) {
	const int clamped = std::clamp(degrees, 40, 120);
	if (three_d_fov_degrees_ == clamped) {
		return;
	}
	three_d_fov_degrees_ = clamped;
	apply_3d_settings();
}

void PreviewPane::rebuild_3d_widgets() {
	rebuild_bsp_widget();
	rebuild_model_widget();
}

void PreviewPane::rebuild_bsp_widget() {
	if (!bsp_page_) {
		return;
	}
	if (three_d_fullscreen_window_) {
		exit_3d_fullscreen();
	}
	auto* layout = qobject_cast<QVBoxLayout*>(bsp_page_->layout());
	if (!layout) {
		return;
	}
	if (bsp_widget_) {
		layout->removeWidget(bsp_widget_);
		bsp_widget_->deleteLater();
		bsp_widget_ = nullptr;
	}

	switch (renderer_effective_) {
		case PreviewRenderer::Vulkan:
			bsp_widget_ = new BspPreviewVulkanWidget(bsp_page_);
			layout->addWidget(bsp_widget_, 1);
			apply_bsp_lightmap(bsp_widget_, bsp_lightmapping_enabled_);
			break;
		case PreviewRenderer::OpenGL:
			bsp_widget_ = new BspPreviewWidget(bsp_page_);
			layout->addWidget(bsp_widget_, 1);
			apply_bsp_lightmap(bsp_widget_, bsp_lightmapping_enabled_);
			break;
	}
	if (has_cached_bsp_) {
		if (auto* gl = as_gl_bsp_widget(bsp_widget_)) {
			gl->set_mesh(cached_bsp_mesh_, cached_bsp_textures_);
		} else if (auto* vk = as_vk_bsp_widget(bsp_widget_)) {
			vk->set_mesh(cached_bsp_mesh_, cached_bsp_textures_);
		}
	}
	apply_3d_settings();
	update_3d_fullscreen_button();
}

void PreviewPane::rebuild_model_widget() {
	if (!model_page_) {
		return;
	}
	if (three_d_fullscreen_window_) {
		exit_3d_fullscreen();
	}
	auto* layout = qobject_cast<QVBoxLayout*>(model_page_->layout());
	if (!layout) {
		return;
	}
	if (model_widget_) {
		layout->removeWidget(model_widget_);
		model_widget_->deleteLater();
		model_widget_ = nullptr;
	}

	switch (renderer_effective_) {
		case PreviewRenderer::Vulkan:
			model_widget_ = new ModelViewerVulkanWidget(model_page_);
			layout->addWidget(model_widget_, 1);
			break;
		case PreviewRenderer::OpenGL:
			model_widget_ = new ModelViewerWidget(model_page_);
			layout->addWidget(model_widget_, 1);
			break;
	}
	set_model_texture_smoothing(model_texture_smoothing_);
	set_model_palettes(model_palette_quake1_, model_palette_quake2_);
	if (has_cached_model_) {
		QString err;
		bool ok = false;
		if (auto* gl = as_gl_model_widget(model_widget_)) {
			ok = cached_model_skin_path_.isEmpty() ? gl->load_file(cached_model_file_path_, &err)
			                                       : gl->load_file(cached_model_file_path_, cached_model_skin_path_, &err);
		} else if (auto* vk = as_vk_model_widget(model_widget_)) {
			ok = cached_model_skin_path_.isEmpty() ? vk->load_file(cached_model_file_path_, &err)
			                                       : vk->load_file(cached_model_file_path_, cached_model_skin_path_, &err);
		}
		if (!ok) {
			has_cached_model_ = false;
		}
	}
	apply_3d_settings();
	update_3d_fullscreen_button();
}

/*
=============
PreviewPane::set_header

Update the header title and subtitle text.
=============
*/

void PreviewPane::set_header(const QString& title, const QString& subtitle) {
	if (title_label_) {
		title_label_->setText(title);
	}
	if (subtitle_label_) {
		subtitle_label_->setText(subtitle);
	}
}

/*
=============
PreviewPane::show_placeholder

Show the default placeholder panel when no item is selected.
=============
*/
void PreviewPane::show_placeholder() {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::None;
	current_pak_path_.clear();
	current_file_size_ = -1;
	current_mtime_utc_secs_ = -1;

	clear_overview_fields();
	show_overview_block(false);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	set_image_qimage({});

	set_header("Insights", "Select a file from the list.");
	show_content_block("Insights", placeholder_page_);
}

/*
=============
PreviewPane::show_message

Show a centered message panel with a title and body text.
=============
*/
void PreviewPane::show_message(const QString& title, const QString& body) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Message;
	set_header(title, QString());
	if (message_label_) {
		message_label_->setText(body);
		message_label_->setStyleSheet("color: rgba(220, 220, 220, 210);");
	}

	const bool have_file_info = !current_pak_path_.isEmpty() || current_file_size_ >= 0 || current_mtime_utc_secs_ >= 0;
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(have_file_info);
	if (have_file_info) {
		populate_basic_overview();
	}

	show_content_block("Insights", message_page_);
}

/*
=============
PreviewPane::show_text

Show a plain-text preview panel.
=============
*/
void PreviewPane::show_text(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::None);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Text");
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}

void PreviewPane::show_c(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::C);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "C");
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}

void PreviewPane::show_txt(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::QuakeTxtBlocks);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Text");
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}

void PreviewPane::show_cfg(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::Cfg);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "CFG");
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}

void PreviewPane::show_font_from_bytes(const QString& title, const QString& subtitle, const QByteArray& bytes) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Font;
	set_text_highlighter(TextSyntax::None);
	set_header(title, subtitle);
	clear_font_preview_font();

	if (image_card_) {
		image_card_->setVisible(false);
	}

	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Font");
	set_overview_value("Bytes", format_size(bytes.size()));

	const auto set_fallback_text = [&](const QString& text) {
		if (!font_page_) {
			if (text_view_) {
				text_view_->setPlainText(text);
			}
			show_content_block("Font Details", text_page_);
			return;
		}

		if (font_heading_label_) {
			font_heading_label_->setText("Font Preview");
		}
		if (font_subheading_label_) {
			font_subheading_label_->setText("No loadable font data was found.");
		}
		if (font_notice_label_) {
			font_notice_label_->setVisible(true);
			font_notice_label_->setText(text);
		}

		if (font_hero_label_) {
			font_hero_label_->setText("The quick brown fox jumps over the lazy dog");
		}
		if (font_pangram_label_) {
			font_pangram_label_->setText("Sphinx of black quartz, judge my vow.");
		}
		if (font_upper_label_) {
			font_upper_label_->setText("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		}
		if (font_lower_label_) {
			font_lower_label_->setText("abcdefghijklmnopqrstuvwxyz");
		}
		if (font_digits_label_) {
			font_digits_label_->setText("0123456789");
		}
		if (font_symbols_label_) {
			font_symbols_label_->setText("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
		}
		if (font_meta_label_) {
			font_meta_label_->setText("Unable to decode font family/style metadata.");
		}
		show_content_block("Font Preview", font_page_);
	};

	if (bytes.isEmpty()) {
		set_fallback_text("Font file is empty.");
		return;
	}

	quint32 signature = 0;
	if (read_u32_be_at(bytes, 0, &signature)) {
		const QString sig_hex = QString("0x%1").arg(QString::number(signature, 16).rightJustified(8, QLatin1Char('0')));
		set_overview_value("Signature", QString("%1 (%2)").arg(sig_hex, sfnt_tag_text(signature)));
		set_overview_value("Container", font_container_label(signature));
	}

	if (signature == 0x74746366u) {  // "ttcf"
		quint32 collection_fonts = 0;
		if (read_u32_be_at(bytes, 8, &collection_fonts)) {
			set_overview_value("Faces", QString::number(collection_fonts));
		}
	} else {
		quint16 num_tables = 0;
		if (read_u16_be_at(bytes, 4, &num_tables)) {
			set_overview_value("Tables", QString::number(num_tables));
		}
	}

	const int font_id = QFontDatabase::addApplicationFontFromData(bytes);
	const bool loadable = (font_id >= 0);
	set_overview_value("Loadable", loadable ? "Yes" : "No");
	font_preview_font_id_ = loadable ? font_id : -1;

	QStringList families;
	QString primary_family;
	QString primary_style;
	QStringList styles;
	if (loadable) {
		families = QFontDatabase::applicationFontFamilies(font_id);
		if (!families.isEmpty()) {
			primary_family = families.first();
			styles = QFontDatabase::styles(primary_family);
			if (!styles.isEmpty()) {
				primary_style = styles.first();
			}
		}
	}

	if (!primary_family.isEmpty()) {
		set_overview_value("Family", primary_family);
	}
	if (!primary_style.isEmpty()) {
		set_overview_value("Style", primary_style);
	}
	if (families.size() > 1) {
		set_overview_value("Families", QString::number(families.size()));
	}
	if (styles.size() > 1) {
		set_overview_value("Styles", QString::number(styles.size()));
	}

	QFont sample_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
	if (!primary_family.isEmpty()) {
		sample_font = QFont(primary_family);
	}
	if (!primary_style.isEmpty()) {
		sample_font.setStyleName(primary_style);
	}
	sample_font.setStyleStrategy(QFont::PreferAntialias);

	if (!font_page_) {
		QStringList details;
		details << "Font Preview Details";
		details << "--------------------";
		details << QString("Container: %1").arg(font_container_label(signature));
		details << QString("Loadable by Qt: %1").arg(loadable ? "yes" : "no");
		if (!primary_family.isEmpty()) {
			details << QString("Primary family: %1").arg(primary_family);
		}
		if (!primary_style.isEmpty()) {
			details << QString("Primary style: %1").arg(primary_style);
		}
		if (text_view_) {
			text_view_->setPlainText(details.join('\n'));
		}
		show_content_block("Font Details", text_page_);
		return;
	}

	QFont hero_font = sample_font;
	hero_font.setPointSize(40);
	hero_font.setWeight(QFont::DemiBold);

	QFont pangram_font = sample_font;
	pangram_font.setPointSize(24);
	pangram_font.setWeight(QFont::Normal);

	QFont letter_font = sample_font;
	letter_font.setPointSize(20);
	letter_font.setWeight(QFont::Normal);

	QFont small_font = sample_font;
	small_font.setPointSize(16);
	small_font.setWeight(QFont::Normal);

	if (font_heading_label_) {
		font_heading_label_->setText(primary_family.isEmpty() ? "Font Specimen" : QString("Font Specimen - %1").arg(primary_family));
	}
	if (font_subheading_label_) {
		const QString resolved_style = primary_style.isEmpty() ? QString("Regular") : primary_style;
		font_subheading_label_->setText(QString("%1  |  %2").arg(font_container_label(signature), resolved_style));
	}
	if (font_notice_label_) {
		font_notice_label_->setVisible(!loadable);
		font_notice_label_->setText(!loadable ? "Qt could not instantiate this font. Showing fallback typography." : QString());
	}
	if (font_hero_label_) {
		font_hero_label_->setFont(hero_font);
		font_hero_label_->setText("The quick brown fox jumps over the lazy dog");
	}
	if (font_pangram_label_) {
		font_pangram_label_->setFont(pangram_font);
		font_pangram_label_->setText("Sphinx of black quartz, judge my vow.");
	}
	if (font_upper_label_) {
		font_upper_label_->setFont(letter_font);
		font_upper_label_->setText("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	}
	if (font_lower_label_) {
		font_lower_label_->setFont(letter_font);
		font_lower_label_->setText("abcdefghijklmnopqrstuvwxyz");
	}
	if (font_digits_label_) {
		font_digits_label_->setFont(small_font);
		font_digits_label_->setText("0123456789");
	}
	if (font_symbols_label_) {
		font_symbols_label_->setFont(small_font);
		font_symbols_label_->setText("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
	}

	QStringList meta_lines;
	const QFontInfo fi(sample_font);
	meta_lines << QString("Resolved family: %1").arg(fi.family());
	meta_lines << QString("Resolved style: %1").arg(fi.styleName().isEmpty() ? QString("Regular") : fi.styleName());
	meta_lines << QString("Resolved weight: %1").arg(fi.weight());
	meta_lines << QString("Italic: %1").arg(fi.italic() ? "yes" : "no");
	if (!families.isEmpty()) {
		meta_lines << QString("Families in file: %1").arg(families.join(", "));
	}
	if (!styles.isEmpty()) {
		meta_lines << QString("Styles: %1").arg(styles.join(", "));
	}
	if (font_meta_label_) {
		font_meta_label_->setText(meta_lines.join('\n'));
	}

	show_content_block("Font Preview", font_page_);
}

/*
=============
PreviewPane::show_binary

Show a binary hex dump preview panel.
=============
*/
void PreviewPane::show_binary(const QString& title,
								const QString& subtitle,
								const QByteArray& bytes,
								bool truncated) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::None);
	QString sub = subtitle;
	if (truncated) {
		sub = sub.isEmpty() ? "Content truncated." : (sub + "  (Content truncated)");
	}
	set_header(title, sub);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Binary");
	set_overview_value("Bytes shown", format_size(bytes.size()));
	if (text_view_) {
		text_view_->setPlainText(hex_dump(bytes, 256));
	}
	show_content_block("Text View", text_page_);
	if (text_controls_) {
		text_controls_->setVisible(false);
	}
}

void PreviewPane::show_image(const QString& title, const QString& subtitle, const QImage& image) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Image;
	set_header(title, subtitle);
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Image");
	if (!image.isNull()) {
		set_overview_value("Dimensions", QString("%1x%2").arg(image.width()).arg(image.height()));
	}

	if (image_card_) {
		image_card_->setVisible(true);
	}
	hide_content_block();

	set_image_qimage(image);
	QTimer::singleShot(0, this, [this]() {
		if (!image_card_ || !image_card_->isVisible() || image_source_pixmap_.isNull()) {
			return;
		}
		set_image_pixmap(image_source_pixmap_);
	});
}

void PreviewPane::show_sprite(const QString& title,
                              const QString& subtitle,
                              const QVector<QImage>& frames,
                              const QVector<int>& frame_durations_ms,
                              const QString& details_text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	stop_sprite_animation();
	current_content_kind_ = ContentKind::Sprite;
	set_header(title, subtitle);
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Sprite");
	set_overview_value("Frames", QString::number(frames.size()));

	int max_w = 0;
	int max_h = 0;
	for (const QImage& frame : frames) {
		if (!frame.isNull()) {
			max_w = std::max(max_w, frame.width());
			max_h = std::max(max_h, frame.height());
		}
	}
	if (max_w > 0 && max_h > 0) {
		set_overview_value("Dimensions", QString("%1x%2").arg(max_w).arg(max_h));
	}
	if (!frame_durations_ms.isEmpty()) {
		qint64 total_ms = 0;
		for (const int ms : frame_durations_ms) {
			total_ms += std::max(0, ms);
		}
		if (total_ms > 0) {
			set_overview_value("Loop duration", format_duration(total_ms));
		}
	}

	if (image_card_) {
		image_card_->setVisible(true);
	}

	set_text_highlighter(TextSyntax::None);
	if (text_view_) {
		text_view_->setPlainText(details_text);
	}
	show_content_block("Sprite Details", text_page_);

	sprite_frames_ = frames;
	sprite_frame_durations_ms_ = frame_durations_ms;
	sprite_frame_index_ = 0;
	set_image_mip_controls(false, 0);
	if (!sprite_frames_.isEmpty()) {
		apply_sprite_frame(0);
		QTimer::singleShot(0, this, [this]() {
			if (!image_card_ || !image_card_->isVisible() || image_source_pixmap_.isNull()) {
				return;
			}
			set_image_pixmap(image_source_pixmap_);
		});
		schedule_next_sprite_frame();
	} else {
		set_image_qimage({});
	}
}

void PreviewPane::show_bsp(const QString& title, const QString& subtitle, BspMesh mesh, QHash<QString, QImage> textures) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	cached_bsp_mesh_ = mesh;
	cached_bsp_textures_ = textures;
	has_cached_bsp_ = !cached_bsp_mesh_.vertices.isEmpty() && !cached_bsp_mesh_.indices.isEmpty();
	current_content_kind_ = ContentKind::Bsp;
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "BSP");
	set_overview_value("Vertices", QString::number(mesh.vertices.size()));
	set_overview_value("Triangles", QString::number(mesh.indices.size() / 3));
	set_overview_value("Surfaces", QString::number(mesh.surfaces.size()));

	show_content_block("3D Panel", bsp_page_);
	if (three_d_controls_) {
		three_d_controls_->setVisible(true);
	}
	if (bsp_lightmap_button_) {
		bsp_lightmap_button_->setVisible(true);
	}
	if (bsp_widget_) {
		if (auto* gl = as_gl_bsp_widget(bsp_widget_)) {
			gl->set_lightmap_enabled(bsp_lightmapping_enabled_);
			gl->set_mesh(std::move(mesh), std::move(textures));
		} else if (auto* vk = as_vk_bsp_widget(bsp_widget_)) {
			vk->set_lightmap_enabled(bsp_lightmapping_enabled_);
			vk->set_mesh(std::move(mesh), std::move(textures));
		}
	}
	if (three_d_fullscreen_window_ && three_d_fullscreen_page_ == bsp_page_ && three_d_fullscreen_widget_) {
		apply_bsp_lightmap(three_d_fullscreen_widget_, bsp_lightmapping_enabled_);
		if (auto* gl = as_gl_bsp_widget(three_d_fullscreen_widget_)) {
			gl->set_mesh(cached_bsp_mesh_, cached_bsp_textures_);
		} else if (auto* vk = as_vk_bsp_widget(three_d_fullscreen_widget_)) {
			vk->set_mesh(cached_bsp_mesh_, cached_bsp_textures_);
		}
	}
}

void PreviewPane::set_text_highlighter(TextSyntax syntax) {
	if (syntax == current_text_syntax_ && text_highlighter_) {
		return;
	}

	text_highlighter_.reset();
	current_text_syntax_ = syntax;

	if (!text_view_ || !text_view_->document()) {
		return;
	}

	switch (syntax) {
		case TextSyntax::None:
			return;
		case TextSyntax::C:
			text_highlighter_ = std::make_unique<SimpleSyntaxHighlighter>(SimpleSyntaxHighlighter::Mode::C, text_view_->document());
			return;
		case TextSyntax::Cfg:
			text_highlighter_ = std::make_unique<CfgSyntaxHighlighter>(text_view_->document());
			return;
		case TextSyntax::Json:
			text_highlighter_ = std::make_unique<SimpleSyntaxHighlighter>(SimpleSyntaxHighlighter::Mode::Json, text_view_->document());
			return;
		case TextSyntax::QuakeTxtBlocks:
			text_highlighter_ = std::make_unique<SimpleSyntaxHighlighter>(SimpleSyntaxHighlighter::Mode::QuakeTxtBlocks, text_view_->document());
			return;
		case TextSyntax::Quake3Menu:
			text_highlighter_ = std::make_unique<SimpleSyntaxHighlighter>(SimpleSyntaxHighlighter::Mode::Quake3Menu, text_view_->document());
			return;
		case TextSyntax::Quake3Shader:
			text_highlighter_ = std::make_unique<SimpleSyntaxHighlighter>(SimpleSyntaxHighlighter::Mode::Quake3Shader, text_view_->document());
			return;
	}
}

void PreviewPane::apply_text_wrap_mode() {
	if (!text_view_) {
		return;
	}
	if (text_word_wrap_enabled_) {
		text_view_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
		text_view_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	} else {
		text_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
		text_view_->setWordWrapMode(QTextOption::NoWrap);
	}
	if (text_wrap_button_) {
		text_wrap_button_->blockSignals(true);
		text_wrap_button_->setChecked(text_word_wrap_enabled_);
		text_wrap_button_->blockSignals(false);
	}
}

void PreviewPane::update_shader_viewport_width() {
	if (!shader_scroll_ || !shader_widget_ || !shader_scroll_->viewport()) {
		return;
	}
	const int viewport_w = shader_scroll_->viewport()->width();
	if (viewport_w <= 0) {
		return;
	}
	shader_widget_->set_viewport_width(viewport_w);
}

void PreviewPane::show_overview_block(bool show) {
	if (overview_card_) {
		overview_card_->setVisible(show);
	}
}

void PreviewPane::show_content_block(const QString& title, QWidget* page) {
	if (page != font_page_) {
		clear_font_preview_font();
	}
	if (three_d_fullscreen_window_ && page != three_d_fullscreen_page_) {
		exit_3d_fullscreen();
	}

	if (content_title_label_) {
		content_title_label_->setText(title);
	}
	if (content_card_) {
		content_card_->setVisible(true);
	}
	if (text_controls_) {
		const bool show_text_controls = (page == text_page_ && current_content_kind_ == ContentKind::Text);
		text_controls_->setVisible(show_text_controls);
		if (show_text_controls) {
			apply_text_wrap_mode();
		}
	}
	if (three_d_controls_) {
		three_d_controls_->setVisible(false);
	}
	if (stack_ && page) {
		stack_->setCurrentWidget(page);
	}
	update_3d_fullscreen_button();
}

void PreviewPane::hide_content_block() {
	clear_font_preview_font();
	if (three_d_fullscreen_window_) {
		exit_3d_fullscreen();
	}
	if (content_card_) {
		content_card_->setVisible(false);
	}
	if (text_controls_) {
		text_controls_->setVisible(false);
	}
	update_3d_fullscreen_button();
}

void PreviewPane::clear_font_preview_font() {
	if (font_preview_font_id_ < 0) {
		return;
	}
	QFontDatabase::removeApplicationFont(font_preview_font_id_);
	font_preview_font_id_ = -1;
}

void PreviewPane::stop_sprite_animation() {
	if (sprite_timer_) {
		sprite_timer_->stop();
	}
	sprite_frames_.clear();
	sprite_frame_durations_ms_.clear();
	sprite_frame_index_ = 0;
}

void PreviewPane::apply_sprite_frame(int index) {
	if (sprite_frames_.isEmpty()) {
		return;
	}
	const int count = sprite_frames_.size();
	const int clamped = ((index % count) + count) % count;
	sprite_frame_index_ = clamped;
	set_image_qimage(sprite_frames_[clamped]);
}

void PreviewPane::schedule_next_sprite_frame() {
	if (!sprite_timer_ || sprite_frames_.size() < 2) {
		return;
	}
	int delay_ms = 100;
	if (!sprite_frame_durations_ms_.isEmpty()) {
		const int idx = (sprite_frame_index_ >= 0 && sprite_frame_index_ < sprite_frame_durations_ms_.size())
		                  ? sprite_frame_index_
		                  : 0;
		delay_ms = std::clamp(sprite_frame_durations_ms_[idx], 30, 2000);
	}
	sprite_timer_->start(delay_ms);
}

void PreviewPane::clear_overview_fields() {
	if (!overview_form_) {
		return;
	}
	auto delete_item_widget = [](QLayoutItem* item) {
		if (!item) {
			return;
		}
		if (QWidget* widget = item->widget()) {
			delete widget;
		} else if (QLayout* layout = item->layout()) {
			delete layout;
		}
		delete item;
	};

	for (auto it = overview_values_.begin(); it != overview_values_.end(); ++it) {
		QLabel* value = it.value();
		if (!value) {
			continue;
		}

		const QFormLayout::TakeRowResult row = overview_form_->takeRow(value);
		delete_item_widget(row.labelItem);
		delete_item_widget(row.fieldItem);
	}
	overview_values_.clear();
}

void PreviewPane::populate_basic_overview() {
	if (!current_pak_path_.isEmpty()) {
		set_overview_value("Path", current_pak_path_);
	}
	if (current_file_size_ >= 0) {
		set_overview_value("Size", format_size(current_file_size_));
	}
	const QString mtime = format_mtime(current_mtime_utc_secs_);
	if (!mtime.isEmpty()) {
		set_overview_value("Modified", mtime);
	}
}

void PreviewPane::set_overview_value(const QString& label, const QString& value) {
	if (!overview_form_) {
		return;
	}

	QLabel* value_label = overview_values_.value(label, nullptr);
	if (!value_label) {
		QWidget* parent = overview_form_->parentWidget();
		if (!parent) {
			parent = overview_card_;
		}

		auto* key_label = new QLabel(label + ":", parent);
		key_label->setStyleSheet("color: rgba(180, 180, 180, 220);");
		key_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

		value_label = new QLabel(parent);
		value_label->setWordWrap(true);
		value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		value_label->setStyleSheet("color: rgba(220, 220, 220, 230);");

		overview_form_->addRow(key_label, value_label);
		overview_values_.insert(label, value_label);
	}

	value_label->setText(value);
}

void PreviewPane::update_audio_overview() {
	if (current_content_kind_ != ContentKind::Audio || !audio_player_) {
		return;
	}

	const qint64 duration = audio_player_->duration();
	if (duration > 0) {
		set_overview_value("Duration", format_duration(duration));
	}

	const QMediaMetaData md = audio_player_->metaData();
	const QString fmt = md.stringValue(QMediaMetaData::FileFormat).trimmed();
	if (!fmt.isEmpty()) {
		set_overview_value("Format", fmt);
	}
	const QString codec = md.value(QMediaMetaData::AudioCodec).toString().trimmed();
	if (!codec.isEmpty()) {
		set_overview_value("Audio codec", codec);
	}
	const int bitrate = md.value(QMediaMetaData::AudioBitRate).toInt();
	if (bitrate > 0) {
		set_overview_value("Bitrate", QString("%1 kbps").arg(bitrate / 1000));
	}
}

void PreviewPane::update_cinematic_overview() {
	if (current_content_kind_ != ContentKind::Cinematic || !cinematic_widget_ || !cinematic_widget_->has_cinematic()) {
		return;
	}

	const CinematicInfo info = cinematic_widget_->cinematic_info();
	if (!info.format.isEmpty()) {
		set_overview_value("Format", info.format.toUpper());
	}
	if (info.width > 0 && info.height > 0) {
		set_overview_value("Dimensions", QString("%1x%2").arg(info.width).arg(info.height));
	}
	if (info.fps > 0.0) {
		set_overview_value("FPS", QString::number(info.fps, 'f', 2));
	}
	if (info.frame_count > 0) {
		set_overview_value("Frames", QString::number(info.frame_count));
		if (info.fps > 0.0) {
			const qint64 duration_ms = static_cast<qint64>((static_cast<double>(info.frame_count) / info.fps) * 1000.0);
			if (duration_ms > 0) {
				set_overview_value("Duration", format_duration(duration_ms));
			}
		}
	}
	if (info.has_audio) {
		set_overview_value("Has audio", "Yes");
		if (info.audio_sample_rate > 0) {
			set_overview_value("Sample rate", QString("%1 Hz").arg(info.audio_sample_rate));
		}
		if (info.audio_channels > 0) {
			set_overview_value("Channels", QString::number(info.audio_channels));
		}
	}
}

void PreviewPane::update_video_overview() {
	if (current_content_kind_ != ContentKind::Video || !video_widget_ || !video_widget_->has_media()) {
		return;
	}

	const qint64 duration = video_widget_->duration_ms();
	if (duration > 0) {
		set_overview_value("Duration", format_duration(duration));
	}

	const QSize sz = video_widget_->video_size();
	if (sz.isValid() && !sz.isEmpty()) {
		set_overview_value("Resolution", QString("%1x%2").arg(sz.width()).arg(sz.height()));
	}

	const QMediaMetaData md = video_widget_->meta_data();
	const QString fmt = md.stringValue(QMediaMetaData::FileFormat).trimmed();
	if (!fmt.isEmpty()) {
		set_overview_value("Format", fmt);
	}
	const QString vcodec = md.value(QMediaMetaData::VideoCodec).toString().trimmed();
	if (!vcodec.isEmpty()) {
		set_overview_value("Video codec", vcodec);
	}
	const QString acodec = md.value(QMediaMetaData::AudioCodec).toString().trimmed();
	if (!acodec.isEmpty()) {
		set_overview_value("Audio codec", acodec);
	}
	const double fps = md.value(QMediaMetaData::VideoFrameRate).toDouble();
	if (fps > 0.0) {
		set_overview_value("FPS", QString::number(fps, 'f', 2));
	}
}

/*
=============
PreviewPane::set_image_pixmap

Scale and display an image pixmap in the preview.
=============
*/
void PreviewPane::set_image_pixmap(const QPixmap& pixmap) {
	if (!image_label_) {
		return;
	}
	image_source_pixmap_ = pixmap;
	if (pixmap.isNull()) {
		image_label_->setPixmap(QPixmap());
		return;
	}

	// Adjust to the available viewport (fit or tile).
	const QSize avail =
		image_scroll_ ? image_scroll_->viewport()->size() : QSize();
	QPixmap source = pixmap;
	if (image_layout_mode_ == ImageLayoutMode::Fit && avail.isValid()) {
		const auto transform_mode = image_texture_smoothing_ ? Qt::SmoothTransformation : Qt::FastTransformation;
		source = pixmap.scaled(avail, Qt::KeepAspectRatio, transform_mode);
	}

	if (source.isNull()) {
		image_label_->setPixmap(QPixmap());
		return;
	}

	QPixmap base = source;
	if (image_bg_mode_ != ImageBackgroundMode::Transparent) {
		QPixmap composite(source.size());
		composite.setDevicePixelRatio(source.devicePixelRatio());
		if (image_bg_mode_ == ImageBackgroundMode::Solid) {
			QColor solid = image_bg_color_;
			if (!solid.isValid()) {
				solid = QColor(64, 64, 64);
			}
			composite.fill(solid);
		} else {
			const int square = 14;
			QColor a = image_bg_color_.lighter(120);
			QColor b = image_bg_color_.darker(120);
			if (!a.isValid()) {
				a = QColor(160, 160, 160);
			}
			if (!b.isValid()) {
				b = QColor(96, 96, 96);
			}

			QPixmap pattern(square * 2, square * 2);
			pattern.fill(a);
			{
				QPainter p(&pattern);
				p.fillRect(0, 0, square, square, b);
				p.fillRect(square, square, square, square, b);
			}

			composite.fill(a);
			{
				QPainter p(&composite);
				p.fillRect(composite.rect(), QBrush(pattern));
			}
		}

		{
			QPainter p(&composite);
			p.drawPixmap(0, 0, source);
		}
		base = composite;
	}

	if (image_layout_mode_ == ImageLayoutMode::Tile && avail.isValid()) {
		QPixmap tiled(avail);
		tiled.fill(Qt::transparent);
		{
			QPainter p(&tiled);
			p.fillRect(tiled.rect(), QBrush(base));
		}
		image_label_->setPixmap(tiled);
		return;
	}

	image_label_->setPixmap(base);
}

void PreviewPane::set_image_qimage(const QImage& image) {
	image_original_ = image;
	update_image_transparency_controls();
	apply_image_transparency_mode();
}

void PreviewPane::apply_image_background() {
	if (!image_scroll_) {
		return;
	}
	QWidget* vp = image_scroll_->viewport();
	if (!vp) {
		return;
	}

	QPalette pal = vp->palette();
	if (image_bg_color_button_) {
		image_bg_color_button_->setEnabled(image_bg_mode_ != ImageBackgroundMode::Transparent);
	}
	if (image_bg_mode_ == ImageBackgroundMode::Transparent) {
		vp->setAutoFillBackground(false);
		pal.setColor(QPalette::Window, Qt::transparent);
		pal.setBrush(QPalette::Window, Qt::NoBrush);
		vp->setPalette(pal);
		vp->update();
		apply_image_transparency_mode();
		return;
	}

	vp->setAutoFillBackground(true);
	const QColor base = palette().color(QPalette::Window);
	pal.setColor(QPalette::Window, base.isValid() ? base : QColor(32, 32, 32));
	pal.setBrush(QPalette::Window, Qt::NoBrush);
	vp->setPalette(pal);
	vp->update();
	apply_image_transparency_mode();
}

void PreviewPane::apply_image_transparency_mode() {
	if (image_original_.isNull()) {
		set_image_pixmap(QPixmap());
		return;
	}

	QImage display = image_original_;
	if (image_reveal_transparency_ && image_original_.hasAlphaChannel()) {
		display = image_original_.convertToFormat(QImage::Format_RGBA8888);
		uchar* bits = display.bits();
		const int stride = display.bytesPerLine();
		const int width = display.width();
		const int height = display.height();
		for (int y = 0; y < height; ++y) {
			uchar* row = bits + y * stride;
			for (int x = 0; x < width; ++x) {
				uchar* px = row + x * 4;
				if (px[3] == 0) {
					px[3] = 255;
				}
			}
		}
	}

	set_image_pixmap(display.isNull() ? QPixmap() : QPixmap::fromImage(display));
}

void PreviewPane::update_image_bg_button() {
	if (!image_bg_color_button_) {
		return;
	}
	QPixmap swatch(14, 14);
	swatch.fill(image_bg_color_);
	image_bg_color_button_->setIcon(QIcon(swatch));
	image_bg_color_button_->setToolTip(QString("Choose the background color behind transparent pixels.\nCurrent: %1").arg(image_bg_color_.name(QColor::HexArgb)));
}

void PreviewPane::update_image_transparency_controls() {
	if (!image_reveal_transparency_button_) {
		return;
	}
	const bool has_alpha = !image_original_.isNull() && image_original_.hasAlphaChannel();
	image_reveal_transparency_button_->setEnabled(has_alpha);
}

void PreviewPane::update_3d_bg_button() {
	if (!three_d_bg_color_button_) {
		return;
	}
	QPixmap swatch(14, 14);
	swatch.fill(three_d_bg_color_);
	three_d_bg_color_button_->setIcon(QIcon(swatch));
	three_d_bg_color_button_->setToolTip(QString("Choose the custom 3D background color.\nCurrent: %1").arg(three_d_bg_color_.name(QColor::HexArgb)));
}

void PreviewPane::apply_3d_bg_color_button_state() {
	if (!three_d_bg_color_button_) {
		return;
	}
	three_d_bg_color_button_->setEnabled(three_d_bg_mode_ == PreviewBackgroundMode::Custom);
}

/*
=============
PreviewPane::show_image_from_bytes

Decode and display an image from raw bytes.
=============
*/
void PreviewPane::show_image_from_bytes(const QString& title,
									   const QString& subtitle,
									   const QByteArray& bytes,
									   const ImageDecodeOptions& options) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	const ImageDecodeResult decoded = decode_image_bytes(bytes, title, options);
	if (!decoded.ok()) {
		show_message(title, decoded.error.isEmpty() ? "Unable to decode this image format." : decoded.error);
		return;
	}
	show_image(title, subtitle, decoded.image);
}

/*
=============
PreviewPane::show_image_from_file

Load and display an image from a file path.
=============
*/
void PreviewPane::show_image_from_file(const QString& title,
								  const QString& subtitle,
								  const QString& file_path,
								  const ImageDecodeOptions& options) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	const ImageDecodeResult decoded = decode_image_file(file_path, options);
	if (!decoded.ok()) {
		show_message(title, decoded.error.isEmpty() ? "Unable to load this image file." : decoded.error);
		return;
	}
	show_image(title, subtitle, decoded.image);
}

/*
=============
PreviewPane::show_audio_from_file

Prepare the audio player controls for a selected audio file.
=============
*/
void PreviewPane::show_audio_from_file(const QString& title,
								const QString& subtitle,
								const QString& file_path) {
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Audio;
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Audio");

	set_audio_source(file_path);
	update_audio_overview();
	show_content_block("Multimedia Control", audio_page_);
}

void PreviewPane::show_cinematic_from_file(const QString& title, const QString& subtitle, const QString& file_path) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Cinematic;
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Cinematic");

	if (!cinematic_widget_ || !cinematic_page_) {
		show_message(title, "Cinematic preview is not available.");
		return;
	}

	show_content_block("Multimedia Control", cinematic_page_);

	QString err;
	if (!cinematic_widget_->load_file(file_path, &err)) {
		show_message(title, err.isEmpty() ? "Unable to load cinematic." : err);
		return;
	}
	update_cinematic_overview();
}

void PreviewPane::show_video_from_file(const QString& title, const QString& subtitle, const QString& file_path) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Video;
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Video");

	if (!video_widget_ || !video_page_) {
		show_message(title, "Video preview is not available.");
		return;
	}

	show_content_block("Multimedia Control", video_page_);

	QString err;
	if (!video_widget_->load_file(file_path, &err)) {
		show_message(title, err.isEmpty() ? "Unable to load video." : err);
		return;
	}
	update_video_overview();
}

void PreviewPane::show_model_from_file(const QString& title,
                                        const QString& subtitle,
                                        const QString& file_path,
                                        const QString& skin_path) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Model;
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Model");

	if (!model_widget_ || !model_page_) {
		show_message(title, "Model preview is not available.");
		return;
	}

	show_content_block("3D Panel", model_page_);
	if (three_d_controls_) {
		three_d_controls_->setVisible(true);
	}
	if (bsp_lightmap_button_) {
		bsp_lightmap_button_->setVisible(false);
	}

	QString err;
	bool ok = false;
	if (auto* gl = as_gl_model_widget(model_widget_)) {
		ok = skin_path.isEmpty() ? gl->load_file(file_path, &err)
								 : gl->load_file(file_path, skin_path, &err);
	} else if (auto* vk = as_vk_model_widget(model_widget_)) {
		ok = skin_path.isEmpty() ? vk->load_file(file_path, &err)
								 : vk->load_file(file_path, skin_path, &err);
	}
	if (!ok) {
		has_cached_model_ = false;
		show_message(title, err.isEmpty() ? "Unable to load model." : err);
		return;
	}
	cached_model_file_path_ = file_path;
	cached_model_skin_path_ = skin_path;
	has_cached_model_ = true;
	if (three_d_fullscreen_window_ && three_d_fullscreen_page_ == model_page_ && three_d_fullscreen_widget_) {
		QString fs_err;
		bool fs_ok = false;
		if (auto* gl = as_gl_model_widget(three_d_fullscreen_widget_)) {
			fs_ok = skin_path.isEmpty() ? gl->load_file(file_path, &fs_err)
			                            : gl->load_file(file_path, skin_path, &fs_err);
		} else if (auto* vk = as_vk_model_widget(three_d_fullscreen_widget_)) {
			fs_ok = skin_path.isEmpty() ? vk->load_file(file_path, &fs_err)
			                            : vk->load_file(file_path, skin_path, &fs_err);
		}
		if (!fs_ok) {
			qWarning() << "PreviewPane: fullscreen model refresh failed:" << fs_err;
		}
	}

	QString fmt;
	ModelMesh mesh;
	if (auto* gl = as_gl_model_widget(model_widget_)) {
		fmt = gl->model_format().toUpper();
		mesh = gl->mesh();
	} else if (auto* vk = as_vk_model_widget(model_widget_)) {
		fmt = vk->model_format().toUpper();
		mesh = vk->mesh();
	}
	if (!fmt.isEmpty()) {
		set_overview_value("Format", fmt);
	}
	set_overview_value("Vertices", QString::number(mesh.vertices.size()));
	set_overview_value("Triangles", QString::number(mesh.indices.size() / 3));
}

void PreviewPane::set_model_texture_smoothing(bool enabled) {
	model_texture_smoothing_ = enabled;
	if (auto* gl = as_gl_model_widget(model_widget_)) {
		gl->set_texture_smoothing(enabled);
	} else if (auto* vk = as_vk_model_widget(model_widget_)) {
		vk->set_texture_smoothing(enabled);
	}
	if (auto* gl = as_gl_model_widget(three_d_fullscreen_widget_)) {
		gl->set_texture_smoothing(enabled);
	} else if (auto* vk = as_vk_model_widget(three_d_fullscreen_widget_)) {
		vk->set_texture_smoothing(enabled);
	}
}

void PreviewPane::set_image_texture_smoothing(bool enabled) {
	image_texture_smoothing_ = enabled;
	// Re-render the current image with the new setting if one is displayed.
	if (image_card_ && image_card_->isVisible() && !image_source_pixmap_.isNull()) {
		set_image_pixmap(image_source_pixmap_);
	}
	// Also update the cinematic player widget.
	if (cinematic_widget_) {
		cinematic_widget_->set_texture_smoothing(enabled);
	}
	if (video_widget_) {
		video_widget_->set_texture_smoothing(enabled);
	}
}

void PreviewPane::set_image_mip_controls(bool visible, int mip_level) {
	if (!image_mip_label_ || !image_mip_combo_) {
		return;
	}
	image_mip_label_->setVisible(visible);
	image_mip_combo_->setVisible(visible);
	if (!visible) {
		image_mip_level_ = 0;
		return;
	}
	const int clamped = qBound(0, mip_level, 3);
	image_mip_level_ = clamped;
	const bool was_blocked = image_mip_combo_->blockSignals(true);
	const int idx = image_mip_combo_->findData(clamped);
	if (idx >= 0) {
		image_mip_combo_->setCurrentIndex(idx);
	}
	image_mip_combo_->blockSignals(was_blocked);
}

void PreviewPane::set_model_palettes(const QVector<QRgb>& quake1_palette, const QVector<QRgb>& quake2_palette) {
	model_palette_quake1_ = quake1_palette;
	model_palette_quake2_ = quake2_palette;
	if (auto* gl = as_gl_model_widget(model_widget_)) {
		gl->set_palettes(quake1_palette, quake2_palette);
	} else if (auto* vk = as_vk_model_widget(model_widget_)) {
		vk->set_palettes(quake1_palette, quake2_palette);
	}
	if (auto* gl = as_gl_model_widget(three_d_fullscreen_widget_)) {
		gl->set_palettes(quake1_palette, quake2_palette);
	} else if (auto* vk = as_vk_model_widget(three_d_fullscreen_widget_)) {
		vk->set_palettes(quake1_palette, quake2_palette);
	}
}

void PreviewPane::set_glow_enabled(bool enabled) {
	glow_enabled_ = enabled;
	if (auto* gl = as_gl_model_widget(model_widget_)) {
		gl->set_glow_enabled(enabled);
	} else if (auto* vk = as_vk_model_widget(model_widget_)) {
		vk->set_glow_enabled(enabled);
	}
	if (auto* gl = as_gl_model_widget(three_d_fullscreen_widget_)) {
		gl->set_glow_enabled(enabled);
	} else if (auto* vk = as_vk_model_widget(three_d_fullscreen_widget_)) {
		vk->set_glow_enabled(enabled);
	}
}

void PreviewPane::apply_3d_settings() {
	auto apply = [&](QWidget* widget) {
		apply_3d_visual_settings(widget,
		                         three_d_grid_mode_,
		                         three_d_bg_mode_,
		                         three_d_bg_color_,
		                         three_d_wireframe_enabled_,
		                         three_d_textured_enabled_,
		                         three_d_fov_degrees_,
		                         glow_enabled_);
	};

	apply(bsp_widget_);
	apply(model_widget_);
	apply(three_d_fullscreen_widget_);
}

void PreviewPane::start_playback_from_beginning() {
	if (stack_ && audio_page_ && stack_->currentWidget() == audio_page_) {
		if (!audio_player_ || audio_file_path_.isEmpty()) {
			return;
		}
		audio_player_->setPosition(0);
		audio_player_->play();
		return;
	}

	if (stack_ && cinematic_page_ && stack_->currentWidget() == cinematic_page_) {
		if (cinematic_widget_) {
			cinematic_widget_->play_from_start();
		}
		return;
	}

	if (stack_ && video_page_ && stack_->currentWidget() == video_page_) {
		if (video_widget_) {
			video_widget_->play_from_start();
		}
		return;
	}

	if (current_content_kind_ == ContentKind::Sprite && !sprite_frames_.isEmpty()) {
		apply_sprite_frame(0);
		schedule_next_sprite_frame();
	}
}

bool PreviewPane::is_shader_view_active() const {
	return current_content_kind_ == ContentKind::Shader && stack_ && shader_page_ && stack_->currentWidget() == shader_page_ && shader_widget_;
}

QString PreviewPane::selected_shader_blocks_text() const {
	if (!is_shader_view_active() || !shader_widget_) {
		return {};
	}
	return shader_widget_->selected_shader_script_text();
}

/*
=============
PreviewPane::stop_audio_playback

Stop any active audio playback and reset the player state.
=============
*/
void PreviewPane::stop_audio_playback() {
	if (!audio_player_) {
		return;
	}
	if (audio_player_->playbackState() != QMediaPlayer::StoppedState) {
		audio_player_->stop();
	}
}

void PreviewPane::stop_cinematic_playback() {
	if (cinematic_widget_) {
		cinematic_widget_->unload();
	}
}

void PreviewPane::stop_video_playback() {
	if (video_widget_) {
		video_widget_->unload();
	}
}

void PreviewPane::stop_model_preview() {
	stop_sprite_animation();
	if (three_d_fullscreen_window_ && three_d_fullscreen_page_ == model_page_) {
		exit_3d_fullscreen();
	}
	if (auto* gl = as_gl_model_widget(model_widget_)) {
		gl->unload();
	} else if (auto* vk = as_vk_model_widget(model_widget_)) {
		vk->unload();
	}
}

/*
=============
PreviewPane::set_audio_source

Load a new audio source into the player and reset UI controls.
=============
*/
void PreviewPane::set_audio_source(const QString& file_path) {
	if (!audio_player_) {
		return;
	}
	audio_file_path_.clear();
	audio_player_->stop();
	audio_player_->setSource(QUrl::fromLocalFile(file_path));
	audio_file_path_ = file_path;
	sync_audio_controls();
	update_audio_tooltip();
	update_audio_status_label();
}

/*
=============
PreviewPane::sync_audio_controls

Reset audio control widgets to a consistent initial state.
=============
*/
void PreviewPane::sync_audio_controls() {
	if (audio_position_slider_) {
		audio_position_slider_->setRange(0, 0);
		audio_position_slider_->setValue(0);
	}
	if (audio_play_button_) {
		audio_play_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	}
}

/*
=============
PreviewPane::update_audio_tooltip

Populate the audio details tooltip from metadata and file info.
=============
*/
void PreviewPane::update_audio_tooltip() {
	if (!audio_info_button_) {
		return;
	}
	QStringList lines;
	if (!audio_file_path_.isEmpty()) {
		const QFileInfo info(audio_file_path_);
		lines << QString("File: %1").arg(info.fileName());
		lines << QString("Size: %1 bytes").arg(info.size());
		if (!info.suffix().isEmpty()) {
			lines << QString("Format: %1").arg(info.suffix().toLower());
		}
	}
	if (audio_player_) {
		const qint64 duration = audio_player_->duration();
		if (duration > 0) {
			lines << QString("Duration: %1").arg(format_duration(duration));
		}
		const QMediaMetaData meta = audio_player_->metaData();
		const QVariant title = meta.value(QMediaMetaData::Title);
		if (title.isValid() && !title.toString().isEmpty()) {
			lines << QString("Title: %1").arg(title.toString());
		}
		const QVariant artist = meta.value(QMediaMetaData::ContributingArtist);
		if (artist.isValid()) {
			const QStringList artists = artist.toStringList();
			if (!artists.isEmpty()) {
				lines << QString("Artist: %1").arg(artists.join(", "));
			}
		}
		const QVariant bitrate = meta.value(QMediaMetaData::AudioBitRate);
		if (bitrate.isValid()) {
			lines << QString("Bitrate: %1 kbps").arg(bitrate.toInt() / 1000);
		}
		const QVariant codec = meta.value(QMediaMetaData::AudioCodec);
		if (codec.isValid() && !codec.toString().isEmpty()) {
			lines << QString("Codec: %1").arg(codec.toString());
		}
	}
	if (lines.isEmpty()) {
		lines << "Audio details unavailable.";
	}
	audio_info_button_->setToolTip(lines.join("\n"));
}

void PreviewPane::update_audio_status_label() {
	if (!audio_status_label_) {
		return;
	}
	if (!audio_player_ || audio_file_path_.isEmpty()) {
		audio_status_label_->clear();
		return;
	}

	const qint64 position = audio_player_->position();
	const qint64 duration = audio_player_->duration();
	QString text;
	if (duration > 0) {
		text = QString("%1 / %2").arg(format_duration(position), format_duration(duration));
	} else if (position > 0) {
		text = format_duration(position);
	}

	const QString name = QFileInfo(audio_file_path_).fileName();
	if (!name.isEmpty()) {
		text = text.isEmpty() ? name : (text + "  •  " + name);
	}

	audio_status_label_->setText(text);
}
void PreviewPane::show_json(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::Json);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "JSON");
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}

void PreviewPane::show_menu(const QString& title, const QString& subtitle, const QString& text) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::Quake3Menu);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Menu");
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}

void PreviewPane::show_shader(const QString& title,
                              const QString& subtitle,
                              const QString& text,
                              const Quake3ShaderDocument& document,
                              QHash<QString, QImage> textures) {
	stop_audio_playback();
	stop_cinematic_playback();
	stop_video_playback();
	stop_model_preview();
	current_content_kind_ = ContentKind::Shader;
	set_text_highlighter(TextSyntax::None);
	set_header(title, subtitle);
	if (image_card_) {
		image_card_->setVisible(false);
	}
	clear_overview_fields();
	show_overview_block(true);
	populate_basic_overview();
	set_overview_value("Type", "Shader Script");
	set_overview_value("Shaders", QString::number(document.shaders.size()));
	if (shader_widget_) {
		shader_widget_->set_document(text, document, std::move(textures));
		update_shader_viewport_width();
		show_content_block("Shader Tiles", shader_page_);
		return;
	}

	// Fallback path if the dedicated widget isn't available.
	current_content_kind_ = ContentKind::Text;
	set_text_highlighter(TextSyntax::Quake3Shader);
	if (text_view_) {
		text_view_->setPlainText(text);
	}
	show_content_block("Text View", text_page_);
}
