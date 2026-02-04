#pragma once

#include <QMetaType>
#include <QString>

enum class PreviewRenderer {
	Vulkan,
	OpenGL,
};

Q_DECLARE_METATYPE(PreviewRenderer);

QString preview_renderer_to_string(PreviewRenderer renderer);
QString preview_renderer_display_name(PreviewRenderer renderer);
PreviewRenderer preview_renderer_from_string(const QString& value);

PreviewRenderer load_preview_renderer();
void save_preview_renderer(PreviewRenderer renderer);

bool is_vulkan_renderer_available();
PreviewRenderer resolve_preview_renderer(PreviewRenderer requested);
