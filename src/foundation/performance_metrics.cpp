#include "foundation/performance_metrics.h"

#include <QDebug>

#include <utility>

namespace PakFu::Metrics {
namespace {

[[nodiscard]] bool env_value_enabled(const char* name) {
	const QString value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toLower();
	if (value.isEmpty()) {
		return false;
	}
	return value != "0" && value != "false" && value != "no" && value != "off";
}

}  // namespace

bool tracing_enabled() {
	static const bool enabled = env_value_enabled("PAKFU_TRACE_PERF") || env_value_enabled("PAKFU_PERF_TRACE");
	return enabled;
}

bool ux_tracing_enabled() {
	static const bool enabled = env_value_enabled("PAKFU_TRACE_UX") || env_value_enabled("PAKFU_UX_TRACE");
	return enabled;
}

QString elapsed_label(qint64 elapsed_ms) {
	return elapsed_ms <= 0 ? QString("<1 ms") : QString("%1 ms").arg(elapsed_ms);
}

void add_profile_step(QStringList* steps, const QString& label, qint64 elapsed_ms, bool cache_hit) {
	if (!steps || label.trimmed().isEmpty()) {
		return;
	}
	steps->push_back(QString("%1 %2%3")
	                   .arg(label, elapsed_label(elapsed_ms), cache_hit ? QString(" (cached)") : QString()));
}

QString profile_text(QStringList steps, qint64 total_ms) {
	steps.removeAll({});
	add_profile_step(&steps, "total", total_ms);
	return steps.join("; ");
}

void record_timing(const QString& category, const QString& label, qint64 elapsed_ms, const QString& detail) {
	if (!tracing_enabled()) {
		return;
	}

	QString message = QString("PakFu timing: %1.%2 %3").arg(category, label, elapsed_label(elapsed_ms));
	if (!detail.trimmed().isEmpty()) {
		message += QString(" - %1").arg(detail.trimmed());
	}
	qInfo().noquote() << message;
}

void record_event(const QString& category, const QString& label, const QString& detail) {
	if (!ux_tracing_enabled()) {
		return;
	}

	QString message = QString("PakFu UX: %1.%2").arg(category, label);
	if (!detail.trimmed().isEmpty()) {
		message += QString(" - %1").arg(detail.trimmed());
	}
	qInfo().noquote() << message;
}

ScopedTimer::ScopedTimer(QString category, QString label, QString detail)
	: category_(std::move(category)), label_(std::move(label)), detail_(std::move(detail)) {
	timer_.start();
}

ScopedTimer::~ScopedTimer() {
	if (active_) {
		record_timing(category_, label_, timer_.elapsed(), detail_);
	}
}

void ScopedTimer::set_detail(QString detail) {
	detail_ = std::move(detail);
}

void ScopedTimer::cancel() {
	active_ = false;
}

qint64 ScopedTimer::elapsed_ms() const {
	return timer_.isValid() ? timer_.elapsed() : 0;
}

}  // namespace PakFu::Metrics
