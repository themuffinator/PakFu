#pragma once

#include <QString>

class QApplication;

enum class AppTheme {
  System = 0,
  Light,
  Dark,
  CreamyGoodness,
  VibeORama,
  Midnight,
  SpringTime,
  DarkMatter,
};

class ThemeManager {
public:
  static void initialize(QApplication& app);
  static AppTheme load_theme();
  static void save_theme(AppTheme theme);
  static void apply_theme(QApplication& app, AppTheme theme);
  static void apply_saved_theme(QApplication& app);

private:
  static QString theme_to_string(AppTheme theme);
  static AppTheme theme_from_string(const QString& value);
};
