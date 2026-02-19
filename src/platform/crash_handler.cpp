#include "platform/crash_handler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace {
QMutex g_log_mutex;
QString g_crash_dir;
QString g_session_log_path;
std::atomic<bool> g_installed = false;

QString message_type_name(QtMsgType type) {
	switch (type) {
		case QtDebugMsg:
			return "DEBUG";
		case QtInfoMsg:
			return "INFO";
		case QtWarningMsg:
			return "WARN";
		case QtCriticalMsg:
			return "ERROR";
		case QtFatalMsg:
			return "FATAL";
	}
	return "LOG";
}

QString resolve_crash_dir() {
	QString dir = qEnvironmentVariable("PAKFU_CRASH_DIR").trimmed();
	if (dir.isEmpty()) {
		QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		if (base.isEmpty()) {
			base = QDir(QCoreApplication::applicationDirPath()).filePath("crash_reports");
		}
		dir = QDir(base).filePath("crashes");
	}
	return QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
}

void append_to_session_log(const QByteArray& bytes) {
	if (g_session_log_path.isEmpty()) {
		return;
	}
	QFile out(g_session_log_path);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
		return;
	}
	out.write(bytes);
	out.flush();
}

void qt_message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
	static thread_local bool in_handler = false;
	if (in_handler) {
		const QByteArray fallback = message.toLocal8Bit();
		std::fwrite(fallback.constData(), 1, static_cast<size_t>(fallback.size()), stderr);
		std::fwrite("\n", 1, 1, stderr);
		std::fflush(stderr);
		return;
	}
	in_handler = true;

	const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	QString line = QString("[%1] [%2] [tid:%3] %4")
	                 .arg(now)
	                 .arg(message_type_name(type))
	                 .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
	                 .arg(message);

	if (context.file && *context.file) {
		const QString file = QFileInfo(QString::fromUtf8(context.file)).fileName();
		line += QString(" (%1:%2, %3)")
		          .arg(file)
		          .arg(context.line)
		          .arg(context.function ? QString::fromUtf8(context.function) : QStringLiteral("?"));
	}

	QByteArray bytes = line.toUtf8();
	bytes.append('\n');

	{
		QMutexLocker lock(&g_log_mutex);
		append_to_session_log(bytes);
	}
	std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
	std::fflush(stderr);
	if (type == QtFatalMsg) {
		std::abort();
	}
	in_handler = false;
}

#ifdef Q_OS_WIN
std::wstring g_crash_dir_w;
std::wstring g_session_log_path_w;
std::atomic<unsigned long> g_crash_seq = 0;

std::wstring make_filename_stamp_local() {
	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t buf[64]{};
	_snwprintf_s(buf,
	             _countof(buf),
	             _TRUNCATE,
	             L"%04u%02u%02u-%02u%02u%02u-%03u",
	             st.wYear,
	             st.wMonth,
	             st.wDay,
	             st.wHour,
	             st.wMinute,
	             st.wSecond,
	             st.wMilliseconds);
	return std::wstring(buf);
}

std::string make_iso_utc_now() {
	SYSTEMTIME st{};
	GetSystemTime(&st);
	char buf[64]{};
	_snprintf_s(buf,
	            _countof(buf),
	            _TRUNCATE,
	            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
	            st.wYear,
	            st.wMonth,
	            st.wDay,
	            st.wHour,
	            st.wMinute,
	            st.wSecond,
	            st.wMilliseconds);
	return std::string(buf);
}

std::wstring join_windows_path(const std::wstring& lhs, const std::wstring& rhs) {
	if (lhs.empty()) {
		return rhs;
	}
	if (lhs.back() == L'\\' || lhs.back() == L'/') {
		return lhs + rhs;
	}
	return lhs + L"\\" + rhs;
}

std::string narrow_utf8(const std::wstring& text) {
	if (text.empty()) {
		return {};
	}
	int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (bytes <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(bytes - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
	return out;
}

bool write_utf8_file(const std::wstring& file_path, const std::string& text) {
	HANDLE out = CreateFileW(file_path.c_str(),
	                         GENERIC_WRITE,
	                         FILE_SHARE_READ,
	                         nullptr,
	                         CREATE_ALWAYS,
	                         FILE_ATTRIBUTE_NORMAL,
	                         nullptr);
	if (out == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD written = 0;
	const BOOL ok = WriteFile(out,
	                          text.data(),
	                          static_cast<DWORD>(text.size()),
	                          &written,
	                          nullptr);
	CloseHandle(out);
	return ok == TRUE && written == text.size();
}

bool write_minidump(const std::wstring& dump_path, EXCEPTION_POINTERS* exception_pointers, std::string* error) {
	HANDLE out = CreateFileW(dump_path.c_str(),
	                         GENERIC_WRITE,
	                         FILE_SHARE_READ,
	                         nullptr,
	                         CREATE_ALWAYS,
	                         FILE_ATTRIBUTE_NORMAL,
	                         nullptr);
	if (out == INVALID_HANDLE_VALUE) {
		if (error) {
			*error = "CreateFileW failed for dump file.";
		}
		return false;
	}

	MINIDUMP_EXCEPTION_INFORMATION exception_info{};
	exception_info.ThreadId = GetCurrentThreadId();
	exception_info.ExceptionPointers = exception_pointers;
	exception_info.ClientPointers = FALSE;

	const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
		MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
		MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData | MiniDumpWithIndirectlyReferencedMemory);

	const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
	                                  GetCurrentProcessId(),
	                                  out,
	                                  dump_type,
	                                  exception_pointers ? &exception_info : nullptr,
	                                  nullptr,
	                                  nullptr);
	if (!ok && error) {
		*error = "MiniDumpWriteDump failed.";
	}
	CloseHandle(out);
	return ok == TRUE;
}

void append_stack_trace(std::string* out, EXCEPTION_POINTERS* exception_pointers) {
	if (!out) {
		return;
	}
	if (!exception_pointers || !exception_pointers->ContextRecord) {
		out->append("Stack trace unavailable (missing exception context).\n");
		return;
	}

	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	if (!SymInitialize(process, nullptr, TRUE)) {
		out->append("SymInitialize failed; stack trace unavailable.\n");
		return;
	}

	CONTEXT ctx = *exception_pointers->ContextRecord;
	STACKFRAME64 frame{};
	DWORD machine = 0;
#if defined(_M_X64) || defined(__x86_64__)
	machine = IMAGE_FILE_MACHINE_AMD64;
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86) || defined(__i386__)
	machine = IMAGE_FILE_MACHINE_I386;
	frame.AddrPC.Offset = ctx.Eip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Ebp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Esp;
	frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64) || defined(__aarch64__)
	machine = IMAGE_FILE_MACHINE_ARM64;
	frame.AddrPC.Offset = ctx.Pc;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Fp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Sp;
	frame.AddrStack.Mode = AddrModeFlat;
#else
	out->append("Stack trace unavailable on this architecture.\n");
	SymCleanup(process);
	return;
#endif

	out->append("Stack trace:\n");
	for (int i = 0; i < 128; ++i) {
		const BOOL ok = StackWalk64(machine,
		                            process,
		                            thread,
		                            &frame,
		                            &ctx,
		                            nullptr,
		                            SymFunctionTableAccess64,
		                            SymGetModuleBase64,
		                            nullptr);
		if (!ok || frame.AddrPC.Offset == 0) {
			break;
		}

		const DWORD64 addr = frame.AddrPC.Offset;
		char sym_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
		auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_storage);
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = MAX_SYM_NAME;

		DWORD64 displacement = 0;
		std::string symbol_name = "(unknown)";
		if (SymFromAddr(process, addr, &displacement, sym)) {
			symbol_name = sym->Name;
		}

		IMAGEHLP_LINE64 line{};
		line.SizeOfStruct = sizeof(line);
		DWORD line_disp = 0;
		std::string line_info = "(no line info)";
		if (SymGetLineFromAddr64(process, addr, &line_disp, &line)) {
			line_info = std::string(line.FileName ? line.FileName : "?") + ":" + std::to_string(line.LineNumber);
		}

		IMAGEHLP_MODULE64 module{};
		module.SizeOfStruct = sizeof(module);
		std::string module_name = "(module?)";
		if (SymGetModuleInfo64(process, addr, &module) && module.ModuleName[0] != '\0') {
			module_name = module.ModuleName;
		}

		char line_buf[1024]{};
		_snprintf_s(line_buf,
		            _countof(line_buf),
		            _TRUNCATE,
		            "  #%02d 0x%016llX %s!%s +0x%llX  [%s]\n",
		            i,
		            static_cast<unsigned long long>(addr),
		            module_name.c_str(),
		            symbol_name.c_str(),
		            static_cast<unsigned long long>(displacement),
		            line_info.c_str());
		out->append(line_buf);
	}

	SymCleanup(process);
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_pointers) {
	const DWORD pid = GetCurrentProcessId();
	const DWORD tid = GetCurrentThreadId();
	const unsigned long seq = g_crash_seq.fetch_add(1) + 1;
	const std::wstring stamp = make_filename_stamp_local();

	wchar_t stem[128]{};
	_snwprintf_s(stem,
	             _countof(stem),
	             _TRUNCATE,
	             L"pakfu-crash-%s-p%lu-t%lu-%lu",
	             stamp.c_str(),
	             static_cast<unsigned long>(pid),
	             static_cast<unsigned long>(tid),
	             seq);

	const std::wstring log_path = join_windows_path(g_crash_dir_w, std::wstring(stem) + L".log");
	const std::wstring dump_path = join_windows_path(g_crash_dir_w, std::wstring(stem) + L".dmp");

	std::string report;
	report.reserve(32 * 1024);
	report.append("PakFu Crash Report\n");
	report.append("==================\n");
	report.append("Timestamp (UTC): " + make_iso_utc_now() + "\n");
	report.append("Process ID: " + std::to_string(pid) + "\n");
	report.append("Thread ID: " + std::to_string(tid) + "\n");
	report.append("Session log: " + narrow_utf8(g_session_log_path_w) + "\n");

	if (exception_pointers && exception_pointers->ExceptionRecord) {
		const auto* record = exception_pointers->ExceptionRecord;
		char exc_line[256]{};
		_snprintf_s(exc_line,
		            _countof(exc_line),
		            _TRUNCATE,
		            "Exception code: 0x%08lX\nException address: 0x%p\n",
		            static_cast<unsigned long>(record->ExceptionCode),
		            record->ExceptionAddress);
		report.append(exc_line);
	} else {
		report.append("Exception context unavailable.\n");
	}

	report.append("\n");
	append_stack_trace(&report, exception_pointers);
	report.append("\n");

	std::string dump_err;
	const bool dump_ok = write_minidump(dump_path, exception_pointers, &dump_err);
	report.append("MiniDump: " + narrow_utf8(dump_path) + (dump_ok ? " (written)\n" : " (failed)\n"));
	if (!dump_ok && !dump_err.empty()) {
		report.append("MiniDump error: " + dump_err + "\n");
	}

	const bool log_ok = write_utf8_file(log_path, report);
	if (!log_ok) {
		OutputDebugStringA(report.c_str());
	}

	const std::string summary = "PakFu crash captured. Log: " + narrow_utf8(log_path) +
	                            " | Dump: " + narrow_utf8(dump_path) + "\n";
	OutputDebugStringA(summary.c_str());
	std::fwrite(summary.data(), 1, summary.size(), stderr);
	std::fflush(stderr);

	return EXCEPTION_EXECUTE_HANDLER;
}

void install_windows_exception_capture() {
	g_crash_dir_w = g_crash_dir.toStdWString();
	g_session_log_path_w = g_session_log_path.toStdWString();
	SetUnhandledExceptionFilter(unhandled_exception_filter);
}
#endif
}  // namespace

namespace platform {
void install_crash_reporting() {
	if (g_installed.exchange(true)) {
		return;
	}

	g_crash_dir = resolve_crash_dir();
	QDir().mkpath(g_crash_dir);

	const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
	const qint64 pid = QCoreApplication::applicationPid();
	g_session_log_path = QDir(g_crash_dir).filePath(QString("pakfu-session-%1-p%2.log").arg(stamp).arg(pid));

	{
		QMutexLocker lock(&g_log_mutex);
		QFile out(g_session_log_path);
		if (out.open(QIODevice::WriteOnly | QIODevice::Append)) {
			QTextStream ts(&out);
			ts << "PakFu session log\n";
			ts << "=================\n";
			ts << "Started (UTC): " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n";
			ts << "PID: " << pid << "\n\n";
		}
	}

	const QString disable_qt_hook = qEnvironmentVariable("PAKFU_DISABLE_QT_MESSAGE_HOOK").trimmed().toLower();
	const bool use_qt_message_hook = !(disable_qt_hook == "1" || disable_qt_hook == "true" ||
	                                   disable_qt_hook == "yes" || disable_qt_hook == "on");
	if (use_qt_message_hook) {
		qInstallMessageHandler(qt_message_handler);
	}
#ifdef Q_OS_WIN
	install_windows_exception_capture();
#endif

	qInfo().noquote() << QString("Crash reporting enabled: %1")
	                     .arg(QDir::toNativeSeparators(g_crash_dir));
}

QString crash_report_directory() {
	return g_crash_dir;
}

QString crash_session_log_path() {
	return g_session_log_path;
}
}  // namespace platform
