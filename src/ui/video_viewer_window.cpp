#include "ui/video_viewer_window.h"

#include <QApplication>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSet>
#include <QShortcut>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QTimer>
#include <QWheelEvent>

#include "ui/preview_pane.h"
#include "ui/viewer_chrome.h"

namespace {
bool paths_equal(const QString& a, const QString& b) {
#if defined(Q_OS_WIN)
	return a.compare(b, Qt::CaseInsensitive) == 0;
#else
	return a == b;
#endif
}

QString normalize_for_compare(const QString& path) {
	return QFileInfo(path).absoluteFilePath();
}

bool auto_play_on_open_enabled() {
	const QString value = qEnvironmentVariable("PAKFU_AUTO_PLAY_ON_OPEN").trimmed().toLower();
	return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool debug_media_enabled() {
	const QString value = qEnvironmentVariable("PAKFU_DEBUG_MEDIA").trimmed().toLower();
	return value == "1" || value == "true" || value == "yes" || value == "on";
}
}  // namespace

VideoViewerWindow::VideoViewerWindow(QWidget* parent) : QMainWindow(parent) {
	setAttribute(Qt::WA_DeleteOnClose, true);
	build_ui();
	install_event_filters();
	update_fullscreen_action();
	update_status();
	update_window_title();
	resize(1280, 820);
}

bool VideoViewerWindow::is_supported_video_ext(const QString& ext) {
	static const QSet<QString> kVideoExts = {"cin", "roq", "bik", "ogv", "mp4", "mkv", "avi", "webm"};
	return kVideoExts.contains(ext.toLower());
}

QString VideoViewerWindow::file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return (dot >= 0) ? lower.mid(dot + 1) : QString();
}

bool VideoViewerWindow::is_supported_video_path(const QString& file_path) {
	return is_supported_video_ext(file_ext_lower(QFileInfo(file_path).fileName()));
}

VideoViewerWindow* VideoViewerWindow::show_for_video(const QString& file_path, bool focus) {
	static QPointer<VideoViewerWindow> viewer;

	if (!viewer) {
		viewer = new VideoViewerWindow();
	}
	if (!viewer->open_video(file_path)) {
		return nullptr;
	}

	viewer->show();
	if (focus) {
		if (viewer->isMinimized()) {
			viewer->showNormal();
		}
		viewer->raise();
		viewer->activateWindow();
	}
	return viewer;
}

void VideoViewerWindow::build_ui() {
	preview_ = new PreviewPane(this);
	setCentralWidget(preview_);

	connect(preview_, &PreviewPane::request_previous_video, this, &VideoViewerWindow::show_previous_video);
	connect(preview_, &PreviewPane::request_next_video, this, &VideoViewerWindow::show_next_video);

	const ViewerChrome::Widgets chrome = ViewerChrome::setup(
		this,
		this,
		ViewerChrome::Spec{tr("Video Viewer"),
		                   tr("Video viewer actions"),
		                   tr("Show the previous video in this folder."),
		                   tr("Show the next video in this folder."),
		                   tr("Toggle fullscreen video viewing."),
		                   tr("Video position"),
		                   tr("Video path"),
		                   tr("Video viewer status")},
		&VideoViewerWindow::show_previous_video,
		&VideoViewerWindow::show_next_video,
		&VideoViewerWindow::toggle_fullscreen,
		[this]() {
			if (isFullScreen()) {
				showNormal();
				update_fullscreen_action();
			}
		});
	prev_action_ = chrome.previous_action;
	next_action_ = chrome.next_action;
	fullscreen_action_ = chrome.fullscreen_action;
	index_label_ = chrome.index_label;
	path_label_ = chrome.path_label;
}

void VideoViewerWindow::install_event_filters() {
	installEventFilter(this);
	if (!preview_) {
		return;
	}
	preview_->installEventFilter(this);
	const QList<QObject*> children = preview_->findChildren<QObject*>();
	for (QObject* child : children) {
		child->installEventFilter(this);
	}
}

bool VideoViewerWindow::open_video(const QString& file_path) {
	const QFileInfo info(file_path);
	if (!info.exists() || !info.isFile()) {
		return false;
	}

	const QString abs = info.absoluteFilePath();
	if (!is_supported_video_path(abs)) {
		return false;
	}

	rebuild_video_list_for(abs);
	if (current_index_ < 0 || current_index_ >= video_paths_.size()) {
		return false;
	}
	show_current_video();
	return true;
}

QString VideoViewerWindow::current_video_path() const {
	if (current_index_ < 0 || current_index_ >= video_paths_.size()) {
		return {};
	}
	return video_paths_[current_index_];
}

void VideoViewerWindow::apply_preferences_from_settings() {
	if (preview_) {
		preview_->apply_preferences_from_settings();
	}
}

void VideoViewerWindow::rebuild_video_list_for(const QString& file_path) {
	video_paths_.clear();
	current_index_ = -1;

	const QFileInfo target(file_path);
	const QString target_abs = target.absoluteFilePath();
	const QDir parent(target.absolutePath());
	const QFileInfoList entries =
		parent.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

	video_paths_.reserve(entries.size());
	for (const QFileInfo& info : entries) {
		const QString abs = info.absoluteFilePath();
		if (is_supported_video_path(abs)) {
			video_paths_.push_back(abs);
		}
	}

	if (video_paths_.isEmpty() && is_supported_video_path(target_abs)) {
		video_paths_.push_back(target_abs);
	}

	for (int i = 0; i < video_paths_.size(); ++i) {
		if (paths_equal(normalize_for_compare(video_paths_[i]), normalize_for_compare(target_abs))) {
			current_index_ = i;
			break;
		}
	}
	if (current_index_ < 0 && !video_paths_.isEmpty()) {
		current_index_ = 0;
	}
}

void VideoViewerWindow::show_current_video() {
	if (!preview_) {
		return;
	}

	const QString video_path = current_video_path();
	if (video_path.isEmpty()) {
		preview_->show_message(tr("Video Viewer"), tr("No supported videos found in this folder."));
		update_status();
		update_window_title();
		return;
	}

	const QFileInfo info(video_path);
	if (!info.exists() || !info.isFile()) {
		preview_->show_message(tr("Video Viewer"), tr("Video file not found."));
		update_status();
		update_window_title();
		return;
	}

	preview_->set_current_file_info(info.absoluteFilePath(),
	                                info.size(),
	                                info.lastModified().toUTC().toSecsSinceEpoch());

	const QString subtitle = QString("%1  |  %2/%3")
	                         .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
	                         .arg(current_index_ + 1)
	                         .arg(video_paths_.size());
	const QString ext = file_ext_lower(info.fileName());
	if (debug_media_enabled()) {
		qInfo().noquote() << QString("VideoViewerWindow: show_current_video ext=%1 item=file").arg(ext);
	}
	if (ext == "cin" || ext == "roq") {
		preview_->show_cinematic_from_file(info.fileName(), subtitle, info.absoluteFilePath());
	} else {
		preview_->show_video_from_file(info.fileName(), subtitle, info.absoluteFilePath());
	}

	if (auto_play_on_open_enabled()) {
		if (debug_media_enabled()) {
			qInfo() << "VideoViewerWindow: autoplay requested";
		}
		QTimer::singleShot(0, preview_, [preview = preview_]() {
			if (preview) {
				preview->start_playback_from_beginning();
			}
		});
	}

	update_status();
	update_window_title();
}

void VideoViewerWindow::show_previous_video() {
	show_video_at(current_index_ - 1, true);
}

void VideoViewerWindow::show_next_video() {
	show_video_at(current_index_ + 1, true);
}

void VideoViewerWindow::show_video_at(int index, bool wrap) {
	if (video_paths_.isEmpty()) {
		return;
	}
	const int count = video_paths_.size();
	if (count <= 0) {
		return;
	}

	int next = index;
	if (wrap) {
		next = ((next % count) + count) % count;
	} else {
		next = qBound(0, next, count - 1);
	}
	if (next == current_index_) {
		return;
	}
	current_index_ = next;
	show_current_video();
}

void VideoViewerWindow::toggle_fullscreen() {
	if (isFullScreen()) {
		showNormal();
	} else {
		showFullScreen();
	}
	update_fullscreen_action();
}

void VideoViewerWindow::update_fullscreen_action() {
	ViewerChrome::update_fullscreen_action(fullscreen_action_, this);
}

void VideoViewerWindow::update_status() {
	ViewerChrome::update_status(
		index_label_, path_label_, prev_action_, next_action_, tr("Video"), video_paths_, current_index_, current_video_path());
}

void VideoViewerWindow::update_window_title() {
	const QString path = current_video_path();
	if (path.isEmpty()) {
		setWindowTitle(tr("PakFu Video Viewer"));
		return;
	}
	const QFileInfo info(path);
	setWindowTitle(tr("PakFu Video Viewer - %1").arg(info.fileName()));
}

bool VideoViewerWindow::eventFilter(QObject* watched, QEvent* event) {
	if (!event) {
		return QMainWindow::eventFilter(watched, event);
	}
	if (QApplication::activePopupWidget()) {
		return QMainWindow::eventFilter(watched, event);
	}

	if (watched) {
		if (QWidget* w = qobject_cast<QWidget*>(watched)) {
			if (w != this && !isAncestorOf(w)) {
				return QMainWindow::eventFilter(watched, event);
			}
		}
	}

	if (ViewerChrome::should_ignore_navigation_event_target(watched)) {
		return QMainWindow::eventFilter(watched, event);
	}

	if (event->type() == QEvent::MouseButtonPress) {
		auto* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::MiddleButton) {
			toggle_fullscreen();
			return true;
		}
	}

	if (event->type() == QEvent::Wheel) {
		auto* wheel = static_cast<QWheelEvent*>(event);
		const QPoint delta = wheel->angleDelta();
		if (delta.y() > 0) {
			show_previous_video();
			return true;
		}
		if (delta.y() < 0) {
			show_next_video();
			return true;
		}
	}

	if (event->type() == QEvent::KeyPress) {
		auto* key = static_cast<QKeyEvent*>(event);
		switch (key->key()) {
			case Qt::Key_Left:
			case Qt::Key_Up:
			case Qt::Key_PageUp:
				show_previous_video();
				return true;
			case Qt::Key_Right:
			case Qt::Key_Down:
			case Qt::Key_PageDown:
			case Qt::Key_Space:
				show_next_video();
				return true;
			case Qt::Key_F11:
				toggle_fullscreen();
				return true;
			case Qt::Key_Escape:
				if (isFullScreen()) {
					showNormal();
					update_fullscreen_action();
					return true;
				}
				break;
			default:
				break;
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

void VideoViewerWindow::closeEvent(QCloseEvent* event) {
	QMainWindow::closeEvent(event);
}
