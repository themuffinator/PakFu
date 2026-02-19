#include "ui/ui_icons.h"

#include <QString>
#include <QStyle>

namespace {
const char* resource_path(UiIcons::Id id) {
	switch (id) {
		case UiIcons::Id::TabClose:
			return ":/assets/icons/ui/tab-close.svg";
		case UiIcons::Id::NewPak:
			return ":/assets/icons/ui/new-pak.svg";
		case UiIcons::Id::OpenArchive:
			return ":/assets/icons/ui/open-archive.svg";
		case UiIcons::Id::OpenFolder:
			return ":/assets/icons/ui/open-folder.svg";
		case UiIcons::Id::Save:
			return ":/assets/icons/ui/save.svg";
		case UiIcons::Id::SaveAs:
			return ":/assets/icons/ui/save-as.svg";
		case UiIcons::Id::ExitApp:
			return ":/assets/icons/ui/exit.svg";
		case UiIcons::Id::Undo:
			return ":/assets/icons/ui/undo.svg";
		case UiIcons::Id::Redo:
			return ":/assets/icons/ui/redo.svg";
		case UiIcons::Id::Cut:
			return ":/assets/icons/ui/cut.svg";
		case UiIcons::Id::Copy:
			return ":/assets/icons/ui/copy.svg";
		case UiIcons::Id::Paste:
			return ":/assets/icons/ui/paste.svg";
		case UiIcons::Id::Rename:
			return ":/assets/icons/ui/rename.svg";
		case UiIcons::Id::Preferences:
			return ":/assets/icons/ui/preferences.svg";
		case UiIcons::Id::CheckUpdates:
			return ":/assets/icons/ui/check-updates.svg";
		case UiIcons::Id::About:
			return ":/assets/icons/ui/about.svg";
		case UiIcons::Id::AddFiles:
			return ":/assets/icons/ui/add-files.svg";
		case UiIcons::Id::AddFolder:
			return ":/assets/icons/ui/add-folder.svg";
		case UiIcons::Id::NewFolder:
			return ":/assets/icons/ui/new-folder.svg";
		case UiIcons::Id::DeleteItem:
			return ":/assets/icons/ui/delete.svg";
		case UiIcons::Id::ViewAuto:
			return ":/assets/icons/ui/view-auto.svg";
		case UiIcons::Id::ViewDetails:
			return ":/assets/icons/ui/view-details.svg";
		case UiIcons::Id::ViewList:
			return ":/assets/icons/ui/view-list.svg";
		case UiIcons::Id::ViewSmallIcons:
			return ":/assets/icons/ui/view-small-icons.svg";
		case UiIcons::Id::ViewLargeIcons:
			return ":/assets/icons/ui/view-large-icons.svg";
		case UiIcons::Id::ViewGallery:
			return ":/assets/icons/ui/view-gallery.svg";
		case UiIcons::Id::MediaPrevious:
			return ":/assets/icons/ui/media-previous.svg";
		case UiIcons::Id::MediaPlay:
			return ":/assets/icons/ui/media-play.svg";
		case UiIcons::Id::MediaPause:
			return ":/assets/icons/ui/media-pause.svg";
		case UiIcons::Id::MediaStop:
			return ":/assets/icons/ui/media-stop.svg";
		case UiIcons::Id::MediaNext:
			return ":/assets/icons/ui/media-next.svg";
		case UiIcons::Id::Info:
			return ":/assets/icons/ui/info.svg";
		case UiIcons::Id::RevealTransparency:
			return ":/assets/icons/ui/reveal-transparency.svg";
		case UiIcons::Id::WordWrap:
			return ":/assets/icons/ui/word-wrap.svg";
		case UiIcons::Id::Lightmaps:
			return ":/assets/icons/ui/lightmaps.svg";
		case UiIcons::Id::Textured:
			return ":/assets/icons/ui/textured.svg";
		case UiIcons::Id::Wireframe:
			return ":/assets/icons/ui/wireframe.svg";
		case UiIcons::Id::FullscreenEnter:
			return ":/assets/icons/ui/fullscreen-enter.svg";
		case UiIcons::Id::FullscreenExit:
			return ":/assets/icons/ui/fullscreen-exit.svg";
		case UiIcons::Id::Configure:
			return ":/assets/icons/ui/configure.svg";
		case UiIcons::Id::AutoDetect:
			return ":/assets/icons/ui/auto-detect.svg";
		case UiIcons::Id::Browse:
			return ":/assets/icons/ui/browse.svg";
		case UiIcons::Id::Associate:
			return ":/assets/icons/ui/associate.svg";
		case UiIcons::Id::Details:
			return ":/assets/icons/ui/details.svg";
	}
	return ":/assets/icons/ui/about.svg";
}

QIcon fallback_icon(UiIcons::Id id, const QStyle* style) {
	if (!style) {
		return {};
	}

	switch (id) {
		case UiIcons::Id::TabClose:
			return style->standardIcon(QStyle::SP_TitleBarCloseButton);
		case UiIcons::Id::OpenArchive:
		case UiIcons::Id::AddFiles:
		case UiIcons::Id::Browse:
			return style->standardIcon(QStyle::SP_DialogOpenButton);
		case UiIcons::Id::OpenFolder:
			return style->standardIcon(QStyle::SP_DirOpenIcon);
		case UiIcons::Id::AddFolder:
			return style->standardIcon(QStyle::SP_DirIcon);
		case UiIcons::Id::NewFolder:
			return style->standardIcon(QStyle::SP_FileDialogNewFolder);
		case UiIcons::Id::Save:
		case UiIcons::Id::SaveAs:
			return style->standardIcon(QStyle::SP_DialogSaveButton);
		case UiIcons::Id::ExitApp:
			return style->standardIcon(QStyle::SP_DialogCloseButton);
		case UiIcons::Id::Undo:
			return style->standardIcon(QStyle::SP_ArrowBack);
		case UiIcons::Id::Redo:
			return style->standardIcon(QStyle::SP_ArrowForward);
		case UiIcons::Id::DeleteItem:
			return style->standardIcon(QStyle::SP_TrashIcon);
		case UiIcons::Id::ViewAuto:
		case UiIcons::Id::ViewSmallIcons:
		case UiIcons::Id::ViewLargeIcons:
		case UiIcons::Id::ViewGallery:
			return style->standardIcon(QStyle::SP_FileDialogContentsView);
		case UiIcons::Id::ViewDetails:
			return style->standardIcon(QStyle::SP_FileDialogDetailedView);
		case UiIcons::Id::ViewList:
			return style->standardIcon(QStyle::SP_FileDialogListView);
		case UiIcons::Id::MediaPrevious:
			return style->standardIcon(QStyle::SP_MediaSkipBackward);
		case UiIcons::Id::MediaPlay:
			return style->standardIcon(QStyle::SP_MediaPlay);
		case UiIcons::Id::MediaPause:
			return style->standardIcon(QStyle::SP_MediaPause);
		case UiIcons::Id::MediaStop:
			return style->standardIcon(QStyle::SP_MediaStop);
		case UiIcons::Id::MediaNext:
			return style->standardIcon(QStyle::SP_MediaSkipForward);
		case UiIcons::Id::Info:
		case UiIcons::Id::About:
		case UiIcons::Id::Details:
			return style->standardIcon(QStyle::SP_MessageBoxInformation);
		case UiIcons::Id::FullscreenEnter:
			return style->standardIcon(QStyle::SP_TitleBarMaxButton);
		case UiIcons::Id::FullscreenExit:
			return style->standardIcon(QStyle::SP_TitleBarNormalButton);
		default:
			return {};
	}
}
}  // namespace

namespace UiIcons {
QIcon icon(Id id) {
	return QIcon(QString::fromLatin1(resource_path(id)));
}

QIcon icon(Id id, const QStyle* style) {
	const QIcon svg_icon = icon(id);
	if (!svg_icon.isNull()) {
		return svg_icon;
	}
	return fallback_icon(id, style);
}
}  // namespace UiIcons
