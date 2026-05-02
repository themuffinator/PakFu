#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

#include <algorithm>
#include <cmath>

namespace PakFu::Ui {

struct DesignTokens {
	QColor surface;
	QColor raised_surface;
	QColor border;
	QColor text_primary;
	QColor text_secondary;
	QColor accent;
	QColor success;
	QColor warning;
	QColor error;
	QColor selection;
	QColor selection_text;
	QColor focus;
	QColor disabled_surface;
	QColor disabled_text;
	QColor preview_scrim;
};

inline double token_luminance_channel(int channel) {
	const double value = static_cast<double>(channel) / 255.0;
	return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

inline double token_relative_luminance(const QColor& color) {
	return 0.2126 * token_luminance_channel(color.red())
		   + 0.7152 * token_luminance_channel(color.green())
		   + 0.0722 * token_luminance_channel(color.blue());
}

inline double token_contrast_ratio(const QColor& foreground, const QColor& background) {
	const double foreground_luminance = token_relative_luminance(foreground);
	const double background_luminance = token_relative_luminance(background);
	const double lighter = std::max(foreground_luminance, background_luminance);
	const double darker = std::min(foreground_luminance, background_luminance);
	return (lighter + 0.05) / (darker + 0.05);
}

inline bool token_surface_is_dark(const QColor& surface) {
	return token_relative_luminance(surface) < 0.35;
}

inline QColor token_mix(const QColor& foreground, const QColor& background, double foreground_weight) {
	const double weight = std::clamp(foreground_weight, 0.0, 1.0);
	const double inverse = 1.0 - weight;
	return QColor(
		static_cast<int>(std::round(foreground.red() * weight + background.red() * inverse)),
		static_cast<int>(std::round(foreground.green() * weight + background.green() * inverse)),
		static_cast<int>(std::round(foreground.blue() * weight + background.blue() * inverse)),
		static_cast<int>(std::round(foreground.alpha() * weight + background.alpha() * inverse)));
}

inline QColor token_with_alpha(QColor color, int alpha) {
	color.setAlpha(std::clamp(alpha, 0, 255));
	return color;
}

inline QColor token_ensure_contrast(QColor color, const QColor& surface, double minimum_ratio) {
	if (token_contrast_ratio(color, surface) >= minimum_ratio) {
		return color;
	}

	const QColor target = token_surface_is_dark(surface) ? QColor(255, 255, 255) : QColor(0, 0, 0);
	for (int step = 1; step <= 10; ++step) {
		const QColor candidate = token_mix(target, color, static_cast<double>(step) / 10.0);
		if (token_contrast_ratio(candidate, surface) >= minimum_ratio) {
			return candidate;
		}
	}

	return target;
}

inline QColor token_readable_text_on(const QColor& preferred, const QColor& surface) {
	if (token_contrast_ratio(preferred, surface) >= 4.5) {
		return preferred;
	}

	const QColor black(0, 0, 0);
	const QColor white(255, 255, 255);
	return token_contrast_ratio(black, surface) >= token_contrast_ratio(white, surface) ? black : white;
}

inline QColor token_secondary_text(const QPalette& palette, const QColor& surface, const QColor& primary) {
	const QColor disabled = palette.color(QPalette::Disabled, QPalette::WindowText);
	if (disabled.isValid() && token_contrast_ratio(disabled, surface) >= 4.5) {
		return disabled;
	}

	const QColor mixed = token_mix(primary, surface, 0.72);
	if (token_contrast_ratio(mixed, surface) >= 4.5) {
		return mixed;
	}

	return primary;
}

inline QColor token_disabled_text(const QPalette& palette, const QColor& surface, const QColor& primary) {
	const QColor disabled = palette.color(QPalette::Disabled, QPalette::WindowText);
	if (disabled.isValid() && token_contrast_ratio(disabled, surface) >= 3.0) {
		return disabled;
	}

	const QColor mixed = token_mix(primary, surface, 0.55);
	if (token_contrast_ratio(mixed, surface) >= 3.0) {
		return mixed;
	}

	return token_secondary_text(palette, surface, primary);
}

inline QColor token_status_color(const QColor& dark_color, const QColor& light_color, const QColor& surface) {
	return token_surface_is_dark(surface) ? dark_color : light_color;
}

inline DesignTokens make_design_tokens(const QPalette& palette) {
	const QColor surface = palette.color(QPalette::Window);
	const QColor raised_surface = palette.color(QPalette::Button);
	const QColor text_primary = palette.color(QPalette::WindowText);
	const QColor selection = palette.color(QPalette::Highlight);
	const bool dark_surface = token_surface_is_dark(surface);

	return DesignTokens{
		surface,
		raised_surface,
		palette.color(QPalette::Mid),
		text_primary,
		token_secondary_text(palette, surface, text_primary),
		token_ensure_contrast(
			palette.color(QPalette::Link).isValid() ? palette.color(QPalette::Link) : selection,
			surface,
			4.5),
		token_ensure_contrast(token_status_color(QColor(84, 210, 132), QColor(0, 128, 82), surface), surface, 4.5),
		token_ensure_contrast(token_status_color(QColor(245, 184, 82), QColor(150, 96, 0), surface), surface, 4.5),
		token_ensure_contrast(token_status_color(QColor(255, 112, 112), QColor(178, 40, 40), surface), surface, 4.5),
		selection,
		token_readable_text_on(palette.color(QPalette::HighlightedText), selection),
		token_ensure_contrast(selection, surface, 3.0),
		palette.color(QPalette::Disabled, QPalette::Button).isValid()
			? palette.color(QPalette::Disabled, QPalette::Button)
			: token_mix(raised_surface, surface, 0.45),
		token_disabled_text(palette, surface, text_primary),
		token_with_alpha(QColor(0, 0, 0), dark_surface ? 170 : 118),
	};
}

inline QString token_qss_color(const QColor& color) {
	if (color.alpha() < 255) {
		return QStringLiteral("rgba(%1, %2, %3, %4)")
			.arg(color.red())
			.arg(color.green())
			.arg(color.blue())
			.arg(color.alpha());
	}

	return color.name(QColor::HexRgb);
}

inline void replace_design_token(QString& style_sheet, const QString& token_name, const QColor& color) {
	style_sheet.replace(token_name, token_qss_color(color));
}

inline QString apply_design_tokens(QString style_sheet, const DesignTokens& tokens) {
	replace_design_token(style_sheet, QStringLiteral("$surface-raised"), tokens.raised_surface);
	replace_design_token(style_sheet, QStringLiteral("$surface-disabled"), tokens.disabled_surface);
	replace_design_token(style_sheet, QStringLiteral("$surface"), tokens.surface);
	replace_design_token(style_sheet, QStringLiteral("$border"), tokens.border);
	replace_design_token(style_sheet, QStringLiteral("$text-primary"), tokens.text_primary);
	replace_design_token(style_sheet, QStringLiteral("$text-secondary"), tokens.text_secondary);
	replace_design_token(style_sheet, QStringLiteral("$accent"), tokens.accent);
	replace_design_token(style_sheet, QStringLiteral("$success"), tokens.success);
	replace_design_token(style_sheet, QStringLiteral("$warning"), tokens.warning);
	replace_design_token(style_sheet, QStringLiteral("$error"), tokens.error);
	replace_design_token(style_sheet, QStringLiteral("$selection-text"), tokens.selection_text);
	replace_design_token(style_sheet, QStringLiteral("$selection"), tokens.selection);
	replace_design_token(style_sheet, QStringLiteral("$focus"), tokens.focus);
	replace_design_token(style_sheet, QStringLiteral("$disabled-text"), tokens.disabled_text);
	replace_design_token(style_sheet, QStringLiteral("$preview-scrim"), tokens.preview_scrim);
	return style_sheet;
}

}  // namespace PakFu::Ui
