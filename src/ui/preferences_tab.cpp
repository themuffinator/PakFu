#include "preferences_tab.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "ui/theme_manager.h"

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
  layout->addStretch();

  connect(theme_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
    apply_theme_from_combo();
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
