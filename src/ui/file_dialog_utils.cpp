#include "ui/file_dialog_utils.h"

#include <QByteArray>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr int kMaxHistoryEntries = 16;
constexpr char kDialogSettingsRoot[] = "ui/fileDialogs";

bool env_var_is_truthy(const char* name) {
	const QByteArray raw = qgetenv(name).trimmed().toLower();
	if (raw.isEmpty()) {
		return false;
	}
	return raw != "0" && raw != "false" && raw != "no" && raw != "off";
}

QString directory_key(QString path) {
	path = QDir::cleanPath(path);
#if defined(Q_OS_WIN)
	path = path.toLower();
#endif
	return path;
}

QString normalize_directory_path(const QString& path_in) {
	QString path = QDir::fromNativeSeparators(path_in.trimmed());
	if (path.isEmpty()) {
		return {};
	}
	path = QDir::cleanPath(path);
	return QFileInfo(path).absoluteFilePath();
}

QString settings_prefix(const QString& key) {
	const QString clean_key = key.trimmed().isEmpty() ? "default" : key.trimmed();
	return QString("%1/%2").arg(QLatin1String(kDialogSettingsRoot), clean_key);
}

bool uses_qt_dialog(const QFileDialog* dialog) {
	return dialog && dialog->testOption(QFileDialog::DontUseNativeDialog);
}

QString sidebar_url_key(const QUrl& url_in) {
	if (!url_in.isValid()) {
		return {};
	}

	QUrl url = url_in;
	if (url.isLocalFile() || url.scheme().compare("file", Qt::CaseInsensitive) == 0) {
		const QString path = normalize_directory_path(url.toLocalFile());
		if (path.isEmpty()) {
			return {};
		}
		return QString("file:%1").arg(directory_key(path));
	}

	url = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
	return url.toString(QUrl::FullyEncoded);
}

void append_unique_sidebar_url(QList<QUrl>* out, QSet<QString>* seen, const QUrl& url_in) {
	if (!out || !seen) {
		return;
	}

	QUrl url = url_in;
	if (url.scheme().isEmpty() && !url.toString().isEmpty()) {
		url = QUrl::fromLocalFile(url.toString());
	}

	const QString key = sidebar_url_key(url);
	if (key.isEmpty() || seen->contains(key)) {
		return;
	}

	seen->insert(key);
	if (url.isLocalFile() || url.scheme().compare("file", Qt::CaseInsensitive) == 0) {
		const QString path = normalize_directory_path(url.toLocalFile());
		if (!path.isEmpty()) {
			out->push_back(QUrl::fromLocalFile(path));
		}
		return;
	}
	out->push_back(url);
}

QList<QUrl> default_sidebar_urls() {
	QList<QUrl> urls;
	QSet<QString> seen;

	// Keep defaults path-only and avoid probing mounted volumes here.
	// Synchronous volume queries can stall UI on unreachable network drives.
	const QList<QStandardPaths::StandardLocation> locations = {
		QStandardPaths::HomeLocation,
		QStandardPaths::DesktopLocation,
		QStandardPaths::DocumentsLocation,
		QStandardPaths::DownloadLocation,
		QStandardPaths::PicturesLocation,
		QStandardPaths::MoviesLocation,
		QStandardPaths::MusicLocation,
	};

	for (const QStandardPaths::StandardLocation location : locations) {
		for (const QString& path : QStandardPaths::standardLocations(location)) {
			append_unique_sidebar_url(&urls, &seen, QUrl::fromLocalFile(path));
		}
	}

	append_unique_sidebar_url(&urls, &seen, QUrl::fromLocalFile(QDir::rootPath()));
	return urls;
}

QStringList normalize_history(const QStringList& history) {
	QStringList out;
	QSet<QString> seen;
	for (const QString& value : history) {
		const QString directory = normalize_directory_path(value);
		if (directory.isEmpty()) {
			continue;
		}

		const QString key = directory_key(directory);
		if (seen.contains(key)) {
			continue;
		}
		seen.insert(key);
		out.push_back(directory);
		if (out.size() >= kMaxHistoryEntries) {
			break;
		}
	}
	return out;
}

QString selected_directory_from_dialog(const QFileDialog* dialog) {
	if (!dialog) {
		return {};
	}

	const QStringList selected_files = dialog->selectedFiles();
	if (selected_files.isEmpty()) {
		return {};
	}

	const QString first = normalize_directory_path(selected_files.first());
	if (first.isEmpty()) {
		return {};
	}

	if (dialog->fileMode() == QFileDialog::Directory) {
		return first;
	}

	return normalize_directory_path(QFileInfo(first).absolutePath());
}

QString resolve_initial_directory(const QString& preferred, const QString& fallback) {
	QString dir = normalize_directory_path(preferred);
	if (!dir.isEmpty()) {
		return dir;
	}

	dir = normalize_directory_path(fallback);
	if (!dir.isEmpty()) {
		return dir;
	}

	return QDir::homePath();
}

void apply_modern_dialog_defaults(QFileDialog* dialog) {
	if (!dialog) {
		return;
	}

	// Native dialogs expose platform-standard breadcrumbs/bookmarks.
	const bool force_qt_dialog = env_var_is_truthy("PAKFU_FORCE_QT_FILE_DIALOG");
	dialog->setOption(QFileDialog::DontUseNativeDialog, force_qt_dialog);
	dialog->setOption(QFileDialog::HideNameFilterDetails, false);
	dialog->setOption(QFileDialog::ReadOnly, false);
	dialog->setViewMode(QFileDialog::Detail);
}

void restore_dialog_state(QFileDialog* dialog,
						  const QString& key_prefix,
						  const QString& fallback_directory,
						  const QString& initial_selection) {
	if (!dialog) {
		return;
	}

	QSettings settings;
	const bool qt_dialog = uses_qt_dialog(dialog);
	if (qt_dialog) {
		const QByteArray state = settings.value(key_prefix + "/state").toByteArray();
		if (!state.isEmpty()) {
			dialog->restoreState(state);
		}

		const QStringList history = normalize_history(settings.value(key_prefix + "/history").toStringList());
		if (!history.isEmpty()) {
			dialog->setHistory(history);
		}
	}

	if (qt_dialog) {
		const QString stored_directory = settings.value(key_prefix + "/lastDirectory").toString();
		dialog->setDirectory(resolve_initial_directory(stored_directory, fallback_directory));
	} else {
		dialog->setDirectory(resolve_initial_directory(fallback_directory, QString()));
	}

	if (qt_dialog) {
		dialog->setSidebarUrls(default_sidebar_urls());
	}

	if (!initial_selection.trimmed().isEmpty()) {
		dialog->selectFile(initial_selection);
	}

	const QString saved_filter = settings.value(key_prefix + "/selectedNameFilter").toString();
	if (!saved_filter.isEmpty() && dialog->nameFilters().contains(saved_filter)) {
		dialog->selectNameFilter(saved_filter);
	}
}

void persist_dialog_state(QFileDialog* dialog, const QString& key_prefix) {
	if (!dialog) {
		return;
	}

	const bool qt_dialog = uses_qt_dialog(dialog);
	QSettings settings;
	if (qt_dialog) {
		settings.setValue(key_prefix + "/state", dialog->saveState());

		const QStringList history = normalize_history(dialog->history());
		if (!history.isEmpty()) {
			settings.setValue(key_prefix + "/history", history);
		} else {
			settings.remove(key_prefix + "/history");
		}
	}
	settings.remove(key_prefix + "/sidebar");

	const QString from_selection = selected_directory_from_dialog(dialog);
	const QString from_current_directory = normalize_directory_path(dialog->directory().absolutePath());
	const QString last_directory = !from_selection.isEmpty() ? from_selection : from_current_directory;
	if (!last_directory.isEmpty()) {
		settings.setValue(key_prefix + "/lastDirectory", last_directory);
	}

	const QString selected_filter = dialog->selectedNameFilter();
	if (!selected_filter.isEmpty()) {
		settings.setValue(key_prefix + "/selectedNameFilter", selected_filter);
	}
}
}  // namespace

namespace FileDialogUtils {

bool exec_with_state(QFileDialog* dialog,
					 const Options& options,
					 QStringList* selected_files,
					 QString* selected_name_filter) {
	if (selected_files) {
		selected_files->clear();
	}
	if (selected_name_filter) {
		selected_name_filter->clear();
	}
	if (!dialog || !selected_files) {
		return false;
	}

	const QString key_prefix = settings_prefix(options.settings_key);
	apply_modern_dialog_defaults(dialog);
	restore_dialog_state(dialog, key_prefix, options.fallback_directory, options.initial_selection);

	const bool accepted = dialog->exec() == QDialog::Accepted;
	persist_dialog_state(dialog, key_prefix);

	if (!accepted) {
		return false;
	}

	*selected_files = dialog->selectedFiles();
	if (selected_name_filter) {
		*selected_name_filter = dialog->selectedNameFilter();
	}
	return !selected_files->isEmpty();
}

}  // namespace FileDialogUtils
