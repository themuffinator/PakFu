#include "theme_manager.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace {
constexpr char kThemeKey[] = "ui/theme";

constexpr char kThemeQss[] = R"QSS(
QMenuBar {
  background: palette(window);
}
QMenuBar::item {
  background: transparent;
  padding: 6px 10px;
  border-radius: 6px;
}
QMenuBar::item:selected {
  background: palette(light);
}

QMenu {
  background: palette(window);
  border: 1px solid palette(mid);
  padding: 6px;
}
QMenu::item {
  padding: 6px 18px;
  border-radius: 6px;
}
QMenu::item:selected {
  background: palette(highlight);
  color: palette(highlighted-text);
}

QTabWidget::pane {
  border: 1px solid palette(mid);
  top: -1px;
}
QTabBar::tab {
  background: palette(button);
  border: 1px solid palette(mid);
  border-bottom: none;
  padding: 8px 14px;
  margin-right: 2px;
  border-top-left-radius: 8px;
  border-top-right-radius: 8px;
}
QTabBar::tab:selected {
  background: palette(window);
}
QTabBar::tab:hover:!selected {
  background: palette(light);
}

QPushButton {
  padding: 7px 14px;
  border-radius: 8px;
  border: 1px solid palette(mid);
  background: palette(button);
}
QPushButton:hover {
  background: palette(light);
}
QPushButton:pressed {
  background: palette(midlight);
}
QPushButton:disabled {
  background: palette(window);
  color: palette(mid);
}

QToolButton {
  border: none;
  padding: 2px;
}
QToolButton:hover {
  background: rgba(127, 127, 127, 40);
  border-radius: 6px;
}

QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
  padding: 6px 10px;
  border-radius: 8px;
  border: 1px solid palette(mid);
  background: palette(base);
}
QComboBox::drop-down {
  border: none;
  width: 26px;
}
QComboBox QAbstractItemView {
  background: palette(window);
  border: 1px solid palette(mid);
  selection-background-color: palette(highlight);
  selection-color: palette(highlighted-text);
}

QTreeView, QTreeWidget, QListView, QTableView {
  border: 1px solid palette(mid);
  background: palette(base);
  alternate-background-color: palette(alternate-base);
  selection-background-color: palette(highlight);
  selection-color: palette(highlighted-text);
}
QHeaderView::section {
  background: palette(button);
  border: 1px solid palette(mid);
  padding: 6px 10px;
}

QScrollBar:vertical {
  background: transparent;
  width: 12px;
  margin: 0px;
}
QScrollBar::handle:vertical {
  background: rgba(127, 127, 127, 80);
  border-radius: 6px;
  min-height: 20px;
}
QScrollBar::handle:vertical:hover {
  background: rgba(127, 127, 127, 120);
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
  height: 0px;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
  background: transparent;
}
)QSS";

struct ThemeBaseline {
  bool initialized = false;
  QString style_name;
  QPalette palette;
};

ThemeBaseline& baseline() {
  static ThemeBaseline b;
  return b;
}

QPalette make_light_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(248, 248, 248));
  p.setColor(QPalette::WindowText, QColor(20, 20, 20));
  p.setColor(QPalette::Base, QColor(255, 255, 255));
  p.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
  p.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
  p.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
  p.setColor(QPalette::Text, QColor(20, 20, 20));
  p.setColor(QPalette::Button, QColor(245, 245, 245));
  p.setColor(QPalette::ButtonText, QColor(20, 20, 20));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(0, 102, 204));
  p.setColor(QPalette::Highlight, QColor(0, 120, 215));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(255, 255, 255));
  p.setColor(QPalette::Midlight, QColor(235, 235, 235));
  p.setColor(QPalette::Mid, QColor(210, 210, 210));
  p.setColor(QPalette::Dark, QColor(170, 170, 170));
  p.setColor(QPalette::Shadow, QColor(120, 120, 120));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 140, 140));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(140, 140, 140));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 140, 140));
  return p;
}

QPalette make_dark_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(24, 24, 26));
  p.setColor(QPalette::WindowText, QColor(235, 235, 235));
  p.setColor(QPalette::Base, QColor(16, 16, 18));
  p.setColor(QPalette::AlternateBase, QColor(28, 28, 30));
  p.setColor(QPalette::ToolTipBase, QColor(235, 235, 235));
  p.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
  p.setColor(QPalette::Text, QColor(235, 235, 235));
  p.setColor(QPalette::Button, QColor(32, 32, 34));
  p.setColor(QPalette::ButtonText, QColor(235, 235, 235));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(77, 163, 255));
  p.setColor(QPalette::Highlight, QColor(71, 132, 255));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(48, 48, 52));
  p.setColor(QPalette::Midlight, QColor(40, 40, 44));
  p.setColor(QPalette::Mid, QColor(54, 54, 58));
  p.setColor(QPalette::Dark, QColor(20, 20, 22));
  p.setColor(QPalette::Shadow, QColor(0, 0, 0));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
  return p;
}

QPalette make_creamy_palette() {
  QPalette p;
  // Slightly darker warm neutrals so the theme doesn't feel washed out.
  p.setColor(QPalette::Window, QColor(240, 230, 214));
  p.setColor(QPalette::WindowText, QColor(45, 32, 22));
  p.setColor(QPalette::Base, QColor(250, 242, 232));
  p.setColor(QPalette::AlternateBase, QColor(244, 232, 214));
  p.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
  p.setColor(QPalette::ToolTipText, QColor(30, 22, 16));
  p.setColor(QPalette::Text, QColor(45, 32, 22));
  p.setColor(QPalette::Button, QColor(236, 215, 189));
  p.setColor(QPalette::ButtonText, QColor(45, 32, 22));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(25, 120, 130));
  // Darker selection for menu items and lists.
  p.setColor(QPalette::Highlight, QColor(186, 108, 54));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(246, 236, 220));
  p.setColor(QPalette::Midlight, QColor(232, 210, 182));
  p.setColor(QPalette::Mid, QColor(206, 180, 148));
  p.setColor(QPalette::Dark, QColor(168, 142, 118));
  p.setColor(QPalette::Shadow, QColor(120, 100, 80));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 120, 105));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(140, 120, 105));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 120, 105));
  return p;
}

QPalette make_vibe_palette() {
  QPalette p;
  // Dark, colorful, and a bit chaotic - but still readable.
  p.setColor(QPalette::Window, QColor(20, 20, 28));
  p.setColor(QPalette::WindowText, QColor(238, 238, 245));
  p.setColor(QPalette::Base, QColor(14, 14, 20));
  p.setColor(QPalette::AlternateBase, QColor(26, 26, 38));
  p.setColor(QPalette::ToolTipBase, QColor(245, 245, 255));
  p.setColor(QPalette::ToolTipText, QColor(20, 20, 30));
  p.setColor(QPalette::Text, QColor(238, 238, 245));
  p.setColor(QPalette::Button, QColor(32, 32, 46));
  p.setColor(QPalette::ButtonText, QColor(238, 238, 245));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(0, 190, 220));
  p.setColor(QPalette::Highlight, QColor(173, 94, 255));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(44, 44, 62));
  p.setColor(QPalette::Midlight, QColor(38, 38, 54));
  p.setColor(QPalette::Mid, QColor(58, 58, 78));
  p.setColor(QPalette::Dark, QColor(16, 16, 22));
  p.setColor(QPalette::Shadow, QColor(0, 0, 0));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 130));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 130));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 130));
  return p;
}

QPalette make_midnight_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(14, 18, 26));
  p.setColor(QPalette::WindowText, QColor(236, 238, 242));
  p.setColor(QPalette::Base, QColor(10, 12, 18));
  p.setColor(QPalette::AlternateBase, QColor(18, 22, 32));
  p.setColor(QPalette::ToolTipBase, QColor(245, 245, 255));
  p.setColor(QPalette::ToolTipText, QColor(20, 20, 30));
  p.setColor(QPalette::Text, QColor(236, 238, 242));
  p.setColor(QPalette::Button, QColor(20, 24, 36));
  p.setColor(QPalette::ButtonText, QColor(236, 238, 242));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(94, 180, 255));
  p.setColor(QPalette::Highlight, QColor(48, 128, 200));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(34, 40, 56));
  p.setColor(QPalette::Midlight, QColor(28, 34, 48));
  p.setColor(QPalette::Mid, QColor(46, 54, 74));
  p.setColor(QPalette::Dark, QColor(8, 10, 14));
  p.setColor(QPalette::Shadow, QColor(0, 0, 0));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 130));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 130));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 130));
  return p;
}

QPalette make_spring_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(242, 250, 244));
  p.setColor(QPalette::WindowText, QColor(22, 30, 22));
  p.setColor(QPalette::Base, QColor(255, 255, 255));
  p.setColor(QPalette::AlternateBase, QColor(234, 246, 238));
  p.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
  p.setColor(QPalette::ToolTipText, QColor(22, 30, 22));
  p.setColor(QPalette::Text, QColor(22, 30, 22));
  p.setColor(QPalette::Button, QColor(232, 244, 236));
  p.setColor(QPalette::ButtonText, QColor(22, 30, 22));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(0, 126, 116));
  p.setColor(QPalette::Highlight, QColor(70, 170, 120));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(255, 255, 255));
  p.setColor(QPalette::Midlight, QColor(220, 238, 226));
  p.setColor(QPalette::Mid, QColor(194, 220, 202));
  p.setColor(QPalette::Dark, QColor(150, 178, 160));
  p.setColor(QPalette::Shadow, QColor(120, 140, 128));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 140, 132));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(130, 140, 132));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 140, 132));
  return p;
}

QPalette make_darkmatter_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(10, 8, 16));
  p.setColor(QPalette::WindowText, QColor(238, 236, 248));
  p.setColor(QPalette::Base, QColor(6, 6, 10));
  p.setColor(QPalette::AlternateBase, QColor(18, 12, 28));
  p.setColor(QPalette::ToolTipBase, QColor(245, 245, 255));
  p.setColor(QPalette::ToolTipText, QColor(20, 20, 30));
  p.setColor(QPalette::Text, QColor(238, 236, 248));
  p.setColor(QPalette::Button, QColor(20, 12, 34));
  p.setColor(QPalette::ButtonText, QColor(238, 236, 248));
  p.setColor(QPalette::BrightText, Qt::red);
  p.setColor(QPalette::Link, QColor(194, 120, 255));
  p.setColor(QPalette::Highlight, QColor(140, 60, 210));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::Light, QColor(38, 24, 66));
  p.setColor(QPalette::Midlight, QColor(30, 18, 54));
  p.setColor(QPalette::Mid, QColor(54, 34, 90));
  p.setColor(QPalette::Dark, QColor(4, 4, 8));
  p.setColor(QPalette::Shadow, QColor(0, 0, 0));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 110, 140));
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 110, 140));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 110, 140));
  return p;
}

QStyle* create_style(const QString& name) {
  if (name.isEmpty()) {
    return nullptr;
  }
  QStyle* style = QStyleFactory::create(name);
  return style;
}

QString extra_qss_for_theme(AppTheme theme) {
  switch (theme) {
    case AppTheme::CreamyGoodness:
      return QString::fromUtf8(R"QSS(
QMainWindow {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
    stop:0 #f9eddc,
    stop:1 #e9d1b3);
}
QMenu::item:selected {
  background: #ba6c36;
}
)QSS");
    case AppTheme::VibeORama:
      return QString::fromUtf8(R"QSS(
QMainWindow {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
    stop:0 #12142a,
    stop:0.55 #1a102a,
    stop:1 #0f2a22);
}
QTabBar::tab:selected {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
    stop:0 rgba(173, 94, 255, 96),
    stop:0.5 rgba(0, 190, 220, 80),
    stop:1 rgba(255, 148, 0, 64));
}
)QSS");
    case AppTheme::Midnight:
      return QString::fromUtf8(R"QSS(
QMainWindow {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
    stop:0 #0b1020,
    stop:1 #111a2b);
}
)QSS");
    case AppTheme::SpringTime:
      return QString::fromUtf8(R"QSS(
QMainWindow {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
    stop:0 #f4fff7,
    stop:0.55 #f7fbff,
    stop:1 #fff7fb);
}
)QSS");
    case AppTheme::DarkMatter:
      return QString::fromUtf8(R"QSS(
QMainWindow {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
    stop:0 #07050f,
    stop:0.6 #120a22,
    stop:1 #05040b);
}
QMenu::item:selected {
  background: #8c3ad6;
}
)QSS");
    default:
      return QString();
  }
}
}  // namespace

void ThemeManager::initialize(QApplication& app) {
  ThemeBaseline& b = baseline();
  if (b.initialized) {
    return;
  }
  b.initialized = true;
  b.style_name = app.style() ? app.style()->objectName() : QString();
  b.palette = app.palette();
}

AppTheme ThemeManager::load_theme() {
  QSettings settings;
  return theme_from_string(settings.value(kThemeKey, "system").toString());
}

void ThemeManager::save_theme(AppTheme theme) {
  QSettings settings;
  settings.setValue(kThemeKey, theme_to_string(theme));
}

void ThemeManager::apply_theme(QApplication& app, AppTheme theme) {
  initialize(app);

  ThemeBaseline& b = baseline();
  if (theme == AppTheme::System) {
    app.setStyleSheet(QString());
    // Restore whatever the platform style/palette was at startup.
    if (!b.style_name.isEmpty()) {
      if (QStyle* style = create_style(b.style_name)) {
        app.setStyle(style);
      }
    }
    app.setPalette(b.palette);
    return;
  }

  // Force a consistent base style for custom theming.
  if (QStyle* fusion = create_style("Fusion")) {
    app.setStyle(fusion);
  }

  if (theme == AppTheme::Dark) {
    app.setPalette(make_dark_palette());
  } else if (theme == AppTheme::Light) {
    app.setPalette(make_light_palette());
  } else if (theme == AppTheme::CreamyGoodness) {
    app.setPalette(make_creamy_palette());
  } else if (theme == AppTheme::VibeORama) {
    app.setPalette(make_vibe_palette());
  } else if (theme == AppTheme::Midnight) {
    app.setPalette(make_midnight_palette());
  } else if (theme == AppTheme::SpringTime) {
    app.setPalette(make_spring_palette());
  } else if (theme == AppTheme::DarkMatter) {
    app.setPalette(make_darkmatter_palette());
  }

  app.setStyleSheet(QString::fromUtf8(kThemeQss) + extra_qss_for_theme(theme));
}

void ThemeManager::apply_saved_theme(QApplication& app) {
  apply_theme(app, load_theme());
}

QString ThemeManager::theme_to_string(AppTheme theme) {
  switch (theme) {
    case AppTheme::System:
      return "system";
    case AppTheme::Light:
      return "light";
    case AppTheme::Dark:
      return "dark";
    case AppTheme::CreamyGoodness:
      return "creamy";
    case AppTheme::VibeORama:
      return "vibe";
    case AppTheme::Midnight:
      return "midnight";
    case AppTheme::SpringTime:
      return "spring";
    case AppTheme::DarkMatter:
      return "darkmatter";
  }
  return "system";
}

AppTheme ThemeManager::theme_from_string(const QString& value) {
  const QString v = value.trimmed().toLower();
  if (v == "light") {
    return AppTheme::Light;
  }
  if (v == "dark") {
    return AppTheme::Dark;
  }
  if (v == "creamy" || v == "creamy-goodness" || v == "creamygoodness") {
    return AppTheme::CreamyGoodness;
  }
  if (v == "vibe" || v == "vibe-o-rama" || v == "vibeorama") {
    return AppTheme::VibeORama;
  }
  if (v == "midnight") {
    return AppTheme::Midnight;
  }
  if (v == "spring" || v == "spring-time" || v == "springtime") {
    return AppTheme::SpringTime;
  }
  if (v == "darkmatter" || v == "dark-matter") {
    return AppTheme::DarkMatter;
  }
  return AppTheme::System;
}
