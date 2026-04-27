#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct ExtensionCommand {
	QString plugin_id;
	QString plugin_name;
	QString plugin_description;
	QString command_id;
	QString command_name;
	QString command_description;
	QString manifest_path;
	QString working_directory;
	QStringList argv;
	QStringList capabilities;
	QStringList allowed_extensions;
	bool requires_entries = false;
	bool allow_multiple = true;
};

struct ExtensionEntryContext {
	QString archive_name;
	QString local_path;
	bool is_dir = false;
	quint32 size = 0;
	qint64 mtime_utc_secs = -1;
};

struct ExtensionRunContext {
	QString archive_path;
	QString readable_archive_path;
	QString archive_format;
	QString mounted_entry;
	QString current_prefix;
	QString import_root;
	bool quakelive_encrypted_pk3 = false;
	bool wad3 = false;
	bool doom_wad = false;
	QVector<ExtensionEntryContext> entries;
};

struct ExtensionImportEntry {
	QString archive_name;
	QString local_path;
	QString mode = "add_or_replace";
	qint64 mtime_utc_secs = -1;
};

struct ExtensionRunResult {
	int exit_code = -1;
	QString std_out;
	QString std_err;
	QVector<ExtensionImportEntry> imports;
};

[[nodiscard]] QString extension_command_ref(const ExtensionCommand& command);
[[nodiscard]] QString extension_command_display_name(const ExtensionCommand& command);
[[nodiscard]] QStringList extension_host_capabilities();
[[nodiscard]] bool extension_command_has_capability(const ExtensionCommand& command, const QString& capability);
[[nodiscard]] QStringList default_extension_search_dirs();
[[nodiscard]] bool load_extension_commands(const QStringList& search_dirs,
                                          QVector<ExtensionCommand>* out,
                                          QStringList* warnings,
                                          QString* error);
[[nodiscard]] const ExtensionCommand* find_extension_command(const QVector<ExtensionCommand>& commands,
                                                            const QString& ref,
                                                            QString* error);
[[nodiscard]] bool extension_command_accepts_entries(const ExtensionCommand& command,
                                                     const QVector<ExtensionEntryContext>& entries,
                                                     QString* error);
[[nodiscard]] bool run_extension_command(const ExtensionCommand& command,
                                         const ExtensionRunContext& context,
                                         ExtensionRunResult* result,
                                         QString* error);
