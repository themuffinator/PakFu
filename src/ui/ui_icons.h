#pragma once

#include <QIcon>

class QStyle;

namespace UiIcons {
enum class Id {
	TabClose,
	NewPak,
	OpenArchive,
	OpenFolder,
	Save,
	SaveAs,
	ExitApp,
	Undo,
	Redo,
	Cut,
	Copy,
	Paste,
	Rename,
	Preferences,
	CheckUpdates,
	About,
	AddFiles,
	AddFolder,
	NewFolder,
	DeleteItem,
	ViewAuto,
	ViewDetails,
	ViewList,
	ViewSmallIcons,
	ViewLargeIcons,
	ViewGallery,
	MediaPrevious,
	MediaPlay,
	MediaPause,
	MediaStop,
	MediaNext,
	Info,
	RevealTransparency,
	WordWrap,
	Lightmaps,
	Textured,
	Wireframe,
	FullscreenEnter,
	FullscreenExit,
	Configure,
	AutoDetect,
	Browse,
	Associate,
	Details,
};

QIcon icon(Id id);
QIcon icon(Id id, const QStyle* style);
}  // namespace UiIcons
