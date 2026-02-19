#include "preferences_tab.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "platform/file_associations.h"
#include "ui/theme_manager.h"
#include "ui/ui_icons.h"

namespace {
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
}  // namespace

PreferencesTab::PreferencesTab(QWidget* parent) : QWidget(parent) {
  build_ui();
  load_settings();
}

void PreferencesTab::build_ui() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(28, 22, 28, 22);
  layout->setSpacing(16);

  auto* title = new QLabel("Preferences", this);
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 6);
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* card = new QFrame(this);
  card->setFrameShape(QFrame::StyledPanel);
  card->setFrameShadow(QFrame::Plain);

  auto* card_layout = new QVBoxLayout(card);
  card_layout->setContentsMargins(18, 18, 18, 18);
  card_layout->setSpacing(10);

  auto* theme_label = new QLabel("Theme", card);
  QFont label_font = theme_label->font();
  label_font.setWeight(QFont::DemiBold);
  theme_label->setFont(label_font);
  card_layout->addWidget(theme_label);

  auto* help = new QLabel("Choose how PakFu should look.", card);
  help->setWordWrap(true);
  card_layout->addWidget(help);

  theme_combo_ = new QComboBox(card);
  theme_combo_->addItem("System (default)");
  theme_combo_->addItem("Light");
  theme_combo_->addItem("Dark");
  theme_combo_->addItem("Midnight");
  theme_combo_->addItem("Spring Time");
  theme_combo_->addItem("Creamy Goodness");
  theme_combo_->addItem("Vibe-o-Rama");
  theme_combo_->addItem("DarkMatter");
  theme_combo_->setMinimumWidth(220);
  card_layout->addWidget(theme_combo_);

  card_layout->addStretch();
  layout->addWidget(card);

  auto* model_card = new QFrame(this);
  model_card->setFrameShape(QFrame::StyledPanel);
  model_card->setFrameShadow(QFrame::Plain);
  auto* model_layout = new QVBoxLayout(model_card);
  model_layout->setContentsMargins(18, 18, 18, 18);
  model_layout->setSpacing(10);

  auto* model_label = new QLabel("Model Viewer", model_card);
  model_label->setFont(label_font);
  model_layout->addWidget(model_label);

  auto* model_help = new QLabel(
    "Configure how 3D model and BSP previews are rendered.",
    model_card);
  model_help->setWordWrap(true);
  model_layout->addWidget(model_help);

  auto* renderer_row = new QWidget(model_card);
  auto* renderer_layout = new QHBoxLayout(renderer_row);
  renderer_layout->setContentsMargins(0, 0, 0, 0);
  renderer_layout->setSpacing(8);

  auto* renderer_label = new QLabel("Renderer", renderer_row);
  renderer_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
  renderer_layout->addWidget(renderer_label);

  renderer_combo_ = new QComboBox(renderer_row);
  renderer_combo_->addItem("Vulkan (default)", preview_renderer_to_string(PreviewRenderer::Vulkan));
  renderer_combo_->addItem("OpenGL", preview_renderer_to_string(PreviewRenderer::OpenGL));
  renderer_combo_->setMinimumWidth(220);
  renderer_layout->addWidget(renderer_combo_);
  renderer_layout->addStretch();
  model_layout->addWidget(renderer_row);

  renderer_status_ = new QLabel(model_card);
  renderer_status_->setWordWrap(true);
  renderer_status_->setStyleSheet("color: rgba(180, 180, 180, 200);");
  renderer_status_->setText(is_vulkan_renderer_available()
                              ? "Vulkan renderer available. OpenGL remains available as a fallback."
                              : "Vulkan renderer is not available in this build. OpenGL will be used.");
  model_layout->addWidget(renderer_status_);

  model_texture_smoothing_ = new QCheckBox("Texture smoothing (bilinear filtering)", model_card);
  model_layout->addWidget(model_texture_smoothing_);

  auto* fov_row = new QWidget(model_card);
  auto* fov_layout = new QHBoxLayout(fov_row);
  fov_layout->setContentsMargins(0, 0, 0, 0);
  fov_layout->setSpacing(8);

  auto* fov_label = new QLabel("3D FOV", fov_row);
  fov_label->setStyleSheet("color: rgba(190, 190, 190, 220);");
  fov_layout->addWidget(fov_label);

  preview_fov_slider_ = new QSlider(Qt::Horizontal, fov_row);
  preview_fov_slider_->setRange(40, 120);
  preview_fov_slider_->setSingleStep(1);
  preview_fov_slider_->setPageStep(5);
  preview_fov_slider_->setTickInterval(10);
  preview_fov_slider_->setTickPosition(QSlider::TicksBelow);
  preview_fov_slider_->setValue(100);
  preview_fov_slider_->setToolTip("Field of view for 3D BSP/model viewers.");
  fov_layout->addWidget(preview_fov_slider_, 1);

  preview_fov_value_label_ = new QLabel("100°", fov_row);
  preview_fov_value_label_->setMinimumWidth(52);
  preview_fov_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  fov_layout->addWidget(preview_fov_value_label_);

  model_layout->addWidget(fov_row);

  model_layout->addStretch();
  layout->addWidget(model_card);

  auto* image_card = new QFrame(this);
  image_card->setFrameShape(QFrame::StyledPanel);
  image_card->setFrameShadow(QFrame::Plain);
  auto* image_layout = new QVBoxLayout(image_card);
  image_layout->setContentsMargins(18, 18, 18, 18);
  image_layout->setSpacing(10);

  auto* image_label = new QLabel("Image Preview", image_card);
  image_label->setFont(label_font);
  image_layout->addWidget(image_label);

  auto* image_help = new QLabel(
    "Configure how 2D image and video previews are rendered.",
    image_card);
  image_help->setWordWrap(true);
  image_layout->addWidget(image_help);

  image_texture_smoothing_ = new QCheckBox("Texture smoothing (bilinear filtering)", image_card);
  image_layout->addWidget(image_texture_smoothing_);

  image_layout->addStretch();
  layout->addWidget(image_card);

  auto* archive_card = new QFrame(this);
  archive_card->setFrameShape(QFrame::StyledPanel);
  archive_card->setFrameShadow(QFrame::Plain);
  auto* archive_layout = new QVBoxLayout(archive_card);
  archive_layout->setContentsMargins(18, 18, 18, 18);
  archive_layout->setSpacing(10);

  auto* archive_label = new QLabel("Archive Protection", archive_card);
  archive_label->setFont(label_font);
  archive_layout->addWidget(archive_label);

  auto* archive_help = new QLabel(
    "Lock official game archives to prevent accidental edits. Disable this if you intentionally want to modify stock game data.",
    archive_card);
  archive_help->setWordWrap(true);
  archive_layout->addWidget(archive_help);

  pure_pak_protector_ = new QCheckBox("Pure PAK protector (read-only official archives)", archive_card);
  archive_layout->addWidget(pure_pak_protector_);

  archive_layout->addStretch();
  layout->addWidget(archive_card);

  auto* assoc_card = new QFrame(this);
  assoc_card->setFrameShape(QFrame::StyledPanel);
  assoc_card->setFrameShadow(QFrame::Plain);
  auto* assoc_layout = new QVBoxLayout(assoc_card);
  assoc_layout->setContentsMargins(18, 18, 18, 18);
  assoc_layout->setSpacing(10);

  auto* assoc_label = new QLabel("File Associations", assoc_card);
  assoc_label->setFont(label_font);
  assoc_layout->addWidget(assoc_label);

  auto* assoc_help = new QLabel(
    "Associate .pak files with PakFu so double-clicking a PAK opens it here.",
    assoc_card);
  assoc_help->setWordWrap(true);
  assoc_layout->addWidget(assoc_help);

  assoc_status_ = new QLabel(assoc_card);
  assoc_status_->setWordWrap(true);
  assoc_status_->setStyleSheet("color: rgba(200, 200, 200, 210);");
  assoc_layout->addWidget(assoc_status_);

  auto* btn_row = new QHBoxLayout();
  assoc_apply_ = new QPushButton("Associate .pak with PakFu", assoc_card);
  assoc_details_ = new QPushButton("Details...", assoc_card);
  assoc_apply_->setIcon(UiIcons::icon(UiIcons::Id::Associate, assoc_apply_->style()));
  assoc_details_->setIcon(UiIcons::icon(UiIcons::Id::Details, assoc_details_->style()));
  btn_row->addWidget(assoc_apply_);
  btn_row->addSpacing(10);
  btn_row->addWidget(assoc_details_);
  btn_row->addStretch();
  assoc_layout->addLayout(btn_row);

  layout->addWidget(assoc_card);
  layout->addStretch();

  connect(theme_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
    apply_theme_from_combo();
  });
  if (model_texture_smoothing_) {
    connect(model_texture_smoothing_, &QCheckBox::toggled, this, [this](bool checked) {
      QSettings s;
      s.setValue("preview/model/textureSmoothing", checked);
      emit model_texture_smoothing_changed(checked);
    });
  }
  if (preview_fov_slider_) {
    connect(preview_fov_slider_, &QSlider::valueChanged, this, [this](int value) {
      const int clamped = qBound(40, value, 120);
      if (preview_fov_value_label_) {
        preview_fov_value_label_->setText(QString("%1°").arg(clamped));
      }
      QSettings s;
      s.setValue("preview/3d/fov", clamped);
      emit preview_fov_changed(clamped);
    });
  }
  if (image_texture_smoothing_) {
    connect(image_texture_smoothing_, &QCheckBox::toggled, this, [this](bool checked) {
      QSettings s;
      s.setValue("preview/image/textureSmoothing", checked);
      emit image_texture_smoothing_changed(checked);
    });
  }
  if (pure_pak_protector_) {
    connect(pure_pak_protector_, &QCheckBox::toggled, this, [this](bool checked) {
      QSettings s;
      s.setValue("archive/purePakProtector", checked);
      emit pure_pak_protector_changed(checked);
    });
  }
  if (renderer_combo_) {
    connect(renderer_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
      if (!renderer_combo_) {
        return;
      }
      const PreviewRenderer renderer = preview_renderer_from_string(renderer_combo_->currentData().toString());
      save_preview_renderer(renderer);
      emit preview_renderer_changed(renderer);
    });
  }
  connect(assoc_apply_, &QPushButton::clicked, this, &PreferencesTab::apply_association);
  connect(assoc_details_, &QPushButton::clicked, this, [this]() {
    QString details;
    FileAssociations::is_pak_registered(&details);
    QMessageBox::information(this, "PakFu File Associations", details);
  });
}

void PreferencesTab::load_settings() {
  if (!theme_combo_) {
    return;
  }
  const AppTheme theme = ThemeManager::load_theme();
  theme_combo_->blockSignals(true);
  theme_combo_->setCurrentIndex(index_for_theme(theme));
  theme_combo_->blockSignals(false);

  if (model_texture_smoothing_) {
    QSettings s;
    const bool smooth = s.value("preview/model/textureSmoothing", false).toBool();
    model_texture_smoothing_->blockSignals(true);
    model_texture_smoothing_->setChecked(smooth);
    model_texture_smoothing_->blockSignals(false);
  }
  if (preview_fov_slider_) {
    QSettings s;
    const int fov = qBound(40, s.value("preview/3d/fov", 100).toInt(), 120);
    preview_fov_slider_->blockSignals(true);
    preview_fov_slider_->setValue(fov);
    preview_fov_slider_->blockSignals(false);
    if (preview_fov_value_label_) {
      preview_fov_value_label_->setText(QString("%1°").arg(fov));
    }
  }
  if (renderer_combo_) {
    const QString renderer_key = preview_renderer_to_string(load_preview_renderer());
    const int idx = renderer_combo_->findData(renderer_key);
    renderer_combo_->blockSignals(true);
    renderer_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    renderer_combo_->blockSignals(false);
  }
  if (image_texture_smoothing_) {
    QSettings s;
    const bool smooth = s.value("preview/image/textureSmoothing", false).toBool();
    image_texture_smoothing_->blockSignals(true);
    image_texture_smoothing_->setChecked(smooth);
    image_texture_smoothing_->blockSignals(false);
  }
  if (pure_pak_protector_) {
    QSettings s;
    const bool enabled = s.value("archive/purePakProtector", true).toBool();
    pure_pak_protector_->blockSignals(true);
    pure_pak_protector_->setChecked(enabled);
    pure_pak_protector_->blockSignals(false);
  }

  refresh_association_status();
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

void PreferencesTab::refresh_association_status() {
  if (!assoc_status_) {
    return;
  }
  QString details;
  const bool ok = FileAssociations::is_pak_registered(&details);
  assoc_status_->setText(ok ? "Status: PakFu is registered for .pak files."
                            : "Status: PakFu is not registered for .pak files.");
}

void PreferencesTab::apply_association() {
  QString err;
  if (!FileAssociations::apply_pak_registration(&err)) {
    QMessageBox::warning(this, "PakFu File Associations", err.isEmpty() ? "Unable to apply file association." : err);
    refresh_association_status();
    return;
  }

  QMessageBox::information(
    this,
    "PakFu File Associations",
    "PakFu has been registered as a handler for .pak files.\n\n"
    "On modern Windows, you may still need to choose PakFu in Settings -> Default apps.");
  refresh_association_status();
}
