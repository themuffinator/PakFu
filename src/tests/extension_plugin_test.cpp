#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include "extensions/extension_plugin.h"

namespace {
void fail_message(const QString& message) {
	QTextStream(stderr) << message << '\n';
}

void set_error(QString* error, const QString& message) {
	if (error) {
		*error = message;
	}
}

bool write_file(const QString& path, const QByteArray& bytes, QString* error) {
	const QFileInfo info(path);
	if (!info.dir().exists() && !QDir().mkpath(info.dir().absolutePath())) {
		set_error(error, QString("Unable to create directory: %1").arg(info.dir().absolutePath()));
		return false;
	}

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		set_error(error, QString("Unable to write file: %1").arg(path));
		return false;
	}
	if (file.write(bytes) != bytes.size()) {
		set_error(error, QString("Unable to write file bytes: %1").arg(path));
		return false;
	}
	return true;
}

int run_child_mode(QString* error) {
	QFile input;
	if (!input.open(stdin, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)) {
		set_error(error, "Child mode could not open stdin.");
		return 1;
	}

	QJsonParseError parse_error{};
	const QJsonDocument doc = QJsonDocument::fromJson(input.readAll(), &parse_error);
	if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
		set_error(error, "Child mode received invalid JSON payload.");
		return 1;
	}

	const QJsonObject root = doc.object();
	if (root.value("schema").toString() != "pakfu-extension/v1") {
		set_error(error, "Child mode received an unexpected extension schema.");
		return 1;
	}

	const QJsonObject command = root.value("command").toObject();
	const QString plugin_id = command.value("plugin_id").toString();
	const QString command_id = command.value("command_id").toString();
	if (plugin_id != "fixture") {
		set_error(error, "Child mode received the wrong plugin id.");
		return 1;
	}
	if (qEnvironmentVariable("PAKFU_EXTENSION_PLUGIN_ID") != plugin_id ||
	    qEnvironmentVariable("PAKFU_EXTENSION_COMMAND_ID") != command_id ||
	    qEnvironmentVariable("PAKFU_EXTENSION_SCHEMA") != "pakfu-extension/v1") {
		set_error(error, "Child mode received unexpected extension environment variables.");
		return 1;
	}

	const QJsonObject archive = root.value("archive").toObject();
	if (archive.value("format").toString() != "zip") {
		set_error(error, "Child mode received the wrong archive format.");
		return 1;
	}

	const QJsonArray entries = root.value("entries").toArray();
	if (command_id == "inspect") {
		if (entries.size() != 1) {
			set_error(error, "Inspect command expected exactly one entry.");
			return 1;
		}

		const QJsonObject entry = entries.first().toObject();
		if (entry.value("archive_name").toString() != "scripts/test.cfg") {
			set_error(error, "Inspect command received the wrong archive entry name.");
			return 1;
		}
		const QString local_path = entry.value("local_path").toString();
		if (!QFileInfo(local_path).isFile()) {
			set_error(error, "Inspect command did not receive a materialized file path.");
			return 1;
		}
	} else if (command_id == "archive-info") {
		if (!entries.isEmpty()) {
			set_error(error, "Archive-info command should not receive entries in this test.");
			return 1;
		}
	} else {
		set_error(error, QString("Child mode received an unexpected command id: %1").arg(command_id));
		return 1;
	}

	QTextStream(stdout) << "child-ok:" << command_id << ':' << entries.size() << '\n';
	QTextStream(stderr) << "child-stderr:" << command_id << '\n';
	return 0;
}

bool run_test(QString* error) {
	QTemporaryDir temp;
	if (!temp.isValid()) {
		set_error(error, "Unable to create temporary directory.");
		return false;
	}

	const QString plugin_dir = QDir(temp.path()).filePath("plugins/fixture");
	const QString manifest_path = QDir(plugin_dir).filePath("plugin.json");
	const QString child_program = QCoreApplication::applicationFilePath();
	QJsonArray child_command;
	child_command.append(child_program);
	child_command.append("--extension-child");

	QJsonObject inspect;
	inspect.insert("id", "inspect");
	inspect.insert("name", "Inspect CFG");
	inspect.insert("description", "Fixture command that expects a selected CFG file.");
	inspect.insert("requires_entries", true);
	inspect.insert("allow_multiple", false);
	inspect.insert("command", child_command);
	QJsonArray inspect_extensions;
	inspect_extensions.append("cfg");
	inspect.insert("extensions", inspect_extensions);

	QJsonObject archive_info;
	archive_info.insert("id", "archive-info");
	archive_info.insert("name", "Archive Info");
	archive_info.insert("description", "Fixture command that inspects archive metadata only.");
	archive_info.insert("command", child_command);

	QJsonObject manifest;
	manifest.insert("schema_version", 1);
	manifest.insert("id", "fixture");
	manifest.insert("name", "Fixture Tools");
	manifest.insert("description", "Synthetic commands for extension contract tests.");
	QJsonArray commands_array;
	commands_array.append(inspect);
	commands_array.append(archive_info);
	manifest.insert("commands", commands_array);

	QString write_err;
	if (!write_file(manifest_path, QJsonDocument(manifest).toJson(QJsonDocument::Indented), &write_err)) {
		set_error(error, write_err);
		return false;
	}

	QVector<ExtensionCommand> commands;
	QStringList warnings;
	QString load_err;
	if (!load_extension_commands({QDir(temp.path()).filePath("plugins")}, &commands, &warnings, &load_err)) {
		set_error(error, load_err.isEmpty() ? "Unable to load extension manifest." : load_err);
		return false;
	}
	if (!warnings.isEmpty()) {
		set_error(error, QString("Unexpected extension manifest warning: %1").arg(warnings.first()));
		return false;
	}
	if (commands.size() != 2) {
		set_error(error, QString("Expected two extension commands, found %1.").arg(commands.size()));
		return false;
	}

	QString find_err;
	if (find_extension_command(commands, "fixture", &find_err) != nullptr ||
	    !find_err.contains("multiple commands", Qt::CaseInsensitive)) {
		set_error(error, "Bare plugin lookup should fail when a plugin exposes multiple commands.");
		return false;
	}

	const ExtensionCommand* inspect_command = find_extension_command(commands, "fixture:inspect", &find_err);
	if (!inspect_command) {
		set_error(error, find_err.isEmpty() ? "Inspect command lookup failed." : find_err);
		return false;
	}
	const ExtensionCommand* archive_info_command = find_extension_command(commands, "fixture:archive-info", &find_err);
	if (!archive_info_command) {
		set_error(error, find_err.isEmpty() ? "Archive-info command lookup failed." : find_err);
		return false;
	}

	QString selection_err;
	const QVector<ExtensionEntryContext> no_entries;
	if (extension_command_accepts_entries(*inspect_command, no_entries, &selection_err) ||
	    !selection_err.contains("requires", Qt::CaseInsensitive)) {
		set_error(error, "Inspect command should require at least one selected entry.");
		return false;
	}

	ExtensionEntryContext wrong_entry;
	wrong_entry.archive_name = "scripts/test.txt";
	wrong_entry.local_path = QDir(temp.path()).filePath("scripts/test.txt");
	QVector<ExtensionEntryContext> wrong_entries{wrong_entry};
	if (extension_command_accepts_entries(*inspect_command, wrong_entries, &selection_err) ||
	    !selection_err.contains("does not support", Qt::CaseInsensitive)) {
		set_error(error, "Inspect command should reject unsupported file extensions.");
		return false;
	}

	ExtensionEntryContext extra_entry;
	extra_entry.archive_name = "scripts/extra.cfg";
	extra_entry.local_path = QDir(temp.path()).filePath("scripts/extra.cfg");
	QVector<ExtensionEntryContext> multiple_entries{wrong_entry, extra_entry};
	if (extension_command_accepts_entries(*inspect_command, multiple_entries, &selection_err) ||
	    !selection_err.contains("only one", Qt::CaseInsensitive)) {
		set_error(error, "Inspect command should reject multi-selection.");
		return false;
	}

	const QString local_cfg_path = QDir(temp.path()).filePath("selection/scripts/test.cfg");
	if (!write_file(local_cfg_path, "set fixture 1\n", &write_err)) {
		set_error(error, write_err);
		return false;
	}

	ExtensionEntryContext selected_entry;
	selected_entry.archive_name = "scripts/test.cfg";
	selected_entry.local_path = local_cfg_path;
	selected_entry.size = 14;
	selected_entry.mtime_utc_secs = 42;

	ExtensionRunContext inspect_context;
	inspect_context.archive_path = QDir(temp.path()).filePath("fixture.pk3");
	inspect_context.readable_archive_path = inspect_context.archive_path;
	inspect_context.archive_format = "zip";
	inspect_context.current_prefix = "scripts/";
	inspect_context.entries.push_back(selected_entry);

	ExtensionRunResult inspect_result;
	QString run_err;
	if (!run_extension_command(*inspect_command, inspect_context, &inspect_result, &run_err)) {
		set_error(error, run_err.isEmpty() ? "Inspect command execution failed." : run_err);
		return false;
	}
	if (!inspect_result.std_out.contains("child-ok:inspect:1") ||
	    !inspect_result.std_err.contains("child-stderr:inspect")) {
		set_error(error, "Inspect command did not produce the expected child output.");
		return false;
	}

	ExtensionRunContext archive_info_context;
	archive_info_context.archive_path = inspect_context.archive_path;
	archive_info_context.readable_archive_path = inspect_context.readable_archive_path;
	archive_info_context.archive_format = "zip";

	ExtensionRunResult archive_info_result;
	if (!run_extension_command(*archive_info_command, archive_info_context, &archive_info_result, &run_err)) {
		set_error(error, run_err.isEmpty() ? "Archive-info command execution failed." : run_err);
		return false;
	}
	if (!archive_info_result.std_out.contains("child-ok:archive-info:0") ||
	    !archive_info_result.std_err.contains("child-stderr:archive-info")) {
		set_error(error, "Archive-info command did not produce the expected child output.");
		return false;
	}

	return true;
}
}  // namespace

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);

	const QStringList args = app.arguments();
	if (args.contains("--extension-child")) {
		QString error;
		const int rc = run_child_mode(&error);
		if (rc != 0) {
			fail_message(error.isEmpty() ? "Extension child mode failed." : error);
		}
		return rc;
	}

	QString error;
	if (!run_test(&error)) {
		fail_message(error.isEmpty() ? "Extension plugin test failed." : error);
		return 1;
	}
	return 0;
}
