#include "file_associations.h"

#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace {
enum class AssociationCategory {
	Archive = 0,
	Image,
	Video,
	Audio,
	Model,
};

struct AssociationSpec {
	QString extension;
	QString friendly_name;
	QColor color;
	AssociationCategory category = AssociationCategory::Archive;
};

QString quoted(const QString& s) {
	QString out = s;
	out.replace('"', "\\\"");
	return '"' + out + '"';
}

QString dotted_extension(const QString& ext) {
	return QString(".%1").arg(ext);
}

QString prog_id_for(const QString& ext) {
	return QString("PakFu.%1").arg(ext);
}

const QVector<AssociationSpec>& association_specs() {
	static const QVector<AssociationSpec> specs = {
	  {"pak", "PAK Archive", QColor("#D35400"), AssociationCategory::Archive},
	  {"sin", "SIN Archive", QColor("#8E6E53"), AssociationCategory::Archive},
	  {"pk3", "PK3 Archive", QColor("#1E88E5"), AssociationCategory::Archive},
	  {"pk4", "PK4 Archive", QColor("#3949AB"), AssociationCategory::Archive},
	  {"pkz", "PKZ Archive", QColor("#00897B"), AssociationCategory::Archive},
	  {"zip", "ZIP Archive", QColor("#43A047"), AssociationCategory::Archive},
	  {"resources", "Resources Archive", QColor("#6D4C41"), AssociationCategory::Archive},
	  {"wad", "WAD Archive", QColor("#8E24AA"), AssociationCategory::Archive},
	  {"wad2", "WAD2 Archive", QColor("#F4511E"), AssociationCategory::Archive},
	  {"wad3", "WAD3 Archive", QColor("#00838F"), AssociationCategory::Archive},
	  {"pcx", "PCX Image", QColor("#546E7A"), AssociationCategory::Image},
	  {"wal", "WAL Image", QColor("#1565C0"), AssociationCategory::Image},
	  {"swl", "SWL Image", QColor("#2E7D32"), AssociationCategory::Image},
	  {"mip", "MIP Image", QColor("#6A1B9A"), AssociationCategory::Image},
	  {"lmp", "LMP Image", QColor("#5D4037"), AssociationCategory::Image},
	  {"dds", "DDS Image", QColor("#0277BD"), AssociationCategory::Image},
	  {"png", "PNG Image", QColor("#00ACC1"), AssociationCategory::Image},
	  {"jpg", "JPG Image", QColor("#F9A825"), AssociationCategory::Image},
	  {"jpeg", "JPEG Image", QColor("#F57F17"), AssociationCategory::Image},
	  {"tga", "TGA Image", QColor("#7CB342"), AssociationCategory::Image},
	  {"bmp", "BMP Image", QColor("#8D6E63"), AssociationCategory::Image},
	  {"gif", "GIF Image", QColor("#EC407A"), AssociationCategory::Image},
	  {"tif", "TIF Image", QColor("#455A64"), AssociationCategory::Image},
	  {"tiff", "TIFF Image", QColor("#37474F"), AssociationCategory::Image},
	  {"cin", "CIN Video", QColor("#5E35B1"), AssociationCategory::Video},
	  {"roq", "ROQ Video", QColor("#3949AB"), AssociationCategory::Video},
	  {"ogv", "OGV Video", QColor("#039BE5"), AssociationCategory::Video},
	  {"bik", "BIK Video", QColor("#00897B"), AssociationCategory::Video},
	  {"mp4", "MP4 Video", QColor("#1E88E5"), AssociationCategory::Video},
	  {"mkv", "MKV Video", QColor("#7CB342"), AssociationCategory::Video},
	  {"avi", "AVI Video", QColor("#6D4C41"), AssociationCategory::Video},
	  {"webm", "WEBM Video", QColor("#00ACC1"), AssociationCategory::Video},
	  {"wav", "WAV Audio", QColor("#43A047"), AssociationCategory::Audio},
	  {"idwav", "IDWAV Audio", QColor("#66BB6A"), AssociationCategory::Audio},
	  {"ogg", "OGG Audio", QColor("#26A69A"), AssociationCategory::Audio},
	  {"mp3", "MP3 Audio", QColor("#F9A825"), AssociationCategory::Audio},
	  {"mdl", "MDL Model", QColor("#8D6E63"), AssociationCategory::Model},
	  {"md2", "MD2 Model", QColor("#5C6BC0"), AssociationCategory::Model},
	  {"md3", "MD3 Model", QColor("#3949AB"), AssociationCategory::Model},
	  {"mdc", "MDC Model", QColor("#283593"), AssociationCategory::Model},
	  {"md4", "MD4 Model", QColor("#1A237E"), AssociationCategory::Model},
	  {"mdr", "MDR Model", QColor("#4E342E"), AssociationCategory::Model},
	  {"skb", "SKB Model", QColor("#6D4C41"), AssociationCategory::Model},
	  {"skd", "SKD Model", QColor("#795548"), AssociationCategory::Model},
	  {"mdm", "MDM Model", QColor("#455A64"), AssociationCategory::Model},
	  {"glm", "GLM Model", QColor("#546E7A"), AssociationCategory::Model},
	  {"iqm", "IQM Model", QColor("#0277BD"), AssociationCategory::Model},
	  {"md5mesh", "MD5MESH Model", QColor("#00838F"), AssociationCategory::Model},
	  {"lwo", "LWO Model", QColor("#7CB342"), AssociationCategory::Model},
	  {"obj", "OBJ Model", QColor("#F57C00"), AssociationCategory::Model},
	};
	return specs;
}

const AssociationSpec* spec_for_extension(QString ext) {
	ext = ext.trimmed().toLower();
	if (ext.startsWith('.')) {
		ext.remove(0, 1);
	}
	if (ext.isEmpty()) {
		return nullptr;
	}
	for (const AssociationSpec& spec : association_specs()) {
		if (spec.extension == ext) {
			return &spec;
		}
	}
	return nullptr;
}

QStringList managed_extensions_for_category(AssociationCategory category) {
	QStringList out;
	for (const AssociationSpec& spec : association_specs()) {
		if (spec.category == category) {
			out.push_back(spec.extension);
		}
	}
	return out;
}

QImage make_association_icon(const QString& ext, const QColor& base_color) {
	constexpr int kSize = 256;
	QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF card(18.0, 10.0, 220.0, 236.0);
	QLinearGradient gradient(card.topLeft(), card.bottomLeft());
	gradient.setColorAt(0.0, base_color.lighter(130));
	gradient.setColorAt(1.0, base_color.darker(120));

	painter.setPen(QPen(QColor(0, 0, 0, 70), 4.0));
	painter.setBrush(gradient);
	painter.drawRoundedRect(card, 26.0, 26.0);

	QPainterPath fold;
	fold.moveTo(card.right() - 58.0, card.top());
	fold.lineTo(card.right(), card.top());
	fold.lineTo(card.right(), card.top() + 58.0);
	fold.closeSubpath();
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, 185));
	painter.drawPath(fold);

	painter.setBrush(QColor(255, 255, 255, 38));
	painter.drawRoundedRect(QRectF(card.left() + 12.0, card.top() + 16.0, card.width() - 24.0, 74.0), 16.0, 16.0);

	const QString label = ext.toUpper();
	const QRect text_rect = QRect(28, 146, 200, 76);

	QFont font;
	font.setFamily("Sans Serif");
	font.setBold(true);
	int pixel_size = 92;
	for (; pixel_size >= 24; pixel_size -= 2) {
		font.setPixelSize(pixel_size);
		const QFontMetrics fm(font);
		if (fm.horizontalAdvance(label) <= text_rect.width() - 8) {
			break;
		}
	}
	font.setPixelSize(qMax(pixel_size, 24));
	painter.setFont(font);

	painter.setPen(QColor(0, 0, 0, 120));
	painter.drawText(text_rect.translated(0, 3), Qt::AlignCenter, label);
	painter.setPen(Qt::white);
	painter.drawText(text_rect, Qt::AlignCenter, label);

	return image;
}

QHash<QString, QString> ensure_extension_icon_files(QString* warning) {
	QHash<QString, QString> out;
	const QString data_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (data_root.isEmpty()) {
		if (warning) {
			*warning = "Unable to resolve a writable icon cache directory; using the app icon as fallback.";
		}
		return out;
	}

	const QString icon_dir = QDir(data_root).filePath("file-association-icons");
	if (!QDir().mkpath(icon_dir)) {
		if (warning) {
			*warning = QString("Unable to create icon cache directory: %1").arg(icon_dir);
		}
		return out;
	}

	QStringList failures;
	for (const AssociationSpec& spec : association_specs()) {
		const QImage icon = make_association_icon(spec.extension, spec.color);
		if (icon.isNull()) {
			failures.push_back(spec.extension);
			continue;
		}

		const QString ico_path = QDir(icon_dir).filePath(spec.extension + ".ico");
		const QString png_path = QDir(icon_dir).filePath(spec.extension + ".png");
		const bool wrote_ico = icon.save(ico_path, "ICO");
		(void)icon.save(png_path, "PNG");
		if (!wrote_ico) {
			failures.push_back(spec.extension);
			continue;
		}
		out.insert(spec.extension, ico_path);
	}

	if (!failures.isEmpty() && warning) {
		*warning = QString("Could not generate icons for: %1. Falling back to app icon for those extensions.")
		             .arg(failures.join(", "));
	}
	return out;
}

#if defined(Q_OS_WIN)
bool is_extension_registered_on_windows(const AssociationSpec& spec, const QString& exe_name, QString* details) {
	const QString dot_ext = dotted_extension(spec.extension);
	const QString expected_prog_id = prog_id_for(spec.extension);

	QSettings ext_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(dot_ext), QSettings::NativeFormat);
	const QString prog_id = ext_key.value(".").toString().trimmed();

	QSettings cmd_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\shell\\open\\command").arg(expected_prog_id),
	                  QSettings::NativeFormat);
	const QString open_cmd = cmd_key.value(".").toString().trimmed();

	QSettings icon_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\DefaultIcon").arg(expected_prog_id),
	                   QSettings::NativeFormat);
	const QString icon_value = icon_key.value(".").toString().trimmed();

	const bool ok = (prog_id.compare(expected_prog_id, Qt::CaseInsensitive) == 0) &&
	                open_cmd.contains(exe_name, Qt::CaseInsensitive) &&
	                !icon_value.isEmpty();
	if (details) {
		*details = QString("%1: %2")
		             .arg(dot_ext,
		                  ok ? "registered"
		                     : QString("missing (ProgID=%1, Command=%2, Icon=%3)")
		                         .arg(prog_id.isEmpty() ? "<unset>" : prog_id,
		                              open_cmd.isEmpty() ? "<unset>" : open_cmd,
		                              icon_value.isEmpty() ? "<unset>" : icon_value));
	}
	return ok;
}

bool register_extension_on_windows(const AssociationSpec& spec,
                                   const QString& exe,
                                   const QString& exe_name,
                                   const QString& open_cmd,
                                   const QHash<QString, QString>& icon_paths,
                                   QString* error) {
	Q_UNUSED(exe_name);

	const QString dot_ext = dotted_extension(spec.extension);
	const QString prog_id = prog_id_for(spec.extension);
	const QString icon_path = icon_paths.value(spec.extension, exe);
	const QString icon_ref = QString("%1,0").arg(quoted(QDir::toNativeSeparators(icon_path)));

	QSettings ext_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(dot_ext), QSettings::NativeFormat);
	ext_key.setValue(".", prog_id);

	QSettings open_with_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\OpenWithProgids").arg(dot_ext),
	                        QSettings::NativeFormat);
	open_with_key.setValue(prog_id, QString());

	QSettings prog_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(prog_id), QSettings::NativeFormat);
	prog_key.setValue(".", QString("PakFu %1").arg(spec.friendly_name));

	QSettings icon_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\DefaultIcon").arg(prog_id), QSettings::NativeFormat);
	icon_key.setValue(".", icon_ref);

	QSettings cmd_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\shell\\open\\command").arg(prog_id),
	                  QSettings::NativeFormat);
	cmd_key.setValue(".", open_cmd);

	ext_key.sync();
	open_with_key.sync();
	prog_key.sync();
	icon_key.sync();
	cmd_key.sync();

	if (ext_key.status() != QSettings::NoError || open_with_key.status() != QSettings::NoError ||
	    prog_key.status() != QSettings::NoError || icon_key.status() != QSettings::NoError ||
	    cmd_key.status() != QSettings::NoError) {
		if (error) {
			*error = QString("Failed to register %1 in the current user registry.").arg(dot_ext);
		}
		return false;
	}
	return true;
}

bool unregister_extension_on_windows(const AssociationSpec& spec, const QString& exe_name, QString* error) {
	const QString dot_ext = dotted_extension(spec.extension);
	const QString prog_id = prog_id_for(spec.extension);

	QSettings ext_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(dot_ext), QSettings::NativeFormat);
	const QString current_prog = ext_key.value(".").toString().trimmed();
	if (current_prog.compare(prog_id, Qt::CaseInsensitive) == 0) {
		ext_key.remove(".");
	}

	QSettings open_with_key(QString("HKEY_CURRENT_USER\\Software\\Classes\\%1\\OpenWithProgids").arg(dot_ext),
	                        QSettings::NativeFormat);
	open_with_key.remove(prog_id);

	QSettings app_types(QString("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\%1\\SupportedTypes").arg(exe_name),
	                    QSettings::NativeFormat);
	app_types.remove(dot_ext);

	ext_key.sync();
	open_with_key.sync();
	app_types.sync();

	if (ext_key.status() != QSettings::NoError || open_with_key.status() != QSettings::NoError || app_types.status() != QSettings::NoError) {
		if (error) {
			*error = QString("Failed to update registration for %1.").arg(dot_ext);
		}
		return false;
	}
	return true;
}
#endif
}  // namespace

QStringList FileAssociations::managed_extensions() {
	QStringList out;
	out.reserve(association_specs().size());
	for (const AssociationSpec& spec : association_specs()) {
		out.push_back(spec.extension);
	}
	return out;
}

QStringList FileAssociations::managed_archive_extensions() {
	return managed_extensions_for_category(AssociationCategory::Archive);
}

QStringList FileAssociations::managed_image_extensions() {
	return managed_extensions_for_category(AssociationCategory::Image);
}

QStringList FileAssociations::managed_video_extensions() {
	return managed_extensions_for_category(AssociationCategory::Video);
}

QStringList FileAssociations::managed_audio_extensions() {
	return managed_extensions_for_category(AssociationCategory::Audio);
}

QStringList FileAssociations::managed_model_extensions() {
	return managed_extensions_for_category(AssociationCategory::Model);
}

QString FileAssociations::managed_extension_list() {
	QStringList out;
	out.reserve(association_specs().size());
	for (const AssociationSpec& spec : association_specs()) {
		out.push_back(dotted_extension(spec.extension));
	}
	return out.join(", ");
}

QString FileAssociations::managed_archive_extension_list() {
	QStringList out;
	const QStringList exts = managed_archive_extensions();
	out.reserve(exts.size());
	for (const QString& ext : exts) {
		out.push_back(dotted_extension(ext));
	}
	return out.join(", ");
}

QString FileAssociations::managed_image_extension_list() {
	QStringList out;
	const QStringList exts = managed_image_extensions();
	out.reserve(exts.size());
	for (const QString& ext : exts) {
		out.push_back(dotted_extension(ext));
	}
	return out.join(", ");
}

QString FileAssociations::managed_video_extension_list() {
	QStringList out;
	const QStringList exts = managed_video_extensions();
	out.reserve(exts.size());
	for (const QString& ext : exts) {
		out.push_back(dotted_extension(ext));
	}
	return out.join(", ");
}

QString FileAssociations::managed_audio_extension_list() {
	QStringList out;
	const QStringList exts = managed_audio_extensions();
	out.reserve(exts.size());
	for (const QString& ext : exts) {
		out.push_back(dotted_extension(ext));
	}
	return out.join(", ");
}

QString FileAssociations::managed_model_extension_list() {
	QStringList out;
	const QStringList exts = managed_model_extensions();
	out.reserve(exts.size());
	for (const QString& ext : exts) {
		out.push_back(dotted_extension(ext));
	}
	return out.join(", ");
}

bool FileAssociations::is_archive_extension(const QString& extension) {
	const AssociationSpec* spec = spec_for_extension(extension);
	return spec && spec->category == AssociationCategory::Archive;
}

bool FileAssociations::is_image_extension(const QString& extension) {
	const AssociationSpec* spec = spec_for_extension(extension);
	return spec && spec->category == AssociationCategory::Image;
}

bool FileAssociations::is_video_extension(const QString& extension) {
	const AssociationSpec* spec = spec_for_extension(extension);
	return spec && spec->category == AssociationCategory::Video;
}

bool FileAssociations::is_audio_extension(const QString& extension) {
	const AssociationSpec* spec = spec_for_extension(extension);
	return spec && spec->category == AssociationCategory::Audio;
}

bool FileAssociations::is_model_extension(const QString& extension) {
	const AssociationSpec* spec = spec_for_extension(extension);
	return spec && spec->category == AssociationCategory::Model;
}

QIcon FileAssociations::icon_for_extension(const QString& extension, const QSize& icon_size) {
	const AssociationSpec* spec = spec_for_extension(extension);
	if (!spec) {
		return {};
	}
	const QSize out_size = icon_size.isValid() ? icon_size : QSize(32, 32);
	const QImage source = make_association_icon(spec->extension, spec->color);
	if (source.isNull()) {
		return {};
	}
	const QImage scaled = source.scaled(out_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	return QIcon(QPixmap::fromImage(scaled));
}

bool FileAssociations::is_extension_registered(const QString& extension, QString* details) {
	if (details) {
		details->clear();
	}
	const AssociationSpec* spec = spec_for_extension(extension);
	if (!spec) {
		if (details) {
			*details = QString("Unsupported managed extension: %1").arg(extension);
		}
		return false;
	}

#if defined(Q_OS_WIN)
	const QString exe_name = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
	return is_extension_registered_on_windows(*spec, exe_name, details);
#else
	if (details) {
		*details = QString("%1: installer-managed on this platform.").arg(dotted_extension(spec->extension));
	}
	return false;
#endif
}

bool FileAssociations::set_extension_registration(const QString& extension, bool enabled, QString* error) {
	if (error) {
		error->clear();
	}

	const AssociationSpec* spec = spec_for_extension(extension);
	if (!spec) {
		if (error) {
			*error = QString("Unsupported managed extension: %1").arg(extension);
		}
		return false;
	}

#if defined(Q_OS_WIN)
	const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
	if (exe.isEmpty()) {
		if (error) {
			*error = "Unable to determine application path.";
		}
		return false;
	}

	const QString exe_name = QFileInfo(exe).fileName();
	const QString open_cmd = QString("%1 \"%2\"").arg(quoted(exe), "%1");

	QString icon_warning;
	const QHash<QString, QString> icon_paths = ensure_extension_icon_files(&icon_warning);

	bool ok = true;
	QString op_error;
	if (enabled) {
		ok = register_extension_on_windows(*spec, exe, exe_name, open_cmd, icon_paths, &op_error);
		if (ok) {
			QSettings app_root(QString("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\%1").arg(exe_name),
			                   QSettings::NativeFormat);
			app_root.setValue("FriendlyAppName", "PakFu");

			QSettings app_cmd(QString("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\%1\\shell\\open\\command").arg(exe_name),
			                  QSettings::NativeFormat);
			app_cmd.setValue(".", open_cmd);

			QSettings app_types(QString("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\%1\\SupportedTypes").arg(exe_name),
			                    QSettings::NativeFormat);
			app_types.setValue(dotted_extension(spec->extension), QString());

			app_root.sync();
			app_cmd.sync();
			app_types.sync();
		}
	} else {
		ok = unregister_extension_on_windows(*spec, exe_name, &op_error);
	}

	if (!ok) {
		if (error) {
			*error = op_error.isEmpty() ? QString("Unable to update registration for .%1.").arg(spec->extension) : op_error;
		}
		return false;
	}

	if (!icon_warning.isEmpty() && error) {
		*error = icon_warning;
	}
	return true;
#else
	if (error) {
		*error = "File associations are installer-managed on this platform.";
	}
	return false;
#endif
}

void FileAssociations::open_default_apps_settings() {
#if defined(Q_OS_WIN)
	QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));
#endif
}

bool FileAssociations::is_pak_registered(QString* details) {
	if (details) {
		details->clear();
	}
#if defined(Q_OS_WIN)
	int ok_count = 0;
	QStringList lines;
	lines.reserve(association_specs().size());

	for (const AssociationSpec& spec : association_specs()) {
		QString ext_details;
		const bool ext_ok = is_extension_registered(spec.extension, &ext_details);
		if (ext_ok) {
			++ok_count;
		}
		lines.push_back(ext_details);
	}

	if (details) {
		*details = QString("Registered %1/%2 managed extensions (%3).\n%4")
		             .arg(ok_count)
		             .arg(association_specs().size())
		             .arg(managed_extension_list(), lines.join('\n'));
	}
	return ok_count == association_specs().size();
#else
	if (details) {
		*details = QString("File associations are installer-managed on this platform.\nManaged extensions: %1")
		             .arg(managed_extension_list());
	}
	return false;
#endif
}

bool FileAssociations::apply_pak_registration(QString* error) {
	if (error) {
		error->clear();
	}
#if defined(Q_OS_WIN)
	QStringList warnings;
	for (const AssociationSpec& spec : association_specs()) {
		QString ext_warning;
		if (!set_extension_registration(spec.extension, true, &ext_warning)) {
			if (error) {
				*error = ext_warning.isEmpty()
				           ? QString("Unable to register .%1 file association.").arg(spec.extension)
				           : ext_warning;
			}
			return false;
		}
		if (!ext_warning.isEmpty()) {
			warnings.push_back(ext_warning);
		}
	}

	// Windows 10/11 typically require user confirmation via "Default apps" UI, but this at least
	// registers the ProgIDs and commands so they can be selected.
	open_default_apps_settings();

	if (error && !warnings.isEmpty()) {
		*error = warnings.join('\n');
	}

	return true;
#else
	if (error) {
		*error = "File associations are installer-managed on this platform.";
	}
	return false;
#endif
}
