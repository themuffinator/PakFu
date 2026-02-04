#include "ui/preview_renderer.h"

#include <QSettings>

#if defined(QT_FEATURE_vulkan) && QT_FEATURE_vulkan
#include <QtGui/QVulkanInstance>
#define PAKFU_HAS_QT_VULKAN 1
#else
#define PAKFU_HAS_QT_VULKAN 0
#endif

namespace {
constexpr char kPreviewRendererKey[] = "preview/renderer";
}

QString preview_renderer_to_string(PreviewRenderer renderer) {
	switch (renderer) {
		case PreviewRenderer::Vulkan:
			return "vk";
		case PreviewRenderer::OpenGL:
			return "gl";
	}
	return "vk";
}

QString preview_renderer_display_name(PreviewRenderer renderer) {
	switch (renderer) {
		case PreviewRenderer::Vulkan:
			return "Vulkan";
		case PreviewRenderer::OpenGL:
			return "OpenGL";
	}
	return "Vulkan";
}

PreviewRenderer preview_renderer_from_string(const QString& value) {
	const QString v = value.trimmed().toLower();
	if (v == "gl" || v == "opengl" || v == "open_gl" || v == "open-gl") {
		return PreviewRenderer::OpenGL;
	}
	if (v == "vk" || v == "vulkan") {
		return PreviewRenderer::Vulkan;
	}
	return PreviewRenderer::Vulkan;
}

PreviewRenderer load_preview_renderer() {
	QSettings settings;
	const QString raw = settings.value(kPreviewRendererKey, "vk").toString();
	return preview_renderer_from_string(raw);
}

void save_preview_renderer(PreviewRenderer renderer) {
	QSettings settings;
	settings.setValue(kPreviewRendererKey, preview_renderer_to_string(renderer));
}

bool is_vulkan_renderer_available() {
#if PAKFU_HAS_QT_VULKAN
	QVulkanInstance instance;
	return instance.create();
#else
	return false;
#endif
}

PreviewRenderer resolve_preview_renderer(PreviewRenderer requested) {
	if (requested == PreviewRenderer::Vulkan && !is_vulkan_renderer_available()) {
		return PreviewRenderer::OpenGL;
	}
	return requested;
}
