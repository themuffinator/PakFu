#include "ui/background_task_coordinator.h"

#include <QThreadPool>

quint64 BackgroundTaskCoordinator::begin_generation() {
	++generation_;
	return generation_;
}

void BackgroundTaskCoordinator::cancel_generation() {
	++generation_;
}

void BackgroundTaskCoordinator::cancel_and_clear(QThreadPool& pool) {
	cancel_generation();
	pool.clear();
}
