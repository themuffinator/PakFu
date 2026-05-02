#include "ui/about_dialog.h"

#include <QDesktopServices>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "pakfu_config.h"

namespace {
class SplashPreviewWidget final : public QWidget {
public:
	explicit SplashPreviewWidget(const QPixmap& logo,
	                             const QString& version,
	                             const QString& build_date,
	                             QWidget* parent = nullptr)
		: QWidget(parent), logo_(logo), version_(version), build_date_(build_date) {
		setMinimumSize(300, 320);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	}

protected:
	void paintEvent(QPaintEvent* event) override {
		Q_UNUSED(event);

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

		const QRectF frame_rect = rect().adjusted(1, 1, -1, -1);
		const qreal radius = 14.0;
		QPainterPath clip;
		clip.addRoundedRect(frame_rect, radius, radius);

		painter.fillPath(clip, QColor(16, 16, 16));
		if (!logo_.isNull()) {
			const QPixmap scaled = logo_.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			const QPoint top_left((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
			painter.save();
			painter.setClipPath(clip);
			painter.drawPixmap(top_left, scaled);
			painter.restore();
		}

		painter.setPen(QPen(QColor(160, 160, 160, 180), 1.0));
		painter.drawRoundedRect(frame_rect, radius, radius);

		const int box_height = qMax(34, qMin(44, height() / 11));
		const QRect info_rect(12, height() - box_height - 8, width() - 24, box_height);
		painter.setPen(QPen(QColor(160, 160, 160, 180), 1.0));
		painter.setBrush(QColor(0, 0, 0, 235));
		painter.drawRoundedRect(info_rect, 10, 10);

		QFont date_font = font();
		date_font.setPointSize(qMax(8, date_font.pointSize() - 1));
		painter.setFont(date_font);
		painter.setPen(QColor(220, 220, 220, 210));
		const QRect text_rect = info_rect.adjusted(12, 6, -12, -6);
		painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, build_date_);

		QFont version_font = date_font;
		version_font.setWeight(QFont::DemiBold);
		painter.setFont(version_font);
		painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignRight, version_);
	}

private:
	QPixmap logo_;
	QString version_;
	QString build_date_;
};

QLabel* make_body_label(const QString& text, QWidget* parent) {
	auto* label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setTextFormat(Qt::PlainText);
	return label;
}
}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
	build_ui();
}

QPixmap AboutDialog::load_logo_pixmap() const {
	QPixmap pixmap(":/assets/img/logo.png");
	if (pixmap.isNull()) {
		pixmap.load("assets/img/logo.png");
	}
	return pixmap;
}

void AboutDialog::build_ui() {
	setModal(true);
	setWindowTitle(tr("About PakFu"));
	setFixedSize(460, 700);

	QString repo = QString::fromUtf8(PAKFU_GITHUB_REPO).trimmed();
	if (!repo.contains('/') || repo.startsWith('/') || repo.endsWith('/')) {
		repo = "themuffinator/PakFu";
	}
	const QString owner = repo.section('/', 0, 0).trimmed();
	const QString repo_url = QString("https://github.com/%1").arg(repo);
	const QString credits_url = QString("https://github.com/%1/blob/main/docs/CREDITS.md").arg(repo);
	const QString sponsors_url = QString("https://github.com/sponsors/%1").arg(owner);
	const QString version_label = QString("v%1").arg(PAKFU_VERSION);
	const QString build_date_label = QString::fromLatin1(__DATE__);

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(14, 14, 14, 14);
	root->setSpacing(10);

	auto* splash = new SplashPreviewWidget(load_logo_pixmap(), version_label, build_date_label, this);
	splash->setMinimumWidth(340);
	splash->setMaximumWidth(520);
	root->addWidget(splash, 1);

	auto* actions = new QWidget(this);
	auto* actions_layout = new QGridLayout(actions);
	actions_layout->setContentsMargins(0, 0, 0, 0);
	actions_layout->setHorizontalSpacing(8);
	actions_layout->setVerticalSpacing(8);
	actions_layout->setColumnStretch(0, 1);
	actions_layout->setColumnStretch(1, 1);

	auto* source_button = new QPushButton(tr("Open Source"), actions);
	auto* credits_button = new QPushButton(tr("View Credits"), actions);
	auto* sponsor_button = new QPushButton(tr("Sponsor on GitHub"), actions);
	auto* close_button = new QPushButton(tr("Close"), actions);

	source_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	credits_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	sponsor_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	close_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	close_button->setDefault(true);
	close_button->setAutoDefault(true);

	actions_layout->addWidget(source_button, 0, 0);
	actions_layout->addWidget(credits_button, 0, 1);
	actions_layout->addWidget(sponsor_button, 1, 0);
	actions_layout->addWidget(close_button, 1, 1);
	root->addWidget(actions);

	auto* legal_group = new QGroupBox(tr("Legal Disclaimer"), this);
	auto* legal_layout = new QVBoxLayout(legal_group);
	legal_layout->setContentsMargins(12, 10, 12, 12);
	legal_layout->setSpacing(8);
	auto* disclaimer_text = make_body_label(tr("Use of this software is at your own risk. No warranty is provided."),
	                                        legal_group);
	auto* license_text = make_body_label(tr("GNU General Public License v3 (GPLv3). See the LICENSE file."), legal_group);
	legal_layout->addWidget(disclaimer_text);
	legal_layout->addWidget(license_text);
	root->addWidget(legal_group, 0);

	connect(close_button, &QPushButton::clicked, this, &QDialog::reject);
	connect(source_button, &QPushButton::clicked, this, [repo_url]() {
		QDesktopServices::openUrl(QUrl(repo_url));
	});
	connect(credits_button, &QPushButton::clicked, this, [credits_url]() {
		QDesktopServices::openUrl(QUrl(credits_url));
	});
	connect(sponsor_button, &QPushButton::clicked, this, [sponsors_url]() {
		QDesktopServices::openUrl(QUrl(sponsors_url));
	});
}
