#pragma once

#include <QString>
#include <QStringList>

class QFileDialog;

namespace FileDialogUtils {

struct Options {
	QString settings_key;
	QString fallback_directory;
	QString initial_selection;
};

bool exec_with_state(QFileDialog* dialog,
					 const Options& options,
					 QStringList* selected_files,
					 QString* selected_name_filter = nullptr);

}  // namespace FileDialogUtils
