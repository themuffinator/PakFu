#include "ui/drag_drop_policy.h"

namespace pakfu::ui {

Qt::DropAction resolve_requested_drop_action(Qt::DropAction drop_action,
                                             Qt::DropAction proposed_action,
                                             Qt::DropActions possible_actions,
                                             Qt::KeyboardModifiers modifiers) {
  auto ensure_supported = [possible_actions](Qt::DropAction wanted) -> Qt::DropAction {
    if (wanted != Qt::IgnoreAction && (possible_actions & wanted)) {
      return wanted;
    }
    if (possible_actions & Qt::CopyAction) {
      return Qt::CopyAction;
    }
    if (possible_actions & Qt::MoveAction) {
      return Qt::MoveAction;
    }
    if (possible_actions & Qt::LinkAction) {
      return Qt::LinkAction;
    }
    return Qt::IgnoreAction;
  };

  Qt::DropAction chosen = (drop_action != Qt::IgnoreAction) ? drop_action : proposed_action;
  Qt::DropAction modifier_action = Qt::IgnoreAction;
#if defined(Q_OS_MACOS)
  // macOS convention: Option copies, Shift requests move.
  if ((modifiers & Qt::AltModifier) && (modifiers & Qt::ControlModifier)) {
    modifier_action = Qt::LinkAction;
  } else if (modifiers & Qt::AltModifier) {
    modifier_action = Qt::CopyAction;
  } else if (modifiers & Qt::ShiftModifier) {
    modifier_action = Qt::MoveAction;
  }
#else
  if ((modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier)) {
    modifier_action = Qt::LinkAction;
  } else if (modifiers & Qt::ShiftModifier) {
    modifier_action = Qt::MoveAction;
  } else if (modifiers & Qt::ControlModifier) {
    modifier_action = Qt::CopyAction;
  }
#endif
  if (modifier_action != Qt::IgnoreAction) {
    chosen = modifier_action;
  }

  return ensure_supported(chosen);
}

}  // namespace pakfu::ui
