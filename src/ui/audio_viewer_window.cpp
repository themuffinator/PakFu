#include "ui/audio_viewer_window.h"

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
#include <QWheelEvent>

#include "ui/preview_pane.h"
#include "ui/ui_icons.h"

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

bool should_ignore_navigation_event_target(QObject* watched) {
	if (!watched) {
		return false;
	}
	return qobject_cast<QComboBox*>(watched) != nullptr ||
	       qobject_cast<QAbstractSpinBox*>(watched) != nullptr ||
	       qobject_cast<QAbstractSlider*>(watched) != nullptr ||
	       qobject_cast<QLineEdit*>(watched) != nullptr ||
	       qobject_cast<QTextEdit*>(watched) != nullptr ||
	       qobject_cast<QPlainTextEdit*>(watched) != nullptr;
}
}  // namespace

AudioViewerWindow::AudioViewerWindow(QWidget* parent) : QMainWindow(parent) {
	setAttribute(Qt::WA_DeleteOnClose, true);
	build_ui();
	install_event_filters();
	update_fullscreen_action();
	update_status();
	update_window_title();
	resize(980, 680);
}

bool AudioViewerWindow::is_supported_audio_ext(const QString& ext) {
	static const QSet<QString> kAudioExts = {"wav", "ogg", "mp3"};
	return kAudioExts.contains(ext.toLower());
}

QString AudioViewerWindow::file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return (dot >= 0) ? lower.mid(dot + 1) : QString();
}

bool AudioViewerWindow::is_supported_audio_path(const QString& file_path) {
	return is_supported_audio_ext(file_ext_lower(QFileInfo(file_path).fileName()));
}

AudioViewerWindow* AudioViewerWindow::show_for_audio(const QString& file_path, bool focus) {
	static QPointer<AudioViewerWindow> viewer;

	if (!viewer) {
		viewer = new AudioViewerWindow();
	}
	if (!viewer->open_audio(file_path)) {
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

void AudioViewerWindow::build_ui() {
	preview_ = new PreviewPane(this);
	setCentralWidget(preview_);

	connect(preview_, &PreviewPane::request_previous_audio, this, &AudioViewerWindow::show_previous_audio);
	connect(preview_, &PreviewPane::request_next_audio, this, &AudioViewerWindow::show_next_audio);

	auto* toolbar = addToolBar("Audio Viewer");
	toolbar->setMovable(false);
	toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

	prev_action_ = toolbar->addAction(UiIcons::icon(UiIcons::Id::MediaPrevious, style()), "Previous");
	next_action_ = toolbar->addAction(UiIcons::icon(UiIcons::Id::MediaNext, style()), "Next");
	toolbar->addSeparator();
	fullscreen_action_ = toolbar->addAction(UiIcons::icon(UiIcons::Id::FullscreenEnter, style()), "Fullscreen");

	connect(prev_action_, &QAction::triggered, this, &AudioViewerWindow::show_previous_audio);
	connect(next_action_, &QAction::triggered, this, &AudioViewerWindow::show_next_audio);
	connect(fullscreen_action_, &QAction::triggered, this, &AudioViewerWindow::toggle_fullscreen);

	auto* left_shortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
	connect(left_shortcut, &QShortcut::activated, this, &AudioViewerWindow::show_previous_audio);
	auto* right_shortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
	connect(right_shortcut, &QShortcut::activated, this, &AudioViewerWindow::show_next_audio);
	auto* f11_shortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
	connect(f11_shortcut, &QShortcut::activated, this, &AudioViewerWindow::toggle_fullscreen);
	auto* fullscreen_shortcut = new QShortcut(QKeySequence::FullScreen, this);
	connect(fullscreen_shortcut, &QShortcut::activated, this, &AudioViewerWindow::toggle_fullscreen);
	auto* esc_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(esc_shortcut, &QShortcut::activated, this, [this]() {
		if (isFullScreen()) {
			showNormal();
			update_fullscreen_action();
		}
	});

	index_label_ = new QLabel(this);
	path_label_ = new QLabel(this);
	path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);

	if (statusBar()) {
		statusBar()->addPermanentWidget(index_label_);
		statusBar()->addWidget(path_label_, 1);
	}
}

void AudioViewerWindow::install_event_filters() {
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

bool AudioViewerWindow::open_audio(const QString& file_path) {
	const QFileInfo info(file_path);
	if (!info.exists() || !info.isFile()) {
		return false;
	}

	const QString abs = info.absoluteFilePath();
	if (!is_supported_audio_path(abs)) {
		return false;
	}

	rebuild_audio_list_for(abs);
	if (current_index_ < 0 || current_index_ >= audio_paths_.size()) {
		return false;
	}
	show_current_audio();
	return true;
}

QString AudioViewerWindow::current_audio_path() const {
	if (current_index_ < 0 || current_index_ >= audio_paths_.size()) {
		return {};
	}
	return audio_paths_[current_index_];
}

void AudioViewerWindow::rebuild_audio_list_for(const QString& file_path) {
	audio_paths_.clear();
	current_index_ = -1;

	const QFileInfo target(file_path);
	const QString target_abs = target.absoluteFilePath();
	const QDir parent(target.absolutePath());
	const QFileInfoList entries =
		parent.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

	audio_paths_.reserve(entries.size());
	for (const QFileInfo& info : entries) {
		const QString abs = info.absoluteFilePath();
		if (is_supported_audio_path(abs)) {
			audio_paths_.push_back(abs);
		}
	}

	if (audio_paths_.isEmpty() && is_supported_audio_path(target_abs)) {
		audio_paths_.push_back(target_abs);
	}

	for (int i = 0; i < audio_paths_.size(); ++i) {
		if (paths_equal(normalize_for_compare(audio_paths_[i]), normalize_for_compare(target_abs))) {
			current_index_ = i;
			break;
		}
	}
	if (current_index_ < 0 && !audio_paths_.isEmpty()) {
		current_index_ = 0;
	}
}

void AudioViewerWindow::show_current_audio() {
	if (!preview_) {
		return;
	}

	const QString audio_path = current_audio_path();
	if (audio_path.isEmpty()) {
		preview_->show_message("Audio Viewer", "No supported audio files found in this folder.");
		update_status();
		update_window_title();
		return;
	}

	const QFileInfo info(audio_path);
	if (!info.exists() || !info.isFile()) {
		preview_->show_message("Audio Viewer", "Audio file not found.");
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
	                         .arg(audio_paths_.size());
	preview_->show_audio_from_file(info.fileName(), subtitle, info.absoluteFilePath());

	update_status();
	update_window_title();
}

void AudioViewerWindow::show_previous_audio() {
	show_audio_at(current_index_ - 1, true);
}

void AudioViewerWindow::show_next_audio() {
	show_audio_at(current_index_ + 1, true);
}

void AudioViewerWindow::show_audio_at(int index, bool wrap) {
	if (audio_paths_.isEmpty()) {
		return;
	}
	const int count = audio_paths_.size();
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
	show_current_audio();
}

void AudioViewerWindow::toggle_fullscreen() {
	if (isFullScreen()) {
		showNormal();
	} else {
		showFullScreen();
	}
	update_fullscreen_action();
}

void AudioViewerWindow::update_fullscreen_action() {
	if (!fullscreen_action_) {
		return;
	}
	const bool full = isFullScreen();
	fullscreen_action_->setText(full ? "Exit Fullscreen" : "Fullscreen");
	fullscreen_action_->setIcon(
		UiIcons::icon(full ? UiIcons::Id::FullscreenExit : UiIcons::Id::FullscreenEnter, style()));
}

void AudioViewerWindow::update_status() {
	if (index_label_) {
		if (audio_paths_.isEmpty() || current_index_ < 0) {
			index_label_->setText("Audio 0/0");
		} else {
			index_label_->setText(QString("Audio %1/%2").arg(current_index_ + 1).arg(audio_paths_.size()));
		}
	}
	if (path_label_) {
		const QString path = current_audio_path();
		path_label_->setText(path.isEmpty() ? QString() : QDir::toNativeSeparators(path));
		path_label_->setToolTip(path.isEmpty() ? QString() : QDir::toNativeSeparators(path));
	}

	const bool can_cycle = audio_paths_.size() > 1;
	if (prev_action_) {
		prev_action_->setEnabled(can_cycle);
	}
	if (next_action_) {
		next_action_->setEnabled(can_cycle);
	}
}

void AudioViewerWindow::update_window_title() {
	const QString path = current_audio_path();
	if (path.isEmpty()) {
		setWindowTitle("PakFu Audio Viewer");
		return;
	}
	const QFileInfo info(path);
	setWindowTitle(QString("PakFu Audio Viewer - %1").arg(info.fileName()));
}

bool AudioViewerWindow::eventFilter(QObject* watched, QEvent* event) {
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

	if (should_ignore_navigation_event_target(watched)) {
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
			show_previous_audio();
			return true;
		}
		if (delta.y() < 0) {
			show_next_audio();
			return true;
		}
	}

	if (event->type() == QEvent::KeyPress) {
		auto* key = static_cast<QKeyEvent*>(event);
		switch (key->key()) {
			case Qt::Key_Left:
			case Qt::Key_Up:
			case Qt::Key_PageUp:
				show_previous_audio();
				return true;
			case Qt::Key_Right:
			case Qt::Key_Down:
			case Qt::Key_PageDown:
			case Qt::Key_Space:
				show_next_audio();
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

void AudioViewerWindow::closeEvent(QCloseEvent* event) {
	QMainWindow::closeEvent(event);
}

