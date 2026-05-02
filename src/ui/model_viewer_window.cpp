#include "ui/model_viewer_window.h"

#include <QApplication>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
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

#include "archive/archive.h"
#include "formats/lmp_image.h"
#include "formats/pcx_image.h"
#include "formats/quake3_skin.h"
#include "ui/preview_pane.h"
#include "ui/viewer_chrome.h"

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
			attempts->push_back(QCoreApplication::translate("ModelViewerWindow", "%1: %2")
			                        .arg(label, load_err.isEmpty()
			                                        ? QCoreApplication::translate("ModelViewerWindow", "unable to load archive")
			                                        : load_err));
		}
		return false;
	}

	QString read_err;
	if (!archive.read_entry_bytes(entry_name, out_bytes, &read_err, kMaxPaletteBytes)) {
		if (attempts) {
			attempts->push_back(
				QCoreApplication::translate("ModelViewerWindow", "%1: %2")
					.arg(label, read_err.isEmpty()
					                ? QCoreApplication::translate("ModelViewerWindow", "%1 not found").arg(entry_name)
					                : read_err));
		}
		return false;
	}
	return true;
}

QString file_base_name(const QString& name) {
	const int dot = name.lastIndexOf('.');
	return (dot >= 0) ? name.left(dot) : name;
}
}  // namespace

ModelViewerWindow::ModelViewerWindow(QWidget* parent) : QMainWindow(parent) {
	setAttribute(Qt::WA_DeleteOnClose, true);
	build_ui();
	install_event_filters();
	update_fullscreen_action();
	update_status();
	update_window_title();
	resize(1280, 820);
}

bool ModelViewerWindow::is_supported_model_ext(const QString& ext) {
	static const QSet<QString> kModelExts = {
		"mdl", "md2", "fm", "md3", "mdc", "md4", "mdr", "skb", "skd", "mdm", "glm", "iqm", "md5mesh", "tan", "obj", "lwo",
	};
	return kModelExts.contains(ext.toLower());
}

QString ModelViewerWindow::file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return (dot >= 0) ? lower.mid(dot + 1) : QString();
}

bool ModelViewerWindow::is_supported_model_path(const QString& file_path) {
	return is_supported_model_ext(file_ext_lower(QFileInfo(file_path).fileName()));
}

ModelViewerWindow* ModelViewerWindow::show_for_model(const QString& file_path, bool focus) {
	static QPointer<ModelViewerWindow> viewer;

	if (!viewer) {
		viewer = new ModelViewerWindow();
	}
	if (!viewer->open_model(file_path)) {
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

void ModelViewerWindow::build_ui() {
	preview_ = new PreviewPane(this);
	setCentralWidget(preview_);

	const ViewerChrome::Widgets chrome = ViewerChrome::setup(
		this,
		this,
		ViewerChrome::Spec{tr("Model Viewer"),
		                   tr("Model viewer actions"),
		                   tr("Show the previous model in this folder."),
		                   tr("Show the next model in this folder."),
		                   tr("Toggle fullscreen model viewing."),
		                   tr("Model position"),
		                   tr("Model path"),
		                   tr("Model viewer status")},
		&ModelViewerWindow::show_previous_model,
		&ModelViewerWindow::show_next_model,
		&ModelViewerWindow::toggle_fullscreen,
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

void ModelViewerWindow::install_event_filters() {
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

bool ModelViewerWindow::open_model(const QString& file_path) {
	const QFileInfo info(file_path);
	if (!info.exists() || !info.isFile()) {
		return false;
	}

	const QString abs = info.absoluteFilePath();
	if (!is_supported_model_path(abs)) {
		return false;
	}

	rebuild_model_list_for(abs);
	if (current_index_ < 0 || current_index_ >= model_paths_.size()) {
		return false;
	}
	show_current_model();
	return true;
}

QString ModelViewerWindow::current_model_path() const {
	if (current_index_ < 0 || current_index_ >= model_paths_.size()) {
		return {};
	}
	return model_paths_[current_index_];
}

void ModelViewerWindow::apply_preferences_from_settings() {
	if (preview_) {
		preview_->apply_preferences_from_settings();
	}
}

void ModelViewerWindow::rebuild_model_list_for(const QString& file_path) {
	model_paths_.clear();
	current_index_ = -1;

	const QFileInfo target(file_path);
	const QString target_abs = target.absoluteFilePath();
	const QDir parent(target.absolutePath());
	const QFileInfoList entries =
		parent.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

	model_paths_.reserve(entries.size());
	for (const QFileInfo& info : entries) {
		const QString abs = info.absoluteFilePath();
		if (is_supported_model_path(abs)) {
			model_paths_.push_back(abs);
		}
	}

	if (model_paths_.isEmpty() && is_supported_model_path(target_abs)) {
		model_paths_.push_back(target_abs);
	}

	for (int i = 0; i < model_paths_.size(); ++i) {
		if (paths_equal(normalize_for_compare(model_paths_[i]), normalize_for_compare(target_abs))) {
			current_index_ = i;
			break;
		}
	}
	if (current_index_ < 0 && !model_paths_.isEmpty()) {
		current_index_ = 0;
	}
}

void ModelViewerWindow::show_current_model() {
	if (!preview_) {
		return;
	}
	preview_->clear_asset_context();

	const QString model_path = current_model_path();
	if (model_path.isEmpty()) {
		preview_->set_current_file_info({}, -1, -1);
		preview_->show_message(tr("Model Viewer"), tr("No supported models found in this folder."));
		update_status();
		update_window_title();
		return;
	}

	const QFileInfo info(model_path);
	if (!info.exists() || !info.isFile()) {
		preview_->set_current_file_info(info.absoluteFilePath(), -1, -1);
		preview_->show_message(tr("Model Viewer"), tr("Model file not found."));
		update_status();
		update_window_title();
		return;
	}

	const QString ext = file_ext_lower(info.fileName());
	const QString skin_path = find_skin_on_disk(info.absoluteFilePath());
	QString unused_error;
	(void)ensure_quake1_palette(info.absoluteFilePath(), &unused_error);
	(void)ensure_quake2_palette(info.absoluteFilePath(), &unused_error);
	preview_->set_model_palettes(quake1_palette_, quake2_palette_);

	preview_->set_current_file_info(info.absoluteFilePath(),
	                                info.size(),
	                                info.lastModified().toUTC().toSecsSinceEpoch());
	PreviewAssetContext asset_context;
	QStringList palette_sources;
	if (quake1_palette_.size() == 256) {
		palette_sources.push_back(tr("Quake: %1").arg(
			quake1_palette_source_.isEmpty() ? QString("gfx/palette.lmp") : quake1_palette_source_));
	}
	if (quake2_palette_.size() == 256) {
		palette_sources.push_back(tr("Quake II: %1").arg(
			quake2_palette_source_.isEmpty() ? QString("pics/colormap.pcx") : quake2_palette_source_));
	}
	if (!palette_sources.isEmpty()) {
		asset_context.palette_provenance = palette_sources.join('\n');
	} else {
		asset_context.palette_provenance =
			tr("Embedded model textures when available; indexed Quake skins use grayscale fallback without a palette.");
	}

	QStringList companion_notes;
	if (!skin_path.isEmpty()) {
		companion_notes.push_back(tr("Skin: %1").arg(QFileInfo(skin_path).fileName()));
		if (file_ext_lower(QFileInfo(skin_path).fileName()) == "skin") {
			Quake3SkinMapping mapping;
			QString skin_err;
			if (parse_quake3_skin_file(skin_path, &mapping, &skin_err)) {
				QSet<QString> unique_shaders;
				for (auto it = mapping.surface_to_shader.cbegin(); it != mapping.surface_to_shader.cend(); ++it) {
					if (!it.value().trimmed().isEmpty()) {
						unique_shaders.insert(it.value().trimmed().toLower());
					}
				}
				asset_context.shader_dependencies =
					tr("%1 .skin mappings, %2 shader refs").arg(mapping.surface_to_shader.size()).arg(unique_shaders.size());
			}
		}
	}
	const QDir model_dir(info.absolutePath());
	if (ext == "mdl") {
		const QString base = info.completeBaseName();
		const QStringList candidates = {model_dir.filePath(base + "T.mdl"), model_dir.filePath(base + "t.mdl")};
		for (const QString& candidate : candidates) {
			if (QFileInfo::exists(candidate)) {
				companion_notes.push_back(tr("GoldSrc textures: %1").arg(QFileInfo(candidate).fileName()));
				break;
			}
		}
	}
	if (ext == "mdm") {
		const QString mdx = model_dir.filePath(info.completeBaseName() + ".mdx");
		if (QFileInfo::exists(mdx)) {
			companion_notes.push_back(tr("Skeleton: %1").arg(QFileInfo(mdx).fileName()));
		} else {
			asset_context.preview_fallback = tr("MDM companion .mdx not found; preview may use guide skeleton data.");
		}
	}
	if (ext == "glm") {
		const QString gla = model_dir.filePath(info.completeBaseName() + ".gla");
		if (QFileInfo::exists(gla)) {
			companion_notes.push_back(tr("Animation base: %1").arg(QFileInfo(gla).fileName()));
		}
	}
	asset_context.companion_resolution =
		companion_notes.isEmpty() ? tr("No external companion files resolved.") : companion_notes.join('\n');
	if (asset_context.shader_dependencies.isEmpty() && (ext == "md3" || ext == "mdc" || ext == "mdr")) {
		asset_context.shader_dependencies = skin_path.isEmpty()
		                                      ? tr("No .skin file resolved; using embedded shader names and same-folder lookup.")
		                                      : tr("Surface textures resolved through skin/model hints and same-folder lookup.");
	}
	preview_->set_asset_context(asset_context);
	const QString subtitle = QString("%1  |  %2/%3")
	                         .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
	                         .arg(current_index_ + 1)
	                         .arg(model_paths_.size());
	preview_->show_model_from_file(info.fileName(), subtitle, info.absoluteFilePath(), skin_path);

	Q_UNUSED(ext);
	update_status();
	update_window_title();
}

QString ModelViewerWindow::find_skin_on_disk(const QString& model_path) const {
	const QFileInfo model_info(model_path);
	const QDir dir(model_info.absolutePath());
	if (!dir.exists()) {
		return {};
	}

	const QString model_ext = file_ext_lower(model_info.fileName());
	const QString model_base = file_base_name(model_info.fileName());

	auto score_skin = [&](const QString& skin_leaf) -> int {
		const QString skin_ext = file_ext_lower(skin_leaf);
		const QString base = file_base_name(skin_leaf);
		const QString base_lower = base.toLower();
		const QString model_base_lower = model_base.toLower();

		int score = 0;
		if (!model_base.isEmpty()) {
			if (base.compare(model_base, Qt::CaseInsensitive) == 0) {
				score += 100;
			} else if (base.startsWith(model_base, Qt::CaseInsensitive)) {
				score += 70;
			}
		}
		if (base.compare("skin", Qt::CaseInsensitive) == 0) {
			score += 80;
		}
		if (base.contains("default", Qt::CaseInsensitive)) {
			score += 30;
		}
		if (base.endsWith("_glow", Qt::CaseInsensitive)) {
			score -= 200;
		}

		if ((model_ext == "md3" || model_ext == "mdc" || model_ext == "mdr") && skin_ext == "skin") {
			score += 160;
		}
		if (model_ext == "mdl" && !model_base_lower.isEmpty()) {
			const QString mdl_prefix = model_base_lower + "_";
			if (base_lower == model_base_lower + "_00_00") {
				score += 220;
			} else if (base_lower.startsWith(mdl_prefix)) {
				const QString suffix = base_lower.mid(mdl_prefix.size());
				const bool two_by_two_numeric = (suffix.size() == 5 && suffix[2] == '_' && suffix[0].isDigit() &&
				                                 suffix[1].isDigit() && suffix[3].isDigit() && suffix[4].isDigit());
				score += two_by_two_numeric ? 180 : 120;
			}
		}

		if (skin_ext == "png") {
			score += 20;
		} else if (skin_ext == "tga") {
			score += 18;
		} else if (skin_ext == "jpg" || skin_ext == "jpeg") {
			score += 16;
		} else if (skin_ext == "ftx") {
			score += 21;
		} else if (skin_ext == "lmp") {
			score += (model_ext == "mdl") ? 26 : 12;
		} else if (skin_ext == "mip") {
			score += (model_ext == "mdl") ? 24 : 11;
		} else if (skin_ext == "pcx") {
			score += 14;
		} else if (skin_ext == "m8" || skin_ext == "m32") {
			score += (model_ext == "fm") ? 30 : 13;
		} else if (skin_ext == "wal" || skin_ext == "swl") {
			score += 12;
		} else if (skin_ext == "dds") {
			score += 10;
		}

		return score;
	};

	QStringList filters = {"*.png", "*.tga", "*.jpg", "*.jpeg", "*.pcx", "*.wal", "*.swl", "*.m8", "*.m32", "*.dds", "*.lmp", "*.mip", "*.ftx"};
	if (model_ext == "md3" || model_ext == "mdc" || model_ext == "mdr") {
		filters.push_back("*.skin");
	}

	const QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
	if (files.isEmpty()) {
		return {};
	}

	QString best;
	int best_score = -1;
	for (const QString& leaf : files) {
		const int score = score_skin(leaf);
		if (score > best_score) {
			best_score = score;
			best = leaf;
		}
	}
	if (best_score < 40 || best.isEmpty()) {
		return {};
	}
	return dir.filePath(best);
}

void ModelViewerWindow::show_previous_model() {
	show_model_at(current_index_ - 1, true);
}

void ModelViewerWindow::show_next_model() {
	show_model_at(current_index_ + 1, true);
}

void ModelViewerWindow::show_model_at(int index, bool wrap) {
	if (model_paths_.isEmpty()) {
		return;
	}
	const int count = model_paths_.size();
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
	show_current_model();
}

void ModelViewerWindow::toggle_fullscreen() {
	if (isFullScreen()) {
		showNormal();
	} else {
		showFullScreen();
	}
	update_fullscreen_action();
}

void ModelViewerWindow::update_fullscreen_action() {
	ViewerChrome::update_fullscreen_action(fullscreen_action_, this);
}

void ModelViewerWindow::update_status() {
	ViewerChrome::update_status(
		index_label_, path_label_, prev_action_, next_action_, tr("Model"), model_paths_, current_index_, current_model_path());
}

void ModelViewerWindow::update_window_title() {
	const QString path = current_model_path();
	if (path.isEmpty()) {
		setWindowTitle(tr("PakFu Model Viewer"));
		return;
	}
	const QFileInfo info(path);
	setWindowTitle(tr("PakFu Model Viewer - %1").arg(info.fileName()));
}

bool ModelViewerWindow::eventFilter(QObject* watched, QEvent* event) {
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
			show_previous_model();
			return true;
		}
		if (delta.y() < 0) {
			show_next_model();
			return true;
		}
	}

	if (event->type() == QEvent::KeyPress) {
		auto* key = static_cast<QKeyEvent*>(event);
		switch (key->key()) {
			case Qt::Key_Left:
			case Qt::Key_Up:
			case Qt::Key_PageUp:
				show_previous_model();
				return true;
			case Qt::Key_Right:
			case Qt::Key_Down:
			case Qt::Key_PageDown:
			case Qt::Key_Space:
				show_next_model();
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

void ModelViewerWindow::closeEvent(QCloseEvent* event) {
	QMainWindow::closeEvent(event);
}

bool ModelViewerWindow::ensure_quake1_palette(const QString& model_path, QString* error) {
	if (error) {
		error->clear();
	}
	if (quake1_palette_.size() == 256) {
		return true;
	}

	const QString lookup_base = QFileInfo(model_path).absolutePath();
	if (!quake1_palette_error_.isEmpty() && paths_equal(lookup_base, quake1_palette_lookup_base_)) {
		if (error) {
			*error = quake1_palette_error_;
		}
		return false;
	}

	quake1_palette_lookup_base_ = lookup_base;
	quake1_palette_.clear();
	quake1_palette_error_.clear();
	quake1_palette_source_.clear();
	QStringList attempts;

	const auto try_lmp_bytes = [&](const QByteArray& lmp_bytes, const QString& where) -> bool {
		QVector<QRgb> palette;
		QString pal_err;
		if (!extract_lmp_palette_256(lmp_bytes, &palette, &pal_err) || palette.size() != 256) {
			attempts.push_back(tr("%1: %2").arg(where, pal_err.isEmpty() ? tr("invalid palette") : pal_err));
			return false;
		}
		quake1_palette_ = std::move(palette);
		quake1_palette_source_ = where;
		return true;
	};

	const auto try_file = [&](const QString& path, const QString& where) -> bool {
		if (!QFileInfo::exists(path)) {
			return false;
		}
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			attempts.push_back(tr("%1: unable to open file").arg(where));
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

		attempts.push_back(tr("%1: no usable palette entries found").arg(where));
		return false;
	};

	const QVector<QString> roots = parent_directories_for(model_path);
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
	                        ? tr("Unable to locate Quake palette (gfx/palette.lmp).")
	                        : tr("Unable to locate Quake palette (gfx/palette.lmp).\nTried:\n- %1")
	                            .arg(attempts.join("\n- "));
	if (error) {
		*error = quake1_palette_error_;
	}
	return false;
}

bool ModelViewerWindow::ensure_quake2_palette(const QString& model_path, QString* error) {
	if (error) {
		error->clear();
	}
	if (quake2_palette_.size() == 256) {
		return true;
	}

	const QString lookup_base = QFileInfo(model_path).absolutePath();
	if (!quake2_palette_error_.isEmpty() && paths_equal(lookup_base, quake2_palette_lookup_base_)) {
		if (error) {
			*error = quake2_palette_error_;
		}
		return false;
	}

	quake2_palette_lookup_base_ = lookup_base;
	quake2_palette_.clear();
	quake2_palette_error_.clear();
	quake2_palette_source_.clear();
	QStringList attempts;

	const auto try_pcx_bytes = [&](const QByteArray& pcx_bytes, const QString& where) -> bool {
		QVector<QRgb> palette;
		QString pal_err;
		if (!extract_pcx_palette_256(pcx_bytes, &palette, &pal_err) || palette.size() != 256) {
			attempts.push_back(tr("%1: %2").arg(where, pal_err.isEmpty() ? tr("invalid palette") : pal_err));
			return false;
		}
		quake2_palette_ = std::move(palette);
		quake2_palette_source_ = where;
		return true;
	};

	const auto try_file = [&](const QString& path, const QString& where) -> bool {
		if (!QFileInfo::exists(path)) {
			return false;
		}
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			attempts.push_back(tr("%1: unable to open file").arg(where));
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

	const QVector<QString> roots = parent_directories_for(model_path);
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
	                        ? tr("Unable to locate Quake II palette (pics/colormap.pcx).")
	                        : tr("Unable to locate Quake II palette (pics/colormap.pcx).\nTried:\n- %1")
	                            .arg(attempts.join("\n- "));
	if (error) {
		*error = quake2_palette_error_;
	}
	return false;
}
