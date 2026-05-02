#include "ui/practical_qa.h"

#include <QAction>
#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>
#include <QSaveFile>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTreeView>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "ui/drag_drop_policy.h"
#include "ui/pak_tab.h"

namespace {
struct QaCheck {
  QString name;
  bool passed = false;
  QString detail;
};

void pump_events(int rounds = 2) {
  for (int i = 0; i < rounds; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
}

bool write_text_file(const QString& path, const QByteArray& bytes, QString* error) {
  QFileInfo info(path);
  QDir parent(info.absolutePath());
  if (!parent.exists() && !parent.mkpath(".")) {
    if (error) {
      *error = QString("Unable to create directory: %1").arg(info.absolutePath());
    }
    return false;
  }

  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to open file: %1").arg(path);
    }
    return false;
  }
  if (out.write(bytes) != bytes.size()) {
    if (error) {
      *error = QString("Unable to write file: %1").arg(path);
    }
    return false;
  }
  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize file: %1").arg(path);
    }
    return false;
  }
  return true;
}

bool build_fixture_tree(const QString& root, QString* error) {
  const struct Entry {
    const char* rel;
    const char* bytes;
  } files[] = {
      {"alpha.txt", "alpha\n"},
      {"bravo.txt", "bravo\n"},
      {"charlie.cfg", "seta test 1\n"},
      {"delta.ogg", "not-a-real-ogg\n"},
      {"echo.pcx", "not-a-real-pcx\n"},
      {"folder_one/readme.txt", "inside folder one\n"},
      {"folder_two/notes.txt", "inside folder two\n"},
  };

  for (const Entry& entry : files) {
    const QString abs = QDir(root).filePath(QString::fromLatin1(entry.rel));
    if (!write_text_file(abs, QByteArray(entry.bytes), error)) {
      return false;
    }
  }
  return true;
}

void send_mouse_event(QWidget* target,
                      QEvent::Type type,
                      const QPoint& pos,
                      Qt::MouseButton button,
                      Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers) {
  if (!target) {
    return;
  }
  QMouseEvent event(type,
                    QPointF(pos),
                    QPointF(target->mapToGlobal(pos)),
                    button,
                    buttons,
                    modifiers);
  QCoreApplication::sendEvent(target, &event);
}

void send_left_click(QWidget* target, const QPoint& pos, Qt::KeyboardModifiers modifiers) {
  send_mouse_event(target, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse_event(target, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, modifiers);
  pump_events();
}

void send_left_drag(QWidget* target, const QPoint& start, const QPoint& end, Qt::KeyboardModifiers modifiers) {
  send_mouse_event(target, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, modifiers);
  constexpr int kSteps = 6;
  for (int i = 1; i <= kSteps; ++i) {
    const qreal t = static_cast<qreal>(i) / static_cast<qreal>(kSteps);
    const int x = static_cast<int>(std::lround(start.x() + (end.x() - start.x()) * t));
    const int y = static_cast<int>(std::lround(start.y() + (end.y() - start.y()) * t));
    send_mouse_event(target, QEvent::MouseMove, QPoint(x, y), Qt::NoButton, Qt::LeftButton, modifiers);
    pump_events(1);
  }
  send_mouse_event(target, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
  pump_events();
}

void send_key_combo(QWidget* target, int key, Qt::KeyboardModifiers modifiers) {
  if (!target) {
    return;
  }
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QCoreApplication::sendEvent(target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(target, &release);
  pump_events();
}

Qt::KeyboardModifiers toggle_selection_modifier() {
#if defined(Q_OS_MACOS)
  return Qt::MetaModifier;
#else
  return Qt::ControlModifier;
#endif
}

QModelIndex view_row_index(QAbstractItemView* view, int row) {
  if (!view || !view->model()) {
    return {};
  }
  return view->model()->index(row, 0, view->rootIndex());
}

int selectable_rows(QAbstractItemView* view) {
  if (!view || !view->model()) {
    return 0;
  }
  int count = 0;
  const int rows = view->model()->rowCount(view->rootIndex());
  for (int row = 0; row < rows; ++row) {
    const QModelIndex index = view_row_index(view, row);
    if (index.isValid() && (view->model()->flags(index) & Qt::ItemIsSelectable)) {
      ++count;
    }
  }
  return count;
}

int selected_row_count(QAbstractItemView* view) {
  if (!view || !view->selectionModel()) {
    return 0;
  }
  QSet<int> rows;
  const QModelIndexList selected = view->selectionModel()->selectedIndexes();
  for (const QModelIndex& index : selected) {
    if (index.isValid()) {
      rows.insert(index.row());
    }
  }
  return rows.size();
}

bool click_view_row(QAbstractItemView* view, int row, Qt::KeyboardModifiers modifiers, QString* error) {
  if (!view) {
    if (error) {
      *error = "View not found.";
    }
    return false;
  }
  const QModelIndex index = view_row_index(view, row);
  if (!index.isValid()) {
    if (error) {
      *error = QString("Row %1 not found.").arg(row);
    }
    return false;
  }

  view->scrollTo(index);
  pump_events();
  const QRect rect = view->visualRect(index);
  if (!rect.isValid()) {
    if (error) {
      *error = QString("Row %1 has invalid visual rect.").arg(row);
    }
    return false;
  }

  QWidget* vp = view->viewport();
  const int x = std::clamp(rect.left() + 12, 2, qMax(2, vp->width() - 3));
  const int y = std::clamp(rect.center().y(), 2, qMax(2, vp->height() - 3));
  send_left_click(vp, QPoint(x, y), modifiers);
  return true;
}

QPoint find_empty_point(QAbstractItemView* view) {
  if (!view || !view->viewport()) {
    return {};
  }
  QWidget* vp = view->viewport();
  const int w = vp->width();
  const int h = vp->height();
  const int xs[] = {6, qMax(6, w / 2), qMax(6, w - 6)};
  for (int y = h - 2; y >= 2; y -= 6) {
    for (int x : xs) {
      const QPoint p(std::clamp(x, 2, qMax(2, w - 2)), y);
      if (!view->indexAt(p).isValid()) {
        return p;
      }
    }
  }
  return {};
}

void add_check(QVector<QaCheck>* checks, const QString& name, bool passed, const QString& detail) {
  if (!checks) {
    return;
  }
  checks->push_back(QaCheck{name, passed, detail});
}

bool trigger_action_by_text(QWidget* root, const QString& text) {
  if (!root) {
    return false;
  }
  const QList<QAction*> actions = root->findChildren<QAction*>();
  for (QAction* action : actions) {
    if (!action) {
      continue;
    }
    QString action_text = action->text();
    action_text.replace("&", "");
    if (action_text.compare(text, Qt::CaseInsensitive) == 0) {
      action->trigger();
      pump_events(2);
      return true;
    }
  }
  return false;
}
}  // namespace

int run_practical_archive_ops_qa() {
  QTextStream out(stdout);
  QTextStream err(stderr);
  QVector<QaCheck> checks;

  QTemporaryDir fixture;
  if (!fixture.isValid()) {
    err << "Practical QA: unable to create temporary fixture directory.\n";
    return 1;
  }

  QString fixture_error;
  if (!build_fixture_tree(fixture.path(), &fixture_error)) {
    err << "Practical QA: fixture setup failed: " << fixture_error << "\n";
    return 1;
  }

  PakTab tab(PakTab::Mode::ExistingPak, fixture.path());
  tab.resize(1180, 860);
  tab.show();
  tab.raise();
  tab.activateWindow();
  pump_events(6);

  auto* details = tab.findChild<QTreeView*>(QStringLiteral("pakfuArchiveDetailsView"));
  auto* icons = tab.findChild<QListView*>(QStringLiteral("pakfuArchiveIconView"));
  auto* stack = tab.findChild<QStackedWidget*>(QStringLiteral("pakfuArchiveViewStack"));

  add_check(&checks,
            "View widgets discovered",
            details && icons && stack,
            details && icons && stack ? "details/icon/stack found" : "missing one or more required views");

  if (!details || !icons || !stack) {
    for (const QaCheck& check : checks) {
      out << (check.passed ? "[PASS] " : "[FAIL] ") << check.name << ": " << check.detail << "\n";
    }
    err << "Practical QA failed: required UI views not available.\n";
    return 1;
  }

  const bool switched_to_details = trigger_action_by_text(&tab, "Details");
  if (!switched_to_details) {
    stack->setCurrentWidget(details);
  }
  details->setFocus();
  pump_events(4);

  const int detail_rows = selectable_rows(details);
  add_check(&checks,
            "Details view has selectable rows",
            detail_rows >= 3,
            QString("rows=%1 (need >=3)").arg(detail_rows));

  if (detail_rows >= 3) {
    QString click_error;
    details->clearSelection();
    const QModelIndex anchor = view_row_index(details, 0);
    bool details_click_ok = (anchor.isValid() && details->selectionModel());
    if (details_click_ok) {
      details->setCurrentIndex(anchor);
      details->selectionModel()->select(anchor, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      pump_events();
      details->setFocus();
      send_key_combo(details, Qt::Key_Down, Qt::ShiftModifier);
      send_key_combo(details, Qt::Key_Down, Qt::ShiftModifier);
    }
    const int after_shift = selected_row_count(details);
    add_check(&checks,
              "Shift range selection in details",
              details_click_ok && after_shift == 3,
              details_click_ok ? QString("selected=%1 expected=3").arg(after_shift)
                               : (click_error.isEmpty() ? "Unable to set range anchor." : click_error));

    click_error.clear();
    const Qt::KeyboardModifiers toggle_mod = toggle_selection_modifier();
    const bool toggle_ok = click_view_row(details, 1, toggle_mod, &click_error);
    const int after_toggle = selected_row_count(details);
    add_check(&checks,
              "Toggle selection in details",
              toggle_ok && after_toggle == 2,
              toggle_ok ? QString("selected=%1 expected=2").arg(after_toggle) : click_error);

    send_key_combo(&tab, Qt::Key_A, toggle_selection_modifier());
    const int after_select_all = selected_row_count(details);
    add_check(&checks,
              "Select-all shortcut in details",
              after_select_all == detail_rows,
              QString("selected=%1 expected=%2").arg(after_select_all).arg(detail_rows));

    details->clearSelection();
    pump_events();
    QPoint drag_start = find_empty_point(details);
    if (drag_start.isNull()) {
      tab.resize(1180, 980);
      pump_events(4);
      drag_start = find_empty_point(details);
    }

    QPoint drag_end;
    const QModelIndex target = view_row_index(details, qMin(2, detail_rows - 1));
    if (target.isValid()) {
      const QRect rect = details->visualRect(target);
      drag_end = QPoint(std::clamp(rect.left() + 16, 2, qMax(2, details->viewport()->width() - 3)),
                        std::clamp(rect.center().y(), 2, qMax(2, details->viewport()->height() - 3)));
    }

    bool marquee_ok = !drag_start.isNull() && !drag_end.isNull();
    if (marquee_ok) {
      send_left_drag(details->viewport(), drag_start, drag_end, Qt::NoModifier);
    }
    const int after_marquee = selected_row_count(details);
    add_check(&checks,
              "Marquee selection in details",
              marquee_ok && after_marquee >= 2,
              marquee_ok ? QString("selected=%1 expected>=2").arg(after_marquee)
                         : "could not find drag points in details view");
  }

  const bool switched_to_list = trigger_action_by_text(&tab, "List");
  if (!switched_to_list) {
    stack->setCurrentWidget(icons);
  }
  stack->setCurrentWidget(icons);
  icons->setFocus();
  pump_events(4);

  const int icon_items = selectable_rows(icons);
  add_check(&checks,
            "Icon view has selectable items",
            icon_items >= 3,
            QString("items=%1 (need >=3)").arg(icon_items));

  if (icon_items >= 3) {
    send_key_combo(&tab, Qt::Key_A, toggle_selection_modifier());
    const int after_select_all = selected_row_count(icons);
    add_check(&checks,
              "Select-all shortcut in icon view",
              after_select_all == icon_items,
              QString("selected=%1 expected=%2").arg(after_select_all).arg(icon_items));

    icons->clearSelection();
    pump_events();

    QPoint drag_start = find_empty_point(icons);
    if (drag_start.isNull()) {
      tab.resize(1180, 980);
      pump_events(4);
      drag_start = find_empty_point(icons);
    }

    QRect target_rect;
    for (int i = 0; i < qMin(4, icon_items); ++i) {
      const QModelIndex index = view_row_index(icons, i);
      const QRect rect = index.isValid() ? icons->visualRect(index) : QRect();
      if (rect.isValid()) {
        target_rect = target_rect.isNull() ? rect : target_rect.united(rect);
      }
    }

    QPoint drag_end;
    if (target_rect.isValid()) {
      drag_end = QPoint(std::clamp(target_rect.center().x(), 2, qMax(2, icons->viewport()->width() - 3)),
                        std::clamp(target_rect.center().y(), 2, qMax(2, icons->viewport()->height() - 3)));
    }

    bool marquee_ok = !drag_start.isNull() && !drag_end.isNull();
    if (marquee_ok) {
      send_left_drag(icons->viewport(), drag_start, drag_end, Qt::NoModifier);
    }
    const int after_marquee = selected_row_count(icons);
    add_check(&checks,
              "Marquee selection in icon view",
              marquee_ok && after_marquee >= 2,
              marquee_ok ? QString("selected=%1 expected>=2").arg(after_marquee)
                         : "could not find drag points in icon view");
  }

  PakTab interaction_tab(PakTab::Mode::NewPak, QString());
  interaction_tab.resize(980, 720);
  interaction_tab.show();
  interaction_tab.raise();
  interaction_tab.activateWindow();
  pump_events(4);

  auto* interaction_details = interaction_tab.findChild<QTreeView*>(QStringLiteral("pakfuArchiveDetailsView"));
  auto* interaction_icons = interaction_tab.findChild<QListView*>(QStringLiteral("pakfuArchiveIconView"));
  auto* interaction_stack = interaction_tab.findChild<QStackedWidget*>(QStringLiteral("pakfuArchiveViewStack"));
  add_check(&checks,
            "Interaction tab widgets discovered",
            interaction_details && interaction_icons && interaction_stack,
            interaction_details && interaction_icons && interaction_stack
              ? "details/icon/stack found"
              : "missing one or more required views");

  if (interaction_details && interaction_icons && interaction_stack) {
    QList<QUrl> seed_urls;
    seed_urls.push_back(QUrl::fromLocalFile(QDir(fixture.path()).filePath("alpha.txt")));
    seed_urls.push_back(QUrl::fromLocalFile(QDir(fixture.path()).filePath("bravo.txt")));
    auto* seed_mime = new QMimeData();
    seed_mime->setUrls(seed_urls);
    QStringList seed_lines;
    seed_lines.reserve(seed_urls.size());
    for (const QUrl& url : seed_urls) {
      if (url.isLocalFile()) {
        seed_lines.push_back(url.toLocalFile());
      }
    }
    seed_mime->setText(seed_lines.join('\n'));
    QApplication::clipboard()->setMimeData(seed_mime);
    interaction_tab.paste();
    pump_events(8);

    const int seeded_rows = selectable_rows(interaction_details);
    add_check(&checks,
              "Editable interaction tab seeded with entries",
              seeded_rows >= 2,
              QString("rows=%1 expected>=2").arg(seeded_rows));

    QMimeData sample_mime;
    sample_mime.setText(fixture.path());
    add_check(&checks,
              "Editable tab accepts import mime",
              interaction_tab.can_accept_mime(&sample_mime),
              interaction_tab.can_accept_mime(&sample_mime) ? "accepted" : "rejected");

    const auto check_details_dnd = [&](const QString& name, bool expect_drop_enabled) {
      const bool drag_enabled = interaction_details->dragEnabled();
      const bool view_accepts = interaction_details->acceptDrops();
      const bool vp_accepts =
        interaction_details->viewport() ? interaction_details->viewport()->acceptDrops() : false;
      const bool drag_mode_ok = interaction_details->dragDropMode() ==
                                (expect_drop_enabled ? QAbstractItemView::DragDrop
                                                     : QAbstractItemView::DragOnly);
      add_check(&checks,
                name,
                drag_enabled && drag_mode_ok && (view_accepts == expect_drop_enabled) &&
                  (vp_accepts == expect_drop_enabled),
                QString("drag=%1 mode=%2 accept=%3 viewport=%4 expectedAccept=%5")
                  .arg(drag_enabled ? "on" : "off")
                  .arg(static_cast<int>(interaction_details->dragDropMode()))
                  .arg(view_accepts ? "on" : "off")
                  .arg(vp_accepts ? "on" : "off")
                  .arg(expect_drop_enabled ? "on" : "off"));
    };

    const auto check_icon_variant = [&](const QString& action_text,
                                        QListView::ViewMode expected_mode,
                                        bool expected_wrapping) {
      const bool switched = trigger_action_by_text(&interaction_tab, action_text);
      const bool mode_ok = switched && interaction_icons->viewMode() == expected_mode;
      const bool stack_ok = switched && interaction_stack->currentWidget() == interaction_icons;
      const bool drag_enabled = interaction_icons->dragEnabled();
      const bool view_accepts = interaction_icons->acceptDrops();
      const bool vp_accepts =
        interaction_icons->viewport() ? interaction_icons->viewport()->acceptDrops() : false;
      const bool drag_mode_ok = interaction_icons->dragDropMode() == QAbstractItemView::DragDrop;
      const bool default_action_ok = interaction_icons->defaultDropAction() == Qt::CopyAction;
      const bool wrap_ok = interaction_icons->isWrapping() == expected_wrapping;
      add_check(&checks,
                QString("%1 view drag/drop enabled").arg(action_text),
                switched && mode_ok && stack_ok && drag_enabled && view_accepts && vp_accepts &&
                  drag_mode_ok && default_action_ok && wrap_ok,
                QString("switched=%1 mode=%2 stack=%3 drag=%4 accept=%5 viewport=%6 defaultAction=%7 wrap=%8")
                  .arg(switched ? "yes" : "no")
                  .arg(static_cast<int>(interaction_icons->viewMode()))
                  .arg(stack_ok ? "icon" : "other")
                  .arg(drag_enabled ? "on" : "off")
                  .arg(view_accepts ? "on" : "off")
                  .arg(vp_accepts ? "on" : "off")
                  .arg(static_cast<int>(interaction_icons->defaultDropAction()))
                  .arg(interaction_icons->isWrapping() ? "on" : "off"));
    };

    const bool switched_to_details_interaction = trigger_action_by_text(&interaction_tab, "Details");
    if (switched_to_details_interaction) {
      check_details_dnd("Details view drag/drop enabled", true);
    } else {
      add_check(&checks, "Details view drag/drop enabled", false, "unable to switch to Details action");
    }

    check_icon_variant("List", QListView::ListMode, false);
    check_icon_variant("Small Icons", QListView::IconMode, true);
    check_icon_variant("Large Icons", QListView::IconMode, true);
    check_icon_variant("Gallery", QListView::IconMode, true);

    interaction_tab.set_pure_pak_protector(true, true);
    pump_events(3);
    add_check(&checks,
              "Locked tab is non-editable",
              !interaction_tab.is_editable(),
              interaction_tab.is_editable() ? "editable (unexpected)" : "locked as expected");
    add_check(&checks,
              "Locked tab rejects import mime",
              !interaction_tab.can_accept_mime(&sample_mime),
              interaction_tab.can_accept_mime(&sample_mime) ? "accepted (unexpected)" : "rejected");

    check_details_dnd("Details view drop disabled when locked", false);

    const bool switched_to_list_locked = trigger_action_by_text(&interaction_tab, "List");
    const bool locked_drag_mode_ok = switched_to_list_locked &&
                                     interaction_icons->dragDropMode() == QAbstractItemView::DragOnly;
    const bool locked_accepts = interaction_icons->acceptDrops();
    const bool locked_vp_accepts =
      interaction_icons->viewport() ? interaction_icons->viewport()->acceptDrops() : false;
    add_check(&checks,
              "Icon/tiled view drop disabled when locked",
              switched_to_list_locked && locked_drag_mode_ok && !locked_accepts && !locked_vp_accepts,
              QString("switched=%1 mode=%2 accept=%3 viewport=%4")
                .arg(switched_to_list_locked ? "yes" : "no")
                .arg(static_cast<int>(interaction_icons->dragDropMode()))
                .arg(locked_accepts ? "on" : "off")
                .arg(locked_vp_accepts ? "on" : "off"));
  }

  constexpr Qt::DropActions all_actions = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
  const Qt::DropAction fallback_copy = pakfu::ui::resolve_requested_drop_action(
    Qt::MoveAction, Qt::MoveAction, Qt::CopyAction, Qt::NoModifier);
  add_check(&checks,
            "Drop-action fallback to supported action",
            fallback_copy == Qt::CopyAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(fallback_copy))
              .arg(static_cast<int>(Qt::CopyAction)));

  const Qt::DropAction default_move = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::MoveAction, all_actions, Qt::NoModifier);
  add_check(&checks,
            "Drop-action default proposal honored",
            default_move == Qt::MoveAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(default_move))
              .arg(static_cast<int>(Qt::MoveAction)));

#if defined(Q_OS_MACOS)
  const Qt::DropAction option_copy = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::MoveAction, all_actions, Qt::AltModifier);
  add_check(&checks,
            "macOS modifier: Option forces copy",
            option_copy == Qt::CopyAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(option_copy))
              .arg(static_cast<int>(Qt::CopyAction)));

  const Qt::DropAction shift_move = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::CopyAction, all_actions, Qt::ShiftModifier);
  add_check(&checks,
            "macOS modifier: Shift requests move",
            shift_move == Qt::MoveAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(shift_move))
              .arg(static_cast<int>(Qt::MoveAction)));

  const Qt::DropAction ctrl_option_link = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::CopyAction, all_actions, Qt::ControlModifier | Qt::AltModifier);
  add_check(&checks,
            "macOS modifier: Control+Option requests link",
            ctrl_option_link == Qt::LinkAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(ctrl_option_link))
              .arg(static_cast<int>(Qt::LinkAction)));
#else
  const Qt::DropAction ctrl_copy = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::MoveAction, all_actions, Qt::ControlModifier);
  add_check(&checks,
            "Modifier: Ctrl forces copy",
            ctrl_copy == Qt::CopyAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(ctrl_copy))
              .arg(static_cast<int>(Qt::CopyAction)));

  const Qt::DropAction shift_move = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::CopyAction, all_actions, Qt::ShiftModifier);
  add_check(&checks,
            "Modifier: Shift requests move",
            shift_move == Qt::MoveAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(shift_move))
              .arg(static_cast<int>(Qt::MoveAction)));

  const Qt::DropAction ctrl_shift_link = pakfu::ui::resolve_requested_drop_action(
    Qt::IgnoreAction, Qt::CopyAction, all_actions, Qt::ControlModifier | Qt::ShiftModifier);
  add_check(&checks,
            "Modifier: Ctrl+Shift requests link",
            ctrl_shift_link == Qt::LinkAction,
            QString("resolved=%1 expected=%2")
              .arg(static_cast<int>(ctrl_shift_link))
              .arg(static_cast<int>(Qt::LinkAction)));
#endif

  int failed = 0;
  for (const QaCheck& check : checks) {
    out << (check.passed ? "[PASS] " : "[FAIL] ") << check.name << ": " << check.detail << "\n";
    if (!check.passed) {
      ++failed;
    }
  }

  if (failed > 0) {
    err << "Practical QA failed: " << failed << " check(s) failed.\n";
    return 1;
  }

  out << "Practical QA passed: " << checks.size() << " check(s).\n";
  return 0;
}
