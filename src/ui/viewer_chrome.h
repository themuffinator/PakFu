#pragma once

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QObject>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSize>
#include <QStatusBar>
#include <QStringList>
#include <QTextEdit>
#include <QToolBar>

#include "ui/ui_icons.h"

namespace ViewerChrome {

struct Spec {
	QString toolbar_title;
	QString toolbar_accessible_name;
	QString previous_status_tip;
	QString next_status_tip;
	QString fullscreen_status_tip;
	QString index_accessible_name;
	QString path_accessible_name;
	QString status_accessible_name;
};

struct Widgets {
	QAction* previous_action = nullptr;
	QAction* next_action = nullptr;
	QAction* fullscreen_action = nullptr;
	QLabel* index_label = nullptr;
	QLabel* path_label = nullptr;
};

template <typename Receiver, typename PreviousSlot, typename NextSlot, typename FullscreenSlot, typename ExitFullscreenSlot>
Widgets setup(QMainWindow* window,
              Receiver* receiver,
              const Spec& spec,
              PreviousSlot previous_slot,
              NextSlot next_slot,
              FullscreenSlot fullscreen_slot,
              ExitFullscreenSlot exit_fullscreen_slot) {
	Widgets widgets;

	auto* toolbar = window->addToolBar(spec.toolbar_title);
	toolbar->setAccessibleName(spec.toolbar_accessible_name);
	toolbar->setIconSize(QSize(18, 18));
	toolbar->setMovable(false);
	toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

	widgets.previous_action = toolbar->addAction(UiIcons::icon(UiIcons::Id::MediaPrevious, window->style()),
	                                             QCoreApplication::translate("ViewerChrome", "Previous"));
	widgets.previous_action->setStatusTip(spec.previous_status_tip);
	widgets.next_action = toolbar->addAction(UiIcons::icon(UiIcons::Id::MediaNext, window->style()),
	                                         QCoreApplication::translate("ViewerChrome", "Next"));
	widgets.next_action->setStatusTip(spec.next_status_tip);
	toolbar->addSeparator();
	widgets.fullscreen_action =
		toolbar->addAction(UiIcons::icon(UiIcons::Id::FullscreenEnter, window->style()),
		                    QCoreApplication::translate("ViewerChrome", "Fullscreen"));
	widgets.fullscreen_action->setStatusTip(spec.fullscreen_status_tip);

	QObject::connect(widgets.previous_action, &QAction::triggered, receiver, previous_slot);
	QObject::connect(widgets.next_action, &QAction::triggered, receiver, next_slot);
	QObject::connect(widgets.fullscreen_action, &QAction::triggered, receiver, fullscreen_slot);

	auto* left_shortcut = new QShortcut(QKeySequence(Qt::Key_Left), window);
	QObject::connect(left_shortcut, &QShortcut::activated, receiver, previous_slot);
	auto* right_shortcut = new QShortcut(QKeySequence(Qt::Key_Right), window);
	QObject::connect(right_shortcut, &QShortcut::activated, receiver, next_slot);
	auto* f11_shortcut = new QShortcut(QKeySequence(Qt::Key_F11), window);
	QObject::connect(f11_shortcut, &QShortcut::activated, receiver, fullscreen_slot);
	auto* fullscreen_shortcut = new QShortcut(QKeySequence::FullScreen, window);
	QObject::connect(fullscreen_shortcut, &QShortcut::activated, receiver, fullscreen_slot);
	auto* esc_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), window);
	QObject::connect(esc_shortcut, &QShortcut::activated, receiver, exit_fullscreen_slot);

	widgets.index_label = new QLabel(window);
	widgets.index_label->setAccessibleName(spec.index_accessible_name);
	widgets.path_label = new QLabel(window);
	widgets.path_label->setAccessibleName(spec.path_accessible_name);
	widgets.path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

	if (window->statusBar()) {
		window->statusBar()->setAccessibleName(spec.status_accessible_name);
		window->statusBar()->addPermanentWidget(widgets.index_label);
		window->statusBar()->addWidget(widgets.path_label, 1);
	}

	return widgets;
}

inline void update_fullscreen_action(QAction* action, const QWidget* window) {
	if (!action || !window) {
		return;
	}
	const bool full = window->isFullScreen();
	action->setText(full ? QCoreApplication::translate("ViewerChrome", "Exit Fullscreen")
	                     : QCoreApplication::translate("ViewerChrome", "Fullscreen"));
	action->setIcon(UiIcons::icon(full ? UiIcons::Id::FullscreenExit : UiIcons::Id::FullscreenEnter, window->style()));
}

inline void update_status(QLabel* index_label,
                          QLabel* path_label,
                          QAction* previous_action,
                          QAction* next_action,
                          const QString& media_label,
                          const QStringList& paths,
                          int current_index,
                          const QString& current_path) {
	if (index_label) {
		if (paths.isEmpty() || current_index < 0) {
			index_label->setText(QString("%1 0/0").arg(media_label));
		} else {
			index_label->setText(QString("%1 %2/%3").arg(media_label).arg(current_index + 1).arg(paths.size()));
		}
	}
	if (path_label) {
		const QString native_path = current_path.isEmpty() ? QString() : QDir::toNativeSeparators(current_path);
		path_label->setText(native_path);
		path_label->setToolTip(native_path);
	}

	const bool can_cycle = paths.size() > 1;
	if (previous_action) {
		previous_action->setEnabled(can_cycle);
	}
	if (next_action) {
		next_action->setEnabled(can_cycle);
	}
}

inline bool should_ignore_navigation_event_target(QObject* watched) {
	if (!watched) {
		return false;
	}
	return qobject_cast<QComboBox*>(watched) != nullptr ||
	       qobject_cast<QAbstractSpinBox*>(watched) != nullptr ||
	       qobject_cast<QAbstractSlider*>(watched) != nullptr ||
	       qobject_cast<QLineEdit*>(watched) != nullptr ||
	       qobject_cast<QTextEdit*>(watched) != nullptr ||
	       qobject_cast<QPlainTextEdit*>(watched) != nullptr;
}

}  // namespace ViewerChrome
