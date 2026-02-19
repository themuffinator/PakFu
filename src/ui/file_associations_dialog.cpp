#include "ui/file_associations_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include "platform/file_associations.h"
#include "ui/ui_icons.h"

namespace {
QString short_status_text(const QString& extension, const QString& details) {
  const QString prefix = QString(".%1:").arg(extension.toLower());
  if (details.startsWith(prefix, Qt::CaseInsensitive)) {
    return details.mid(prefix.size()).trimmed();
  }
  return details;
}
}  // namespace

FileAssociationsDialog::FileAssociationsDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle("File Associations");
	setMinimumWidth(760);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(18, 16, 18, 16);
	layout->setSpacing(10);

	auto* title = new QLabel("Manage archive, image, video, audio, and model file associations by format.", this);
	QFont title_font = title->font();
	title_font.setBold(true);
	title_font.setPointSize(title_font.pointSize() + 1);
	title->setFont(title_font);
	layout->addWidget(title);

	auto* help = new QLabel(
		"Use tabs to configure archives, images, videos, audio, and models independently. On Windows, you may still need to confirm defaults in Settings -> Default apps.",
		this);
	help->setWordWrap(true);
	layout->addWidget(help);

	tabs_ = new QTabWidget(this);
	layout->addWidget(tabs_, 1);

	rows_.reserve(FileAssociations::managed_extensions().size());

	auto populate_tab = [this](QWidget* tab, int tab_index, const QStringList& extensions) {
		auto* tab_layout = new QVBoxLayout(tab);
		tab_layout->setContentsMargins(0, 0, 0, 0);
		tab_layout->setSpacing(0);

		auto* scroll = new QScrollArea(tab);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		tab_layout->addWidget(scroll, 1);

		auto* body = new QWidget(scroll);
		auto* body_layout = new QVBoxLayout(body);
		body_layout->setContentsMargins(0, 0, 0, 0);
		body_layout->setSpacing(6);

		for (const QString& ext : extensions) {
			auto* row = new QWidget(body);
			auto* row_layout = new QHBoxLayout(row);
			row_layout->setContentsMargins(4, 4, 4, 4);
			row_layout->setSpacing(10);

			auto* icon = new QLabel(row);
			const QIcon ext_icon = FileAssociations::icon_for_extension(ext, QSize(20, 20));
			icon->setPixmap(ext_icon.pixmap(20, 20));
			icon->setFixedSize(24, 24);
			row_layout->addWidget(icon);

			auto* enabled = new QCheckBox(QString(".%1").arg(ext), row);
			enabled->setMinimumWidth(90);
			row_layout->addWidget(enabled);

			auto* status = new QLabel("...", row);
			status->setWordWrap(true);
			status->setTextInteractionFlags(Qt::TextSelectableByMouse);
			row_layout->addWidget(status, 1);

			body_layout->addWidget(row);
			rows_.push_back(Row{ext, tab_index, enabled, status});
		}

		body_layout->addStretch(1);
		scroll->setWidget(body);
	};

	auto* archives_tab = new QWidget(tabs_);
	const int archives_tab_index = tabs_->addTab(archives_tab, "Archives");
	populate_tab(archives_tab, archives_tab_index, FileAssociations::managed_archive_extensions());

	auto* images_tab = new QWidget(tabs_);
	const int images_tab_index = tabs_->addTab(images_tab, "Images");
	populate_tab(images_tab, images_tab_index, FileAssociations::managed_image_extensions());

	auto* videos_tab = new QWidget(tabs_);
	const int videos_tab_index = tabs_->addTab(videos_tab, "Videos");
	populate_tab(videos_tab, videos_tab_index, FileAssociations::managed_video_extensions());

	auto* audio_tab = new QWidget(tabs_);
	const int audio_tab_index = tabs_->addTab(audio_tab, "Audio");
	populate_tab(audio_tab, audio_tab_index, FileAssociations::managed_audio_extensions());

	auto* models_tab = new QWidget(tabs_);
	const int models_tab_index = tabs_->addTab(models_tab, "Models");
	populate_tab(models_tab, models_tab_index, FileAssociations::managed_model_extensions());

	summary_label_ = new QLabel(this);
	summary_label_->setWordWrap(true);
	layout->addWidget(summary_label_);

	auto* actions_row = new QHBoxLayout();
	auto* select_tab = new QPushButton("Select Tab", this);
	auto* clear_tab = new QPushButton("Clear Tab", this);
	auto* refresh = new QPushButton("Refresh", this);
	auto* apply = new QPushButton("Apply", this);
	auto* open_defaults = new QPushButton("Open Default Apps", this);
	select_tab->setIcon(UiIcons::icon(UiIcons::Id::AddFiles, select_tab->style()));
	clear_tab->setIcon(UiIcons::icon(UiIcons::Id::DeleteItem, clear_tab->style()));
	refresh->setIcon(UiIcons::icon(UiIcons::Id::CheckUpdates, refresh->style()));
	apply->setIcon(UiIcons::icon(UiIcons::Id::Associate, apply->style()));
	open_defaults->setIcon(UiIcons::icon(UiIcons::Id::Configure, open_defaults->style()));
	actions_row->addWidget(select_tab);
	actions_row->addWidget(clear_tab);
	actions_row->addWidget(refresh);
	actions_row->addStretch(1);
	actions_row->addWidget(open_defaults);
	actions_row->addWidget(apply);
	layout->addLayout(actions_row);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	connect(select_tab, &QPushButton::clicked, this, [this]() {
		const int tab = tabs_ ? tabs_->currentIndex() : -1;
		for (const Row& row : rows_) {
			if (row.tab_index == tab && row.enabled && row.enabled->isEnabled()) {
				row.enabled->setChecked(true);
			}
		}
	});
	connect(clear_tab, &QPushButton::clicked, this, [this]() {
		const int tab = tabs_ ? tabs_->currentIndex() : -1;
		for (const Row& row : rows_) {
			if (row.tab_index == tab && row.enabled && row.enabled->isEnabled()) {
				row.enabled->setChecked(false);
			}
		}
	});
	connect(refresh, &QPushButton::clicked, this, &FileAssociationsDialog::refresh_status);
	connect(apply, &QPushButton::clicked, this, &FileAssociationsDialog::apply_changes);
	connect(open_defaults, &QPushButton::clicked, this, []() { FileAssociations::open_default_apps_settings(); });

#if !defined(Q_OS_WIN)
	for (const Row& row : rows_) {
		if (row.enabled) {
			row.enabled->setEnabled(false);
		}
	}
	select_tab->setEnabled(false);
	clear_tab->setEnabled(false);
	apply->setEnabled(false);
	open_defaults->setEnabled(false);
#endif

	refresh_status();
}

void FileAssociationsDialog::refresh_status() {
	int registered_count = 0;
	int archive_registered = 0;
	int image_registered = 0;
	int video_registered = 0;
	int audio_registered = 0;
	int model_registered = 0;
	const int archive_total = FileAssociations::managed_archive_extensions().size();
	const int image_total = FileAssociations::managed_image_extensions().size();
	const int video_total = FileAssociations::managed_video_extensions().size();
	const int audio_total = FileAssociations::managed_audio_extensions().size();
	const int model_total = FileAssociations::managed_model_extensions().size();

	for (const Row& row : rows_) {
		QString details;
		const bool ok = FileAssociations::is_extension_registered(row.extension, &details);
		if (ok) {
			++registered_count;
			if (FileAssociations::is_archive_extension(row.extension)) {
				++archive_registered;
			} else if (FileAssociations::is_image_extension(row.extension)) {
				++image_registered;
			} else if (FileAssociations::is_video_extension(row.extension)) {
				++video_registered;
			} else if (FileAssociations::is_audio_extension(row.extension)) {
				++audio_registered;
			} else if (FileAssociations::is_model_extension(row.extension)) {
				++model_registered;
			}
		}
		if (row.enabled) {
#if defined(Q_OS_WIN)
			row.enabled->setChecked(ok);
#endif
		}
		if (row.status) {
			row.status->setText(short_status_text(row.extension, details));
			row.status->setToolTip(details);
		}
	}

	if (summary_label_) {
#if defined(Q_OS_WIN)
		summary_label_->setText(QString("Registered %1 of %2 managed formats. Archives: %3/%4. Images: %5/%6. Videos: %7/%8. Audio: %9/%10. Models: %11/%12.")
		                        .arg(registered_count)
		                        .arg(rows_.size())
		                        .arg(archive_registered)
		                        .arg(archive_total)
		                        .arg(image_registered)
		                        .arg(image_total)
		                        .arg(video_registered)
		                        .arg(video_total)
		                        .arg(audio_registered)
		                        .arg(audio_total)
		                        .arg(model_registered)
		                        .arg(model_total));
#else
		summary_label_->setText("Associations are installer-managed on this platform.");
#endif
	}
}

void FileAssociationsDialog::apply_changes() {
#if !defined(Q_OS_WIN)
  QMessageBox::information(this, "File Associations", "Associations are installer-managed on this platform.");
  return;
#else
  QStringList errors;
  QStringList warnings;
  for (const Row& row : rows_) {
    if (!row.enabled) {
      continue;
    }
    QString message;
    if (!FileAssociations::set_extension_registration(row.extension, row.enabled->isChecked(), &message)) {
      errors.push_back(message.isEmpty() ? QString("Failed to update .%1").arg(row.extension) : message);
      continue;
    }
    if (!message.isEmpty()) {
      warnings.push_back(message);
    }
  }

  refresh_status();

  if (!errors.isEmpty()) {
    QMessageBox::warning(this, "File Associations", errors.join('\n'));
    return;
  }

  QString text = "File associations were updated.";
  if (!warnings.isEmpty()) {
    text += "\n\nNotes:\n" + warnings.join('\n');
  }
  const auto choice = QMessageBox::question(
    this,
    "File Associations",
    text + "\n\nOpen Settings -> Default apps now?");
  if (choice == QMessageBox::Yes) {
    FileAssociations::open_default_apps_settings();
  }
#endif
}
