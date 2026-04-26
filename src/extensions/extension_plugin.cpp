#include "extensions/extension_plugin.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
QString normalize_extension(QString ext) {
	ext = ext.trimmed().toLower();
	while (ext.startsWith('.')) {
		ext.remove(0, 1);
	}
	return ext;
}

QString file_ext_lower(const QString& path) {
	const QString lower = path.toLower();
	const int dot = lower.lastIndexOf('.');
	return dot >= 0 ? lower.mid(dot + 1) : QString();
}

bool is_valid_id(const QString& id) {
	static const QRegularExpression kIdPattern("^[A-Za-z0-9_.-]+$");
	return kIdPattern.match(id).hasMatch();
}

void add_warning(QStringList* warnings, const QString& warning) {
	if (warnings && !warning.trimmed().isEmpty()) {
		warnings->push_back(warning.trimmed());
	}
}

QString absolute_search_dir(const QString& path) {
	if (path.trimmed().isEmpty()) {
		return {};
	}
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString resolve_working_directory(const QString& manifest_path, const QString& working_directory) {
	const QString manifest_dir = QFileInfo(manifest_path).absolutePath();
	if (working_directory.trimmed().isEmpty()) {
		return manifest_dir;
	}
	const QFileInfo info(working_directory);
	if (info.isAbsolute()) {
		return QDir::cleanPath(info.absoluteFilePath());
	}
	return QDir(manifest_dir).absoluteFilePath(working_directory);
}

QString resolve_program_path(const QString& working_directory, const QString& program) {
	if (program.trimmed().isEmpty()) {
		return {};
	}
	const QFileInfo info(program);
	if (info.isAbsolute()) {
		return QDir::cleanPath(info.absoluteFilePath());
	}
	if (program.contains('/') || program.contains('\\') || program.startsWith('.')) {
		return QDir(working_directory).absoluteFilePath(program);
	}
	return program;
}

QStringList parse_command_argv(const QJsonValue& value) {
	QStringList argv;
	if (!value.isArray()) {
		return argv;
	}
	const QJsonArray array = value.toArray();
	argv.reserve(array.size());
	for (const QJsonValue& entry : array) {
		if (!entry.isString()) {
			return {};
		}
		const QString token = entry.toString().trimmed();
		if (token.isEmpty()) {
			return {};
		}
		argv.push_back(token);
	}
	return argv;
}

QStringList parse_extensions(const QJsonValue& value) {
	QStringList out;
	if (value.isUndefined() || value.isNull()) {
		return out;
	}
	if (!value.isArray()) {
		return {};
	}
	const QJsonArray array = value.toArray();
	out.reserve(array.size());
	for (const QJsonValue& entry : array) {
		if (!entry.isString()) {
			return {};
		}
		const QString normalized = normalize_extension(entry.toString());
		if (normalized.isEmpty()) {
			return {};
		}
		out.push_back(normalized);
	}
	out.removeDuplicates();
	return out;
}

bool load_manifest(const QString& manifest_path,
                   QVector<ExtensionCommand>* out,
                   QSet<QString>* seen_refs,
                   QStringList* warnings) {
	QFile file(manifest_path);
	if (!file.open(QIODevice::ReadOnly)) {
		add_warning(warnings, QString("Unable to read extension manifest: %1").arg(manifest_path));
		return true;
	}

	QJsonParseError parse_error{};
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
		add_warning(warnings,
		            QString("Invalid extension manifest JSON: %1 (%2)")
		              .arg(manifest_path, parse_error.errorString()));
		return true;
	}

	const QJsonObject root = doc.object();
	const int schema_version = root.value("schema_version").toInt(1);
	if (schema_version != 1) {
		add_warning(warnings,
		            QString("Unsupported extension manifest schema_version in %1: %2")
		              .arg(manifest_path)
		              .arg(schema_version));
		return true;
	}

	const QString plugin_id = root.value("id").toString().trimmed();
	if (!is_valid_id(plugin_id)) {
		add_warning(warnings, QString("Invalid extension plugin id in %1").arg(manifest_path));
		return true;
	}

	QString plugin_name = root.value("name").toString().trimmed();
	if (plugin_name.isEmpty()) {
		plugin_name = plugin_id;
	}
	const QString plugin_description = root.value("description").toString().trimmed();

	const QJsonArray commands = root.value("commands").toArray();
	if (commands.isEmpty()) {
		add_warning(warnings, QString("Extension manifest has no commands: %1").arg(manifest_path));
		return true;
	}

	for (const QJsonValue& value : commands) {
		if (!value.isObject()) {
			add_warning(warnings, QString("Extension command entry is not an object: %1").arg(manifest_path));
			continue;
		}

		const QJsonObject obj = value.toObject();
		const QString command_id = obj.value("id").toString().trimmed();
		if (!is_valid_id(command_id)) {
			add_warning(warnings, QString("Invalid extension command id in %1").arg(manifest_path));
			continue;
		}

		QString command_name = obj.value("name").toString().trimmed();
		if (command_name.isEmpty()) {
			command_name = command_id;
		}

		const QStringList argv = parse_command_argv(obj.value("command"));
		if (argv.isEmpty()) {
			add_warning(warnings,
			            QString("Extension command %1 in %2 has an invalid command array.")
			              .arg(command_id, manifest_path));
			continue;
		}

		const QStringList extensions = parse_extensions(obj.value("extensions"));
		if (!obj.value("extensions").isUndefined() && extensions.isEmpty()) {
			add_warning(warnings,
			            QString("Extension command %1 in %2 has an invalid extensions list.")
			              .arg(command_id, manifest_path));
			continue;
		}

		ExtensionCommand command;
		command.plugin_id = plugin_id;
		command.plugin_name = plugin_name;
		command.plugin_description = plugin_description;
		command.command_id = command_id;
		command.command_name = command_name;
		command.command_description = obj.value("description").toString().trimmed();
		command.manifest_path = QFileInfo(manifest_path).absoluteFilePath();
		command.working_directory = resolve_working_directory(command.manifest_path, obj.value("working_directory").toString());
		command.argv = argv;
		command.allowed_extensions = extensions;
		command.requires_entries = obj.value("requires_entries").toBool(false);
		command.allow_multiple = obj.value("allow_multiple").toBool(true);

		const QString ref = extension_command_ref(command).toLower();
		if (seen_refs && seen_refs->contains(ref)) {
			add_warning(warnings, QString("Duplicate extension command ref skipped: %1").arg(extension_command_ref(command)));
			continue;
		}
		if (seen_refs) {
			seen_refs->insert(ref);
		}
		if (out) {
			out->push_back(std::move(command));
		}
	}

	return true;
}
}  // namespace

QString extension_command_ref(const ExtensionCommand& command) {
	return QString("%1:%2").arg(command.plugin_id, command.command_id);
}

QString extension_command_display_name(const ExtensionCommand& command) {
	if (command.command_name.isEmpty() ||
	    command.command_name.compare(command.plugin_name, Qt::CaseInsensitive) == 0) {
		return command.plugin_name;
	}
	return QString("%1: %2").arg(command.plugin_name, command.command_name);
}

QStringList default_extension_search_dirs() {
	QStringList out;
	QSet<QString> seen;
	const auto add_dir = [&](const QString& path) {
		const QString absolute = absolute_search_dir(path);
		if (absolute.isEmpty() || seen.contains(absolute.toLower())) {
			return;
		}
		seen.insert(absolute.toLower());
		out.push_back(absolute);
	};

	add_dir(QDir(QCoreApplication::applicationDirPath()).filePath("plugins"));
	add_dir(QDir::current().filePath("plugins"));

	const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (!app_data.isEmpty()) {
		add_dir(QDir(app_data).filePath("plugins"));
	}

	const QString app_local_data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (!app_local_data.isEmpty()) {
		add_dir(QDir(app_local_data).filePath("plugins"));
	}

	return out;
}

bool load_extension_commands(const QStringList& search_dirs,
                             QVector<ExtensionCommand>* out,
                             QStringList* warnings,
                             QString* error) {
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}

	QSet<QString> seen_refs;
	QSet<QString> seen_manifests;
	for (const QString& root_path : search_dirs) {
		const QString absolute_root = absolute_search_dir(root_path);
		if (absolute_root.isEmpty() || !QFileInfo(absolute_root).isDir()) {
			continue;
		}

		QDirIterator it(absolute_root,
		                QStringList{"plugin.json", "*.pakfu-plugin.json"},
		                QDir::Files,
		                QDirIterator::Subdirectories);
		while (it.hasNext()) {
			const QString manifest_path = QFileInfo(it.next()).absoluteFilePath();
			const QString key = manifest_path.toLower();
			if (seen_manifests.contains(key)) {
				continue;
			}
			seen_manifests.insert(key);
			if (!load_manifest(manifest_path, out, &seen_refs, warnings)) {
				if (error && error->isEmpty()) {
					*error = QString("Unable to load extension manifest: %1").arg(manifest_path);
				}
				return false;
			}
		}
	}

	if (out) {
		std::sort(out->begin(), out->end(), [](const ExtensionCommand& a, const ExtensionCommand& b) {
			const int by_plugin = a.plugin_name.compare(b.plugin_name, Qt::CaseInsensitive);
			if (by_plugin != 0) {
				return by_plugin < 0;
			}
			return a.command_name.compare(b.command_name, Qt::CaseInsensitive) < 0;
		});
	}
	return true;
}

const ExtensionCommand* find_extension_command(const QVector<ExtensionCommand>& commands,
                                               const QString& ref,
                                               QString* error) {
	if (error) {
		error->clear();
	}

	const QString trimmed = ref.trimmed();
	if (trimmed.isEmpty()) {
		if (error) {
			*error = "Extension command id is empty.";
		}
		return nullptr;
	}

	const int colon = trimmed.indexOf(':');
	if (colon >= 0) {
		const QString plugin_id = trimmed.left(colon);
		const QString command_id = trimmed.mid(colon + 1);
		for (const ExtensionCommand& command : commands) {
			if (command.plugin_id.compare(plugin_id, Qt::CaseInsensitive) == 0 &&
			    command.command_id.compare(command_id, Qt::CaseInsensitive) == 0) {
				return &command;
			}
		}
		if (error) {
			*error = QString("Extension command not found: %1").arg(trimmed);
		}
		return nullptr;
	}

	const ExtensionCommand* match = nullptr;
	for (const ExtensionCommand& command : commands) {
		if (command.plugin_id.compare(trimmed, Qt::CaseInsensitive) != 0) {
			continue;
		}
		if (match) {
			if (error) {
				*error = QString("Extension plugin \"%1\" has multiple commands. Use plugin:command.").arg(trimmed);
			}
			return nullptr;
		}
		match = &command;
	}

	if (!match && error) {
		*error = QString("Extension plugin not found: %1").arg(trimmed);
	}
	return match;
}

bool extension_command_accepts_entries(const ExtensionCommand& command,
                                       const QVector<ExtensionEntryContext>& entries,
                                       QString* error) {
	if (error) {
		error->clear();
	}

	if (command.requires_entries && entries.isEmpty()) {
		if (error) {
			*error = QString("%1 requires at least one selected entry.").arg(extension_command_display_name(command));
		}
		return false;
	}

	if (!command.allow_multiple && entries.size() > 1) {
		if (error) {
			*error = QString("%1 accepts only one selected entry.").arg(extension_command_display_name(command));
		}
		return false;
	}

	if (!command.allowed_extensions.isEmpty()) {
		if (entries.isEmpty()) {
			if (error) {
				*error = QString("%1 requires a selected file with one of: %2")
				           .arg(extension_command_display_name(command), command.allowed_extensions.join(", "));
			}
			return false;
		}

		for (const ExtensionEntryContext& entry : entries) {
			if (entry.is_dir) {
				if (error) {
					*error = QString("%1 does not accept directory selections.").arg(extension_command_display_name(command));
				}
				return false;
			}
			const QString ext = file_ext_lower(entry.archive_name);
			if (!command.allowed_extensions.contains(ext)) {
				if (error) {
					*error = QString("%1 does not support %2.")
					           .arg(extension_command_display_name(command), entry.archive_name);
				}
				return false;
			}
		}
	}

	return true;
}

bool run_extension_command(const ExtensionCommand& command,
                           const ExtensionRunContext& context,
                           ExtensionRunResult* result,
                           QString* error) {
	if (error) {
		error->clear();
	}
	if (result) {
		*result = ExtensionRunResult{};
	}

	QString selection_error;
	if (!extension_command_accepts_entries(command, context.entries, &selection_error)) {
		if (error) {
			*error = selection_error;
		}
		return false;
	}

	QJsonObject command_object;
	command_object.insert("plugin_id", command.plugin_id);
	command_object.insert("plugin_name", command.plugin_name);
	command_object.insert("command_id", command.command_id);
	command_object.insert("command_name", command.command_name);
	command_object.insert("description", command.command_description);
	command_object.insert("manifest_path", command.manifest_path);

	QJsonObject archive_object;
	archive_object.insert("path", context.archive_path);
	archive_object.insert("readable_path", context.readable_archive_path);
	archive_object.insert("format", context.archive_format);
	archive_object.insert("mounted_entry", context.mounted_entry);
	archive_object.insert("current_prefix", context.current_prefix);
	archive_object.insert("quakelive_encrypted_pk3", context.quakelive_encrypted_pk3);
	archive_object.insert("wad3", context.wad3);
	archive_object.insert("doom_wad", context.doom_wad);

	QJsonArray entries_array;
	for (const ExtensionEntryContext& entry : context.entries) {
		QJsonObject entry_object;
		entry_object.insert("archive_name", entry.archive_name);
		entry_object.insert("local_path", entry.local_path);
		entry_object.insert("is_dir", entry.is_dir);
		entry_object.insert("size", static_cast<qint64>(entry.size));
		entry_object.insert("mtime_utc_secs", entry.mtime_utc_secs);
		entries_array.append(entry_object);
	}

	QJsonObject payload;
	payload.insert("schema", "pakfu-extension/v1");
	payload.insert("command", command_object);
	payload.insert("archive", archive_object);
	payload.insert("entries", entries_array);

	QProcess process;
	process.setProcessChannelMode(QProcess::SeparateChannels);
	process.setWorkingDirectory(command.working_directory);

	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("PAKFU_EXTENSION_SCHEMA", "pakfu-extension/v1");
	env.insert("PAKFU_EXTENSION_PLUGIN_ID", command.plugin_id);
	env.insert("PAKFU_EXTENSION_COMMAND_ID", command.command_id);
	process.setProcessEnvironment(env);

	const QString program = resolve_program_path(command.working_directory, command.argv.value(0));
	process.setProgram(program);
	process.setArguments(command.argv.mid(1));
	process.start();
	if (!process.waitForStarted(10000)) {
		if (error) {
			*error = QString("Unable to start extension command: %1").arg(extension_command_display_name(command));
		}
		return false;
	}

	const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	process.write(json);
	process.closeWriteChannel();

	if (!process.waitForFinished(300000)) {
		process.kill();
		process.waitForFinished();
		if (result) {
			result->std_out = QString::fromUtf8(process.readAllStandardOutput());
			result->std_err = QString::fromUtf8(process.readAllStandardError());
		}
		if (error) {
			*error = QString("Extension command timed out: %1").arg(extension_command_display_name(command));
		}
		return false;
	}

	if (result) {
		result->exit_code = process.exitCode();
		result->std_out = QString::fromUtf8(process.readAllStandardOutput());
		result->std_err = QString::fromUtf8(process.readAllStandardError());
	}

	if (process.exitStatus() != QProcess::NormalExit) {
		if (error) {
			*error = QString("Extension command crashed: %1").arg(extension_command_display_name(command));
		}
		return false;
	}
	if (process.exitCode() != 0) {
		if (error) {
			*error = QString("Extension command failed (%1): %2")
			           .arg(process.exitCode())
			           .arg(extension_command_display_name(command));
		}
		return false;
	}

	return true;
}
