#pragma once

#include <Qt>

namespace pakfu::ui {

Qt::DropAction resolve_requested_drop_action(Qt::DropAction drop_action,
                                             Qt::DropAction proposed_action,
                                             Qt::DropActions possible_actions,
                                             Qt::KeyboardModifiers modifiers);

}  // namespace pakfu::ui
