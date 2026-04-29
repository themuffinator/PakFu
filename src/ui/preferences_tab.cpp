#include "preferences_tab.h"

#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ui/theme_manager.h"

namespace {
constexpr char kArchiveOpenAlwaysAskKey[] = "archive/openChooserAlwaysAsk";
constexpr char kArchiveOpenDefaultActionKey[] = "archive/openChooserDefaultAction";
constexpr char kRecentFileLimitKey[] = "ui/recentFilesLimit";
constexpr int kDefaultRecentFileLimit = 12;

constexpr char kImageBackgroundModeKey[] = "preview/image/backgroundMode";
constexpr char kImageBackgroundColorKey[] = "preview/image/backgroundColor";
constexpr char kImageLayoutKey[] = "preview/image/layout";
constexpr char kImageRevealTransparentKey[] = "preview/image/revealTransparent";
constexpr char kImageTextureSmoothingKey[] = "preview/image/textureSmoothing";
constexpr char kTextWordWrapKey[] = "preview/text/wordWrap";
constexpr char kModelTextureSmoothingKey[] = "preview/model/textureSmoothing";
constexpr char kModelAnimPlayingKey[] = "preview/model/animPlaying";
constexpr char kModelAnimLoopKey[] = "preview/model/animLoop";
constexpr char kModelSkeletonKey[] = "preview/model/skeletonOverlay";
constexpr char kModelAnimSpeedKey[] = "preview/model/animSpeed";
constexpr char kPreviewFovKey[] = "preview/3d/fov";
constexpr char kPreviewGridKey[] = "preview/3d/gridMode";
constexpr char kPreviewBackgroundModeKey[] = "preview/3d/backgroundMode";
constexpr char kPreviewBackgroundColorKey[] = "preview/3d/backgroundColor";
constexpr char kPreviewWireframeKey[] = "preview/3d/wireframe";
constexpr char kPreviewTexturedKey[] = "preview/3d/textured";
constexpr char kBspLightmappingKey[] = "preview/bsp/lightmapping";
constexpr char kPurePakProtectorKey[] = "archive/purePakProtector";
constexpr char kUpdatesAutoCheckKey[] = "updates/autoCheck";
constexpr char kUpdatesSkipVersionKey[] = "updates/skipVersion";

AppTheme theme_for_index(int idx) {
	switch (idx) {
		case 1:
			return AppTheme::Light;
		case 2:
			return AppTheme::Dark;
		case 3:
			return AppTheme::Midnight;
		case 4:
			return AppTheme::SpringTime;
		case 5:
			return AppTheme::CreamyGoodness;
		case 6:
			return AppTheme::VibeORama;
		case 7:
			return AppTheme::DarkMatter;
		default:
			return AppTheme::System;
	}
}

int index_for_theme(AppTheme theme) {
	switch (theme) {
		case AppTheme::System:
			return 0;
		case AppTheme::Light:
			return 1;
		case AppTheme::Dark:
			return 2;
		case AppTheme::Midnight:
			return 3;
		case AppTheme::SpringTime:
			return 4;
		case AppTheme::CreamyGoodness:
			return 5;
		case AppTheme::VibeORama:
			return 6;
		case AppTheme::DarkMatter:
			return 7;
	}
	return 0;
}

QWidget* make_scroll_page(QWidget* parent, QVBoxLayout** out_layout) {
	auto* scroll = new QScrollArea(parent);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto* page = new QWidget(scroll);
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(22, 22, 22, 22);
	layout->setSpacing(16);

	scroll->setWidget(page);
	if (out_layout) {
		*out_layout = layout;
	}
	return scroll;
}

QGroupBox* make_group(QWidget* parent, const QString& title, const QString& detail, QVBoxLayout** out_layout) {
	auto* group = new QGroupBox(title, parent);
	auto* layout = new QVBoxLayout(group);
	layout->setContentsMargins(16, 18, 16, 16);
	layout->setSpacing(10);
	if (!detail.isEmpty()) {
		auto* label = new QLabel(detail, group);
		label->setWordWrap(true);
		layout->addWidget(label);
	}
	if (out_layout) {
		*out_layout = layout;
	}
	return group;
}

QLabel* make_hint(const QString& text, QWidget* parent) {
	auto* label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setStyleSheet("color: palette(mid);");
	return label;
}

void set_combo_data(QComboBox* combo, const QString& value, int fallback = 0) {
	if (!combo) {
		return;
	}
	const int idx = combo->findData(value);
	combo->setCurrentIndex(idx >= 0 ? idx : fallback);
}

void set_speed_combo(QComboBox* combo, double value) {
	if (!combo || combo->count() <= 0) {
		return;
	}
	int best_index = 0;
	double best_delta = 1000.0;
	for (int i = 0; i < combo->count(); ++i) {
		const double candidate = combo->itemData(i).toDouble();
		const double delta = std::abs(candidate - value);
		if (delta < best_delta) {
			best_delta = delta;
			best_index = i;
		}
	}
	combo->setCurrentIndex(best_index);
}

QColor setting_color(QSettings& settings, const QString& key, const QColor& fallback) {
	const QVariant raw = settings.value(key);
	QColor color;
	if (raw.canConvert<QColor>()) {
		color = raw.value<QColor>();
	} else {
		color = QColor(raw.toString());
	}
	return color.isValid() ? color : fallback;
}

void style_color_button(QPushButton* button, const QColor& color) {
	if (!button) {
		return;
	}
	const QColor c = color.isValid() ? color : QColor(96, 96, 96);
	const QString name = c.name(QColor::HexRgb);
	button->setText(name.toUpper());
	button->setStyleSheet(QString("QPushButton { text-align: left; padding-left: 10px; border: 1px solid palette(mid);"
	                              " background: %1; color: %2; }")
	                         .arg(name, c.lightness() < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#111111")));
}
}  // namespace

PreferencesTab::PreferencesTab(QWidget* parent) : QWidget(parent) {
	build_ui();
	load_settings();
}

void PreferencesTab::build_ui() {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(28, 22, 28, 22);
	layout->setSpacing(14);

	auto* title = new QLabel("Preferences", this);
	QFont title_font = title->font();
	title_font.setPointSize(title_font.pointSize() + 6);
	title_font.setWeight(QFont::DemiBold);
	title->setFont(title_font);
	layout->addWidget(title);

	auto* intro = new QLabel("Tune PakFu's workspace, archive safety, preview rendering, media display, text inspection, and update behavior.", this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	tabs_ = new QTabWidget(this);
	tabs_->addTab(build_appearance_tab(), "Appearance");
	tabs_->addTab(build_archives_tab(), "Workspace & Archives");
	tabs_->addTab(build_previews_tab(), "3D Preview");
	tabs_->addTab(build_images_text_tab(), "Images & Text");
	tabs_->addTab(build_updates_tab(), "Updates");
	layout->addWidget(tabs_, 1);
}

QWidget* PreferencesTab::build_appearance_tab() {
	QVBoxLayout* layout = nullptr;
	QWidget* scroll = make_scroll_page(this, &layout);

	QVBoxLayout* group_layout = nullptr;
	layout->addWidget(make_group(scroll, "Theme", "Choose the application palette used by the main window, archive tabs, viewers, and dialogs.", &group_layout));

	auto* form = new QFormLayout();
	form->setLabelAlignment(Qt::AlignLeft);
	form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
	theme_combo_ = new QComboBox(scroll);
	theme_combo_->addItem("System default");
	theme_combo_->addItem("Light");
	theme_combo_->addItem("Dark");
	theme_combo_->addItem("Midnight");
	theme_combo_->addItem("Spring Time");
	theme_combo_->addItem("Creamy Goodness");
	theme_combo_->addItem("Vibe-o-Rama");
	theme_combo_->addItem("DarkMatter");
	theme_combo_->setMinimumWidth(240);
	form->addRow("Color theme", theme_combo_);
	group_layout->addLayout(form);

	auto* reset = new QPushButton("Restore Appearance Defaults", scroll);
	group_layout->addWidget(reset, 0, Qt::AlignLeft);
	layout->addStretch();

	connect(theme_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_) {
			return;
		}
		apply_theme_from_combo();
	});
	connect(reset, &QPushButton::clicked, this, &PreferencesTab::reset_appearance_settings);

	return scroll;
}

QWidget* PreferencesTab::build_archives_tab() {
	QVBoxLayout* layout = nullptr;
	QWidget* scroll = make_scroll_page(this, &layout);

	QVBoxLayout* open_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Opening Archives",
	                            "Control how PakFu handles archives opened from the shell, drag-and-drop, recent files, or the File menu.",
	                            &open_layout));
	archive_open_always_ask_ = new QCheckBox("Ask how supported archives should be opened", scroll);
	open_layout->addWidget(archive_open_always_ask_);

	archive_open_action_combo_ = new QComboBox(scroll);
	archive_open_action_combo_->addItem("Open in place", "open_direct");
	archive_open_action_combo_->addItem("Install a copy into the selected game profile", "install_copy");
	archive_open_action_combo_->addItem("Move into the selected game profile", "move_to_install");
	auto* action_form = new QFormLayout();
	action_form->addRow("Default action when not asking", archive_open_action_combo_);
	open_layout->addLayout(action_form);

	QVBoxLayout* history_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "History",
	                            "Recent files are stored per installation profile so active projects stay separate.",
	                            &history_layout));
	recent_file_limit_spin_ = new QSpinBox(scroll);
	recent_file_limit_spin_->setRange(1, 50);
	recent_file_limit_spin_->setSuffix(" files");
	recent_file_limit_spin_->setMinimumWidth(140);
	auto* recent_form = new QFormLayout();
	recent_form->addRow("Recent files to keep", recent_file_limit_spin_);
	history_layout->addLayout(recent_form);

	QVBoxLayout* safety_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Protection",
	                            "Keep known stock game archives read-only unless you deliberately disable the guardrail.",
	                            &safety_layout));
	pure_pak_protector_ = new QCheckBox("Pure PAK protector for official archives", scroll);
	safety_layout->addWidget(pure_pak_protector_);

	auto* reset = new QPushButton("Restore Workspace & Archive Defaults", scroll);
	safety_layout->addWidget(reset, 0, Qt::AlignLeft);
	layout->addStretch();

	connect(archive_open_always_ask_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kArchiveOpenAlwaysAskKey, checked);
	});
	connect(archive_open_action_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !archive_open_action_combo_) {
			return;
		}
		QSettings s;
		s.setValue(kArchiveOpenDefaultActionKey, archive_open_action_combo_->currentData().toString());
	});
	connect(recent_file_limit_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
		if (loading_) {
			return;
		}
		const int limit = qBound(1, value, 50);
		QSettings s;
		s.setValue(kRecentFileLimitKey, limit);
		emit recent_file_limit_changed(limit);
	});
	connect(pure_pak_protector_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kPurePakProtectorKey, checked);
		emit pure_pak_protector_changed(checked);
	});
	connect(reset, &QPushButton::clicked, this, &PreferencesTab::reset_archive_settings);

	return scroll;
}

QWidget* PreferencesTab::build_previews_tab() {
	QVBoxLayout* layout = nullptr;
	QWidget* scroll = make_scroll_page(this, &layout);

	QVBoxLayout* renderer_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Renderer",
	                            "Choose the backend and camera defaults used by BSP, map, and model previews.",
	                            &renderer_layout));
	renderer_combo_ = new QComboBox(scroll);
	renderer_combo_->addItem("Vulkan", preview_renderer_to_string(PreviewRenderer::Vulkan));
	renderer_combo_->addItem("OpenGL", preview_renderer_to_string(PreviewRenderer::OpenGL));
	renderer_combo_->setMinimumWidth(220);
	auto* renderer_form = new QFormLayout();
	renderer_form->addRow("3D renderer", renderer_combo_);
	renderer_layout->addLayout(renderer_form);

	renderer_status_ = make_hint(is_vulkan_renderer_available()
	                               ? "Vulkan is available in this build. OpenGL remains available as a compatibility fallback."
	                               : "Vulkan is not available in this build. OpenGL will be used even if Vulkan is requested.",
	                             scroll);
	renderer_layout->addWidget(renderer_status_);

	auto* fov_row = new QWidget(scroll);
	auto* fov_layout = new QHBoxLayout(fov_row);
	fov_layout->setContentsMargins(0, 0, 0, 0);
	fov_layout->setSpacing(8);
	preview_fov_slider_ = new QSlider(Qt::Horizontal, fov_row);
	preview_fov_slider_->setRange(40, 120);
	preview_fov_slider_->setSingleStep(1);
	preview_fov_slider_->setPageStep(5);
	preview_fov_slider_->setTickInterval(10);
	preview_fov_slider_->setTickPosition(QSlider::TicksBelow);
	fov_layout->addWidget(preview_fov_slider_, 1);
	preview_fov_value_label_ = new QLabel("100 deg", fov_row);
	preview_fov_value_label_->setMinimumWidth(62);
	preview_fov_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	fov_layout->addWidget(preview_fov_value_label_);
	renderer_form->addRow("Field of view", fov_row);

	QVBoxLayout* scene_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Scene Defaults",
	                            "Set the default staging for inspecting maps and models. These defaults also update open preview panes.",
	                            &scene_layout));
	three_d_grid_combo_ = new QComboBox(scroll);
	three_d_grid_combo_->addItem("Floor grid", "floor");
	three_d_grid_combo_->addItem("Full grid", "grid");
	three_d_grid_combo_->addItem("No grid", "none");
	three_d_background_combo_ = new QComboBox(scroll);
	three_d_background_combo_->addItem("Follow theme", "themed");
	three_d_background_combo_->addItem("Neutral grey", "grey");
	three_d_background_combo_->addItem("Custom color", "custom");
	three_d_background_color_button_ = new QPushButton(scroll);
	three_d_background_color_button_->setMinimumWidth(130);

	auto* scene_form = new QFormLayout();
	scene_form->addRow("Grid", three_d_grid_combo_);
	scene_form->addRow("Background", three_d_background_combo_);
	scene_form->addRow("Custom color", three_d_background_color_button_);
	scene_layout->addLayout(scene_form);

	three_d_textured_ = new QCheckBox("Textured rendering by default", scroll);
	three_d_wireframe_ = new QCheckBox("Wireframe overlay by default", scroll);
	bsp_lightmapping_ = new QCheckBox("Use BSP lightmaps when available", scroll);
	scene_layout->addWidget(three_d_textured_);
	scene_layout->addWidget(three_d_wireframe_);
	scene_layout->addWidget(bsp_lightmapping_);

	QVBoxLayout* model_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Model Playback",
	                            "Choose how animated models behave when first previewed.",
	                            &model_layout));
	model_texture_smoothing_ = new QCheckBox("Smooth model textures", scroll);
	model_animation_playing_ = new QCheckBox("Start model animations playing", scroll);
	model_animation_loop_ = new QCheckBox("Loop model animations", scroll);
	model_skeleton_overlay_ = new QCheckBox("Show skeleton overlay", scroll);
	model_animation_speed_combo_ = new QComboBox(scroll);
	model_animation_speed_combo_->addItem("0.25x", 0.25);
	model_animation_speed_combo_->addItem("0.5x", 0.5);
	model_animation_speed_combo_->addItem("1.0x", 1.0);
	model_animation_speed_combo_->addItem("1.5x", 1.5);
	model_animation_speed_combo_->addItem("2.0x", 2.0);
	model_animation_speed_combo_->addItem("3.0x", 3.0);
	model_animation_speed_combo_->addItem("4.0x", 4.0);
	model_layout->addWidget(model_texture_smoothing_);
	model_layout->addWidget(model_animation_playing_);
	model_layout->addWidget(model_animation_loop_);
	model_layout->addWidget(model_skeleton_overlay_);
	auto* model_form = new QFormLayout();
	model_form->addRow("Animation speed", model_animation_speed_combo_);
	model_layout->addLayout(model_form);

	auto* reset = new QPushButton("Restore 3D Preview Defaults", scroll);
	model_layout->addWidget(reset, 0, Qt::AlignLeft);
	layout->addStretch();

	connect(renderer_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !renderer_combo_) {
			return;
		}
		const PreviewRenderer renderer = preview_renderer_from_string(renderer_combo_->currentData().toString());
		save_preview_renderer(renderer);
		emit preview_renderer_changed(renderer);
		notify_preview_preferences_changed();
	});
	connect(preview_fov_slider_, &QSlider::valueChanged, this, [this](int value) {
		const int clamped = qBound(40, value, 120);
		if (preview_fov_value_label_) {
			preview_fov_value_label_->setText(QString("%1 deg").arg(clamped));
		}
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kPreviewFovKey, clamped);
		emit preview_fov_changed(clamped);
		notify_preview_preferences_changed();
	});
	connect(three_d_grid_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !three_d_grid_combo_) {
			return;
		}
		QSettings s;
		s.setValue(kPreviewGridKey, three_d_grid_combo_->currentData().toString());
		notify_preview_preferences_changed();
	});
	connect(three_d_background_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !three_d_background_combo_) {
			return;
		}
		QSettings s;
		s.setValue(kPreviewBackgroundModeKey, three_d_background_combo_->currentData().toString());
		notify_preview_preferences_changed();
	});
	connect(three_d_background_color_button_, &QPushButton::clicked, this, &PreferencesTab::choose_3d_background_color);
	connect(three_d_textured_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kPreviewTexturedKey, checked);
		notify_preview_preferences_changed();
	});
	connect(three_d_wireframe_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kPreviewWireframeKey, checked);
		notify_preview_preferences_changed();
	});
	connect(bsp_lightmapping_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kBspLightmappingKey, checked);
		notify_preview_preferences_changed();
	});
	connect(model_texture_smoothing_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kModelTextureSmoothingKey, checked);
		emit model_texture_smoothing_changed(checked);
		notify_preview_preferences_changed();
	});
	connect(model_animation_playing_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kModelAnimPlayingKey, checked);
		notify_preview_preferences_changed();
	});
	connect(model_animation_loop_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kModelAnimLoopKey, checked);
		notify_preview_preferences_changed();
	});
	connect(model_skeleton_overlay_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kModelSkeletonKey, checked);
		notify_preview_preferences_changed();
	});
	connect(model_animation_speed_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !model_animation_speed_combo_) {
			return;
		}
		QSettings s;
		s.setValue(kModelAnimSpeedKey, model_animation_speed_combo_->currentData().toDouble());
		notify_preview_preferences_changed();
	});
	connect(reset, &QPushButton::clicked, this, &PreferencesTab::reset_preview_settings);

	return scroll;
}

QWidget* PreferencesTab::build_images_text_tab() {
	QVBoxLayout* layout = nullptr;
	QWidget* scroll = make_scroll_page(this, &layout);

	QVBoxLayout* image_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Image and Video Preview",
	                            "Control image scaling, transparency visualization, and media texture filtering.",
	                            &image_layout));
	image_texture_smoothing_ = new QCheckBox("Smooth image, cinematic, and video textures", scroll);
	image_layout->addWidget(image_texture_smoothing_);

	image_background_combo_ = new QComboBox(scroll);
	image_background_combo_->addItem("Checkerboard", "checkerboard");
	image_background_combo_->addItem("Solid color", "solid");
	image_background_combo_->addItem("Transparent", "transparent");
	image_background_color_button_ = new QPushButton(scroll);
	image_background_color_button_->setMinimumWidth(130);
	image_layout_combo_ = new QComboBox(scroll);
	image_layout_combo_->addItem("Fit to viewport", "fit");
	image_layout_combo_->addItem("Tile at original size", "tile");
	image_reveal_transparency_ = new QCheckBox("Reveal fully transparent pixels", scroll);

	auto* image_form = new QFormLayout();
	image_form->addRow("Background", image_background_combo_);
	image_form->addRow("Background color", image_background_color_button_);
	image_form->addRow("Layout", image_layout_combo_);
	image_layout->addLayout(image_form);
	image_layout->addWidget(image_reveal_transparency_);

	QVBoxLayout* text_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Text Inspection",
	                            "Set defaults for CFG, shader, menu, JSON, script, and binary-adjacent text previews.",
	                            &text_layout));
	text_word_wrap_ = new QCheckBox("Wrap long lines in text previews", scroll);
	text_layout->addWidget(text_word_wrap_);

	auto* reset = new QPushButton("Restore Images & Text Defaults", scroll);
	text_layout->addWidget(reset, 0, Qt::AlignLeft);
	layout->addStretch();

	connect(image_texture_smoothing_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kImageTextureSmoothingKey, checked);
		emit image_texture_smoothing_changed(checked);
		notify_preview_preferences_changed();
	});
	connect(image_background_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !image_background_combo_) {
			return;
		}
		QSettings s;
		s.setValue(kImageBackgroundModeKey, image_background_combo_->currentData().toString());
		notify_preview_preferences_changed();
	});
	connect(image_background_color_button_, &QPushButton::clicked, this, &PreferencesTab::choose_image_background_color);
	connect(image_layout_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
		if (loading_ || !image_layout_combo_) {
			return;
		}
		QSettings s;
		s.setValue(kImageLayoutKey, image_layout_combo_->currentData().toString());
		notify_preview_preferences_changed();
	});
	connect(image_reveal_transparency_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kImageRevealTransparentKey, checked);
		notify_preview_preferences_changed();
	});
	connect(text_word_wrap_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kTextWordWrapKey, checked);
		notify_preview_preferences_changed();
	});
	connect(reset, &QPushButton::clicked, this, &PreferencesTab::reset_image_text_settings);

	return scroll;
}

QWidget* PreferencesTab::build_updates_tab() {
	QVBoxLayout* layout = nullptr;
	QWidget* scroll = make_scroll_page(this, &layout);

	QVBoxLayout* updates_layout = nullptr;
	layout->addWidget(make_group(scroll,
	                            "Release Checks",
	                            "PakFu can check GitHub Releases after the main window opens. Manual checks remain available from the Help menu.",
	                            &updates_layout));
	updates_auto_check_ = new QCheckBox("Check for updates automatically", scroll);
	updates_layout->addWidget(updates_auto_check_);

	auto* reset = new QPushButton("Restore Update Defaults", scroll);
	updates_layout->addWidget(reset, 0, Qt::AlignLeft);
	layout->addStretch();

	connect(updates_auto_check_, &QCheckBox::toggled, this, [this](bool checked) {
		if (loading_) {
			return;
		}
		QSettings s;
		s.setValue(kUpdatesAutoCheckKey, checked);
	});
	connect(reset, &QPushButton::clicked, this, &PreferencesTab::reset_update_settings);

	return scroll;
}

void PreferencesTab::load_settings() {
	loading_ = true;

	QSettings s;
	if (theme_combo_) {
		theme_combo_->setCurrentIndex(index_for_theme(ThemeManager::load_theme()));
	}
	if (archive_open_always_ask_) {
		archive_open_always_ask_->setChecked(s.value(kArchiveOpenAlwaysAskKey, true).toBool());
	}
	if (archive_open_action_combo_) {
		set_combo_data(archive_open_action_combo_, s.value(kArchiveOpenDefaultActionKey, "open_direct").toString());
	}
	if (recent_file_limit_spin_) {
		recent_file_limit_spin_->setValue(qBound(1, s.value(kRecentFileLimitKey, kDefaultRecentFileLimit).toInt(), 50));
	}
	if (pure_pak_protector_) {
		pure_pak_protector_->setChecked(s.value(kPurePakProtectorKey, true).toBool());
	}
	if (renderer_combo_) {
		set_combo_data(renderer_combo_, preview_renderer_to_string(load_preview_renderer()));
	}
	if (preview_fov_slider_) {
		const int fov = qBound(40, s.value(kPreviewFovKey, 100).toInt(), 120);
		preview_fov_slider_->setValue(fov);
		if (preview_fov_value_label_) {
			preview_fov_value_label_->setText(QString("%1 deg").arg(fov));
		}
	}
	if (three_d_grid_combo_) {
		set_combo_data(three_d_grid_combo_, s.value(kPreviewGridKey, "floor").toString());
	}
	if (three_d_background_combo_) {
		set_combo_data(three_d_background_combo_, s.value(kPreviewBackgroundModeKey, "themed").toString());
	}
	if (three_d_wireframe_) {
		three_d_wireframe_->setChecked(s.value(kPreviewWireframeKey, false).toBool());
	}
	if (three_d_textured_) {
		three_d_textured_->setChecked(s.value(kPreviewTexturedKey, true).toBool());
	}
	if (bsp_lightmapping_) {
		bsp_lightmapping_->setChecked(s.value(kBspLightmappingKey, true).toBool());
	}
	if (model_texture_smoothing_) {
		model_texture_smoothing_->setChecked(s.value(kModelTextureSmoothingKey, false).toBool());
	}
	if (model_animation_playing_) {
		model_animation_playing_->setChecked(s.value(kModelAnimPlayingKey, true).toBool());
	}
	if (model_animation_loop_) {
		model_animation_loop_->setChecked(s.value(kModelAnimLoopKey, true).toBool());
	}
	if (model_skeleton_overlay_) {
		model_skeleton_overlay_->setChecked(s.value(kModelSkeletonKey, false).toBool());
	}
	if (model_animation_speed_combo_) {
		set_speed_combo(model_animation_speed_combo_, s.value(kModelAnimSpeedKey, 1.0).toDouble());
	}
	if (image_texture_smoothing_) {
		image_texture_smoothing_->setChecked(s.value(kImageTextureSmoothingKey, false).toBool());
	}
	if (image_background_combo_) {
		const QString mode = s.value(kImageBackgroundModeKey, s.value("preview/image/checkerboard", true).toBool() ? "checkerboard" : "solid").toString();
		set_combo_data(image_background_combo_, mode);
	}
	if (image_layout_combo_) {
		set_combo_data(image_layout_combo_, s.value(kImageLayoutKey, "fit").toString());
	}
	if (image_reveal_transparency_) {
		image_reveal_transparency_->setChecked(s.value(kImageRevealTransparentKey, false).toBool());
	}
	if (text_word_wrap_) {
		text_word_wrap_->setChecked(s.value(kTextWordWrapKey, false).toBool());
	}
	if (updates_auto_check_) {
		updates_auto_check_->setChecked(s.value(kUpdatesAutoCheckKey, true).toBool());
	}

	update_color_buttons();
	loading_ = false;
}

void PreferencesTab::apply_theme_from_combo() {
	if (!theme_combo_) {
		return;
	}
	const AppTheme theme = theme_for_index(theme_combo_->currentIndex());
	ThemeManager::save_theme(theme);
	if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance())) {
		ThemeManager::apply_theme(*app, theme);
	}
	emit theme_changed(theme);
}

void PreferencesTab::choose_image_background_color() {
	QSettings s;
	const QColor initial = setting_color(s, kImageBackgroundColorKey, palette().color(QPalette::Window));
	const QColor chosen = QColorDialog::getColor(initial, this, "Choose Image Background Color");
	if (!chosen.isValid()) {
		return;
	}
	s.setValue(kImageBackgroundColorKey, chosen);
	update_color_buttons();
	notify_preview_preferences_changed();
}

void PreferencesTab::choose_3d_background_color() {
	QSettings s;
	const QColor initial = setting_color(s, kPreviewBackgroundColorKey, palette().color(QPalette::Window));
	const QColor chosen = QColorDialog::getColor(initial, this, "Choose 3D Background Color");
	if (!chosen.isValid()) {
		return;
	}
	s.setValue(kPreviewBackgroundColorKey, chosen);
	update_color_buttons();
	notify_preview_preferences_changed();
}

void PreferencesTab::update_color_buttons() {
	QSettings s;
	style_color_button(image_background_color_button_,
	                   setting_color(s, kImageBackgroundColorKey, palette().color(QPalette::Window)));
	style_color_button(three_d_background_color_button_,
	                   setting_color(s, kPreviewBackgroundColorKey, palette().color(QPalette::Window)));
}

void PreferencesTab::reset_appearance_settings() {
	ThemeManager::save_theme(AppTheme::System);
	if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance())) {
		ThemeManager::apply_theme(*app, AppTheme::System);
	}
	load_settings();
	emit theme_changed(AppTheme::System);
}

void PreferencesTab::reset_archive_settings() {
	QSettings s;
	s.setValue(kArchiveOpenAlwaysAskKey, true);
	s.setValue(kArchiveOpenDefaultActionKey, "open_direct");
	s.remove("archive/openChooserDefaultInstallUid");
	s.setValue(kRecentFileLimitKey, kDefaultRecentFileLimit);
	s.setValue(kPurePakProtectorKey, true);
	load_settings();
	emit recent_file_limit_changed(kDefaultRecentFileLimit);
	emit pure_pak_protector_changed(true);
}

void PreferencesTab::reset_preview_settings() {
	QSettings s;
	save_preview_renderer(PreviewRenderer::Vulkan);
	s.setValue(kPreviewFovKey, 100);
	s.setValue(kPreviewGridKey, "floor");
	s.setValue(kPreviewBackgroundModeKey, "themed");
	s.remove(kPreviewBackgroundColorKey);
	s.setValue(kPreviewWireframeKey, false);
	s.setValue(kPreviewTexturedKey, true);
	s.setValue(kBspLightmappingKey, true);
	s.setValue(kModelTextureSmoothingKey, false);
	s.setValue(kModelAnimPlayingKey, true);
	s.setValue(kModelAnimLoopKey, true);
	s.setValue(kModelSkeletonKey, false);
	s.setValue(kModelAnimSpeedKey, 1.0);
	load_settings();
	emit preview_renderer_changed(PreviewRenderer::Vulkan);
	emit preview_fov_changed(100);
	emit model_texture_smoothing_changed(false);
	notify_preview_preferences_changed();
}

void PreferencesTab::reset_image_text_settings() {
	QSettings s;
	s.setValue(kImageTextureSmoothingKey, false);
	s.setValue(kImageBackgroundModeKey, "checkerboard");
	s.remove(kImageBackgroundColorKey);
	s.setValue(kImageLayoutKey, "fit");
	s.setValue(kImageRevealTransparentKey, false);
	s.setValue(kTextWordWrapKey, false);
	load_settings();
	emit image_texture_smoothing_changed(false);
	notify_preview_preferences_changed();
}

void PreferencesTab::reset_update_settings() {
	QSettings s;
	s.setValue(kUpdatesAutoCheckKey, true);
	s.remove(kUpdatesSkipVersionKey);
	load_settings();
}

void PreferencesTab::notify_preview_preferences_changed() {
	if (loading_) {
		return;
	}
	emit preview_preferences_changed();
}
