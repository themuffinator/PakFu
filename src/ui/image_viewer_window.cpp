#include "ui/image_viewer_window.h"

#include <algorithm>

#include <QApplication>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
#include <QTimeZone>
#include <QToolBar>
#include <QWheelEvent>
#include <QtGlobal>

#include "archive/archive.h"
#include "formats/image_loader.h"
#include "formats/lmp_image.h"
#include "formats/pcx_image.h"
#include "ui/preview_pane.h"
#include "ui/ui_icons.h"

namespace {
constexpr qint64 kMaxPaletteBytes = 8LL * 1024 * 1024;

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

QVector<QString> parent_directories_for(const QString& file_path) {
	QVector<QString> out;
	QSet<QString> seen;
	QDir dir(QFileInfo(file_path).absolutePath());
	while (true) {
		const QString abs = dir.absolutePath();
		const QString key = abs.toLower();
		if (seen.contains(key)) {
			break;
		}
		seen.insert(key);
		out.push_back(abs);
		if (!dir.cdUp()) {
			break;
		}
	}
	return out;
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

bool try_load_archive_entry(const QString& archive_path,
                            const QString& entry_name,
                            QByteArray* out_bytes,
                            QStringList* attempts,
                            const QString& label) {
	if (!out_bytes || archive_path.isEmpty()) {
		return false;
	}
	if (!QFileInfo::exists(archive_path)) {
		return false;
	}

	Archive archive;
	QString load_err;
	if (!archive.load(archive_path, &load_err) || !archive.is_loaded()) {
		if (attempts) {
			attempts->push_back(QString("%1: %2").arg(label, load_err.isEmpty() ? "unable to load archive" : load_err));
		}
		return false;
	}

	QString read_err;
	if (!archive.read_entry_bytes(entry_name, out_bytes, &read_err, kMaxPaletteBytes)) {
		if (attempts) {
			attempts->push_back(
				QString("%1: %2").arg(label, read_err.isEmpty() ? QString("%1 not found").arg(entry_name) : read_err));
		}
		return false;
	}
	return true;
}
}  // namespace

ImageViewerWindow::ImageViewerWindow(QWidget* parent) : QMainWindow(parent) {
	setAttribute(Qt::WA_DeleteOnClose, true);
	build_ui();
	install_event_filters();
	update_fullscreen_action();
	update_status();
	update_window_title();
	resize(1200, 800);
}

bool ImageViewerWindow::is_supported_image_ext(const QString& ext) {
	static const QSet<QString> kImageExts = {
		"png", "jpg", "jpeg", "bmp", "gif", "tga", "pcx", "wal", "swl", "dds", "lmp", "mip", "ftx", "tif", "tiff",
	};
	return kImageExts.contains(ext.toLower());
}

QString ImageViewerWindow::file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return (dot >= 0) ? lower.mid(dot + 1) : QString();
}

bool ImageViewerWindow::is_supported_image_path(const QString& file_path) {
	return is_supported_image_ext(file_ext_lower(QFileInfo(file_path).fileName()));
}

ImageViewerWindow* ImageViewerWindow::show_for_image(const QString& file_path, bool focus) {
	static QPointer<ImageViewerWindow> viewer;

	if (!viewer) {
		viewer = new ImageViewerWindow();
	}
	if (!viewer->open_image(file_path)) {
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

void ImageViewerWindow::build_ui() {
	preview_ = new PreviewPane(this);
	setCentralWidget(preview_);

	connect(preview_, &PreviewPane::request_image_mip_level, this, [this](int) {
		show_current_image();
	});

	auto* toolbar = addToolBar("Image Viewer");
	toolbar->setMovable(false);
	toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

	prev_action_ = toolbar->addAction(UiIcons::icon(UiIcons::Id::MediaPrevious, style()), "Previous");
	next_action_ = toolbar->addAction(UiIcons::icon(UiIcons::Id::MediaNext, style()), "Next");
	toolbar->addSeparator();
	fullscreen_action_ = toolbar->addAction(UiIcons::icon(UiIcons::Id::FullscreenEnter, style()), "Fullscreen");

	connect(prev_action_, &QAction::triggered, this, &ImageViewerWindow::show_previous_image);
	connect(next_action_, &QAction::triggered, this, &ImageViewerWindow::show_next_image);
	connect(fullscreen_action_, &QAction::triggered, this, &ImageViewerWindow::toggle_fullscreen);

	auto* left_shortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
	connect(left_shortcut, &QShortcut::activated, this, &ImageViewerWindow::show_previous_image);
	auto* right_shortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
	connect(right_shortcut, &QShortcut::activated, this, &ImageViewerWindow::show_next_image);
	auto* f11_shortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
	connect(f11_shortcut, &QShortcut::activated, this, &ImageViewerWindow::toggle_fullscreen);
	auto* fullscreen_shortcut = new QShortcut(QKeySequence::FullScreen, this);
	connect(fullscreen_shortcut, &QShortcut::activated, this, &ImageViewerWindow::toggle_fullscreen);
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

void ImageViewerWindow::install_event_filters() {
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

bool ImageViewerWindow::open_image(const QString& file_path) {
	const QFileInfo info(file_path);
	if (!info.exists() || !info.isFile()) {
		return false;
	}

	const QString abs = info.absoluteFilePath();
	if (!is_supported_image_path(abs)) {
		return false;
	}

	rebuild_image_list_for(abs);
	if (current_index_ < 0 || current_index_ >= image_paths_.size()) {
		return false;
	}
	show_current_image();
	return true;
}

QString ImageViewerWindow::current_image_path() const {
	if (current_index_ < 0 || current_index_ >= image_paths_.size()) {
		return {};
	}
	return image_paths_[current_index_];
}

void ImageViewerWindow::rebuild_image_list_for(const QString& file_path) {
	image_paths_.clear();
	current_index_ = -1;

	const QFileInfo target(file_path);
	const QString target_abs = target.absoluteFilePath();
	const QDir parent(target.absolutePath());
	const QFileInfoList entries =
		parent.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

	image_paths_.reserve(entries.size());
	for (const QFileInfo& info : entries) {
		const QString abs = info.absoluteFilePath();
		if (is_supported_image_path(abs)) {
			image_paths_.push_back(abs);
		}
	}

	if (image_paths_.isEmpty() && is_supported_image_path(target_abs)) {
		image_paths_.push_back(target_abs);
	}

	for (int i = 0; i < image_paths_.size(); ++i) {
		if (paths_equal(normalize_for_compare(image_paths_[i]), normalize_for_compare(target_abs))) {
			current_index_ = i;
			break;
		}
	}
	if (current_index_ < 0 && !image_paths_.isEmpty()) {
		current_index_ = 0;
	}
}

void ImageViewerWindow::show_current_image() {
	if (!preview_) {
		return;
	}

	const QString image_path = current_image_path();
	if (image_path.isEmpty()) {
		preview_->show_message("Image Viewer", "No supported images found in this folder.");
		update_status();
		update_window_title();
		return;
	}

	const QFileInfo info(image_path);
	if (!info.exists() || !info.isFile()) {
		preview_->show_message("Image Viewer", "Image file not found.");
		update_status();
		update_window_title();
		return;
	}

	const QString ext = file_ext_lower(info.fileName());
	const bool supports_mips = (ext == "wal" || ext == "swl" || ext == "mip");
	preview_->set_image_mip_controls(supports_mips, preview_->image_mip_level());

	ImageDecodeOptions options;
	options.mip_level = supports_mips ? preview_->image_mip_level() : 0;

	if (ext == "wal") {
		QString pal_err;
		if (!ensure_quake2_palette(image_path, &pal_err)) {
			preview_->show_message(info.fileName(),
			                       pal_err.isEmpty() ? "Unable to locate Quake II palette required for WAL preview." : pal_err);
			update_status();
			update_window_title();
			return;
		}
		options.palette = &quake2_palette_;
	} else if (ext == "lmp" || ext == "mip") {
		QString pal_err;
		if (ensure_quake1_palette(image_path, &pal_err)) {
			options.palette = &quake1_palette_;
		}
	}

	ImageDecodeResult decoded = decode_image_file(image_path, options);
	if (!decoded.ok() && options.palette && ext != "wal") {
		decoded = decode_image_file(image_path, ImageDecodeOptions{});
	}

	preview_->set_current_file_info(info.absoluteFilePath(),
	                                info.size(),
	                                info.lastModified().toUTC().toSecsSinceEpoch());

	if (!decoded.ok()) {
		preview_->show_message(info.fileName(),
		                       decoded.error.isEmpty() ? "Unable to decode this image format." : decoded.error);
		update_status();
		update_window_title();
		return;
	}

	const QString subtitle = QString("%1  |  %2/%3")
	                         .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
	                         .arg(current_index_ + 1)
	                         .arg(image_paths_.size());
	preview_->show_image(info.fileName(), subtitle, decoded.image);

	update_status();
	update_window_title();
}

void ImageViewerWindow::show_previous_image() {
	show_image_at(current_index_ - 1, true);
}

void ImageViewerWindow::show_next_image() {
	show_image_at(current_index_ + 1, true);
}

void ImageViewerWindow::show_image_at(int index, bool wrap) {
	if (image_paths_.isEmpty()) {
		return;
	}
	const int count = image_paths_.size();
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
	show_current_image();
}

void ImageViewerWindow::toggle_fullscreen() {
	if (isFullScreen()) {
		showNormal();
	} else {
		showFullScreen();
	}
	update_fullscreen_action();
}

void ImageViewerWindow::update_fullscreen_action() {
	if (!fullscreen_action_) {
		return;
	}
	const bool full = isFullScreen();
	fullscreen_action_->setText(full ? "Exit Fullscreen" : "Fullscreen");
	fullscreen_action_->setIcon(
		UiIcons::icon(full ? UiIcons::Id::FullscreenExit : UiIcons::Id::FullscreenEnter, style()));
}

void ImageViewerWindow::update_status() {
	if (index_label_) {
		if (image_paths_.isEmpty() || current_index_ < 0) {
			index_label_->setText("Image 0/0");
		} else {
			index_label_->setText(QString("Image %1/%2").arg(current_index_ + 1).arg(image_paths_.size()));
		}
	}
	if (path_label_) {
		const QString path = current_image_path();
		path_label_->setText(path.isEmpty() ? QString() : QDir::toNativeSeparators(path));
		path_label_->setToolTip(path.isEmpty() ? QString() : QDir::toNativeSeparators(path));
	}

	const bool can_cycle = image_paths_.size() > 1;
	if (prev_action_) {
		prev_action_->setEnabled(can_cycle);
	}
	if (next_action_) {
		next_action_->setEnabled(can_cycle);
	}
}

void ImageViewerWindow::update_window_title() {
	const QString path = current_image_path();
	if (path.isEmpty()) {
		setWindowTitle("PakFu Image Viewer");
		return;
	}
	const QFileInfo info(path);
	setWindowTitle(QString("PakFu Image Viewer - %1").arg(info.fileName()));
}

bool ImageViewerWindow::eventFilter(QObject* watched, QEvent* event) {
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
			show_previous_image();
			return true;
		}
		if (delta.y() < 0) {
			show_next_image();
			return true;
		}
	}

	if (event->type() == QEvent::KeyPress) {
		auto* key = static_cast<QKeyEvent*>(event);
		switch (key->key()) {
			case Qt::Key_Left:
			case Qt::Key_Up:
			case Qt::Key_PageUp:
				show_previous_image();
				return true;
			case Qt::Key_Right:
			case Qt::Key_Down:
			case Qt::Key_PageDown:
			case Qt::Key_Space:
				show_next_image();
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

void ImageViewerWindow::closeEvent(QCloseEvent* event) {
	QMainWindow::closeEvent(event);
}

bool ImageViewerWindow::ensure_quake1_palette(const QString& image_path, QString* error) {
	if (error) {
		error->clear();
	}
	if (quake1_palette_.size() == 256) {
		return true;
	}

	const QString lookup_base = QFileInfo(image_path).absolutePath();
	if (!quake1_palette_error_.isEmpty() && paths_equal(lookup_base, quake1_palette_lookup_base_)) {
		if (error) {
			*error = quake1_palette_error_;
		}
		return false;
	}

	quake1_palette_lookup_base_ = lookup_base;
	quake1_palette_.clear();
	quake1_palette_error_.clear();
	QStringList attempts;

	const auto try_lmp_bytes = [&](const QByteArray& lmp_bytes, const QString& where) -> bool {
		QVector<QRgb> palette;
		QString pal_err;
		if (!extract_lmp_palette_256(lmp_bytes, &palette, &pal_err) || palette.size() != 256) {
			attempts.push_back(QString("%1: %2").arg(where, pal_err.isEmpty() ? "invalid palette" : pal_err));
			return false;
		}
		quake1_palette_ = std::move(palette);
		return true;
	};

	const auto try_file = [&](const QString& path, const QString& where) -> bool {
		if (!QFileInfo::exists(path)) {
			return false;
		}
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			attempts.push_back(QString("%1: unable to open file").arg(where));
			return false;
		}
		return try_lmp_bytes(file.read(kMaxPaletteBytes), where);
	};

	const auto try_archive = [&](const QString& archive_path, const QString& where) -> bool {
		QByteArray bytes;
		if (try_load_archive_entry(archive_path, "gfx/palette.lmp", &bytes, &attempts, where + ": gfx/palette.lmp")) {
			if (try_lmp_bytes(bytes, where + ": gfx/palette.lmp")) {
				return true;
			}
		}

		bytes.clear();
		if (try_load_archive_entry(archive_path, "palette.lmp", &bytes, nullptr, where + ": palette.lmp")) {
			if (try_lmp_bytes(bytes, where + ": palette.lmp")) {
				return true;
			}
		}

		bytes.clear();
		if (try_load_archive_entry(archive_path, "palette", &bytes, nullptr, where + ": palette")) {
			if (try_lmp_bytes(bytes, where + ": palette")) {
				return true;
			}
		}

		attempts.push_back(QString("%1: no usable palette entries found").arg(where));
		return false;
	};

	const QVector<QString> roots = parent_directories_for(image_path);
	for (const QString& root : roots) {
		const QDir base(root);

		if (try_file(base.filePath("gfx/palette.lmp"), root + ": gfx/palette.lmp")) {
			return true;
		}
		if (try_file(base.filePath("id1/gfx/palette.lmp"), root + ": id1/gfx/palette.lmp")) {
			return true;
		}
		if (try_file(base.filePath("rerelease/id1/gfx/palette.lmp"), root + ": rerelease/id1/gfx/palette.lmp")) {
			return true;
		}

		if (try_archive(base.filePath("pak0.pak"), root + ": pak0.pak")) {
			return true;
		}
		if (try_archive(base.filePath("id1/pak0.pak"), root + ": id1/pak0.pak")) {
			return true;
		}
		if (try_archive(base.filePath("rerelease/id1/pak0.pak"), root + ": rerelease/id1/pak0.pak")) {
			return true;
		}
	}

	quake1_palette_error_ = attempts.isEmpty()
	                        ? "Unable to locate Quake palette (gfx/palette.lmp)."
	                        : QString("Unable to locate Quake palette (gfx/palette.lmp).\nTried:\n- %1")
	                            .arg(attempts.join("\n- "));
	if (error) {
		*error = quake1_palette_error_;
	}
	return false;
}

bool ImageViewerWindow::ensure_quake2_palette(const QString& image_path, QString* error) {
	if (error) {
		error->clear();
	}
	if (quake2_palette_.size() == 256) {
		return true;
	}

	const QString lookup_base = QFileInfo(image_path).absolutePath();
	if (!quake2_palette_error_.isEmpty() && paths_equal(lookup_base, quake2_palette_lookup_base_)) {
		if (error) {
			*error = quake2_palette_error_;
		}
		return false;
	}

	quake2_palette_lookup_base_ = lookup_base;
	quake2_palette_.clear();
	quake2_palette_error_.clear();
	QStringList attempts;

	const auto try_pcx_bytes = [&](const QByteArray& pcx_bytes, const QString& where) -> bool {
		QVector<QRgb> palette;
		QString pal_err;
		if (!extract_pcx_palette_256(pcx_bytes, &palette, &pal_err) || palette.size() != 256) {
			attempts.push_back(QString("%1: %2").arg(where, pal_err.isEmpty() ? "invalid palette" : pal_err));
			return false;
		}
		quake2_palette_ = std::move(palette);
		return true;
	};

	const auto try_file = [&](const QString& path, const QString& where) -> bool {
		if (!QFileInfo::exists(path)) {
			return false;
		}
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			attempts.push_back(QString("%1: unable to open file").arg(where));
			return false;
		}
		return try_pcx_bytes(file.read(kMaxPaletteBytes), where);
	};

	const auto try_archive = [&](const QString& archive_path, const QString& where) -> bool {
		QByteArray bytes;
		if (!try_load_archive_entry(archive_path, "pics/colormap.pcx", &bytes, &attempts, where)) {
			return false;
		}
		return try_pcx_bytes(bytes, where + ": pics/colormap.pcx");
	};

	const QVector<QString> roots = parent_directories_for(image_path);
	for (const QString& root : roots) {
		const QDir base(root);

		if (try_file(base.filePath("pics/colormap.pcx"), root + ": pics/colormap.pcx")) {
			return true;
		}
		if (try_file(base.filePath("baseq2/pics/colormap.pcx"), root + ": baseq2/pics/colormap.pcx")) {
			return true;
		}
		if (try_file(base.filePath("rerelease/baseq2/pics/colormap.pcx"),
		             root + ": rerelease/baseq2/pics/colormap.pcx")) {
			return true;
		}

		if (try_archive(base.filePath("pak0.pak"), root + ": pak0.pak")) {
			return true;
		}
		if (try_archive(base.filePath("baseq2/pak0.pak"), root + ": baseq2/pak0.pak")) {
			return true;
		}
		if (try_archive(base.filePath("rerelease/baseq2/pak0.pak"), root + ": rerelease/baseq2/pak0.pak")) {
			return true;
		}
	}

	quake2_palette_error_ = attempts.isEmpty()
	                        ? "Unable to locate Quake II palette (pics/colormap.pcx)."
	                        : QString("Unable to locate Quake II palette (pics/colormap.pcx).\nTried:\n- %1")
	                            .arg(attempts.join("\n- "));
	if (error) {
		*error = quake2_palette_error_;
	}
	return false;
}
