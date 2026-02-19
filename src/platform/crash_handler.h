#pragma once

#include <QString>

namespace platform {
void install_crash_reporting();
QString crash_report_directory();
QString crash_session_log_path();
}  // namespace platform
