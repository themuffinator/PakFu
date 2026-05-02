#include "ui/game_set_dialog.h"

#include <algorithm>

#include <QUuid>

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QModelIndex>
#include <QPushButton>
#include <QStringList>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "game/game_auto_detect.h"
#include "ui/game_set_editor_dialog.h"
#include "ui/ui_icons.h"

namespace {
QString detail_tooltip_for(const GameSet& set) {
  QString tip;
  tip += QString("<b>%1</b><br/>").arg(set.name.toHtmlEscaped());
  tip += QString("Game: %1<br/>").arg(game_display_name(set.game).toHtmlEscaped());
  if (!set.root_dir.isEmpty()) {
    tip += QString("Root: %1<br/>").arg(QFileInfo(set.root_dir).absoluteFilePath().toHtmlEscaped());
  }
  if (!set.default_dir.isEmpty()) {
    tip += QString("Default: %1<br/>").arg(QFileInfo(set.default_dir).absoluteFilePath().toHtmlEscaped());
  }
  if (!set.launch.executable_path.isEmpty()) {
    tip += QString("Launch: %1<br/>").arg(QFileInfo(set.launch.executable_path).absoluteFilePath().toHtmlEscaped());
  }
  return tip;
}

QString new_uid() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

GameSet make_new_game_set_template() {
  GameSet set;
  set.uid = new_uid();
  set.game = GameId::Quake;
  set.name = game_display_name(set.game);
  set.palette_id = default_palette_for_game(set.game);
  return set;
}

QString installation_primary_label(const GameSet& set) {
  return set.name.isEmpty() ? game_display_name(set.game) : set.name;
}

QString installation_list_label(const GameSet& set) {
  const QString primary = installation_primary_label(set);
  if (primary != game_display_name(set.game)) {
    return QString("%1 — %2").arg(primary, game_display_name(set.game));
  }
  return primary;
}

QString detail_accessible_description_for(const GameSet& set) {
  QStringList parts;
  parts.push_back(installation_list_label(set));
  parts.push_back(QString("Game: %1").arg(game_display_name(set.game)));
  if (!set.root_dir.isEmpty()) {
    parts.push_back(QString("Root: %1").arg(QFileInfo(set.root_dir).absoluteFilePath()));
  }
  if (!set.default_dir.isEmpty()) {
    parts.push_back(QString("Default: %1").arg(QFileInfo(set.default_dir).absoluteFilePath()));
  }
  if (!set.launch.executable_path.isEmpty()) {
    parts.push_back(QString("Launch: %1").arg(QFileInfo(set.launch.executable_path).absoluteFilePath()));
  }
  return parts.join(QStringLiteral(". "));
}

bool installation_less(const GameSet* a, const GameSet* b) {
  if (!a || !b) {
    return a != nullptr;
  }
  const QString a_primary = installation_primary_label(*a);
  const QString b_primary = installation_primary_label(*b);
  const int by_primary = QString::compare(a_primary, b_primary, Qt::CaseInsensitive);
  if (by_primary != 0) {
    return by_primary < 0;
  }

  const QString a_game = game_display_name(a->game);
  const QString b_game = game_display_name(b->game);
  const int by_game = QString::compare(a_game, b_game, Qt::CaseInsensitive);
  if (by_game != 0) {
    return by_game < 0;
  }

  return QString::compare(a->uid, b->uid, Qt::CaseInsensitive) < 0;
}
}  // namespace

GameSetDialog::GameSetDialog(QWidget* parent) : QDialog(parent) {
  build_ui();
  load_state();
  refresh_list();
  update_ui_state();
}

void GameSetDialog::build_ui() {
  setModal(true);
  setWindowTitle(tr("Installations"));
  resize(760, 520);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(12);

  auto* title = new QLabel(tr("Choose an Installation"), this);
  title->setAccessibleName(tr("Installation chooser title"));
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 6);
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  hint_label_ = new QLabel(
    tr("Installations hold per-game defaults (directories, palettes, launch settings). "
       "Add one, or auto-detect installs (Steam, then GOG.com, then EOS), then select a game to continue."),
    this);
  hint_label_->setWordWrap(true);
  hint_label_->setStyleSheet("color: rgba(180, 180, 180, 220);");
  hint_label_->setAccessibleName(tr("Installation chooser status"));
  hint_label_->setAccessibleDescription(hint_label_->text());
  layout->addWidget(hint_label_);

  list_model_ = new QStandardItemModel(this);

  list_ = new QListView(this);
  list_->setModel(list_model_);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setAlternatingRowColors(true);
  list_->setUniformItemSizes(true);
  list_->setAccessibleName(tr("Installations"));
  list_->setAccessibleDescription(tr("Choose the game installation to open."));
  layout->addWidget(list_, 1);

  auto* action_bar = new QWidget(this);
  action_bar->setAccessibleName(tr("Installation actions"));
  action_bar->setAccessibleDescription(tr("Actions for adding, editing, removing, detecting, and opening installations."));
  auto* row = new QHBoxLayout(action_bar);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  add_button_ = new QPushButton(tr("Add…"), action_bar);
  configure_button_ = new QPushButton(tr("Configure…"), action_bar);
  remove_button_ = new QPushButton(tr("Remove"), action_bar);
  auto_detect_button_ = new QPushButton(tr("Auto-detect"), action_bar);
  add_button_->setIcon(UiIcons::icon(UiIcons::Id::AddFiles, add_button_->style()));
  configure_button_->setIcon(UiIcons::icon(UiIcons::Id::Configure, configure_button_->style()));
  remove_button_->setIcon(UiIcons::icon(UiIcons::Id::DeleteItem, remove_button_->style()));
  auto_detect_button_->setIcon(UiIcons::icon(UiIcons::Id::AutoDetect, auto_detect_button_->style()));
  add_button_->setAccessibleDescription(tr("Add a new game installation."));
  configure_button_->setAccessibleDescription(tr("Edit the selected game installation."));
  remove_button_->setAccessibleDescription(tr("Remove the selected game installation."));
  auto_detect_button_->setAccessibleDescription(tr("Search common storefront install locations for supported games."));

  row->addWidget(add_button_);
  row->addWidget(configure_button_);
  row->addWidget(remove_button_);
  row->addSpacing(12);
  row->addWidget(auto_detect_button_);
  row->addStretch();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, action_bar);
  open_button_ = buttons->button(QDialogButtonBox::Open);
  if (open_button_) {
    open_button_->setText(tr("Open"));
    open_button_->setIcon(UiIcons::icon(UiIcons::Id::OpenFolder, open_button_->style()));
    open_button_->setAccessibleDescription(tr("Open the selected installation."));
  }
  if (QPushButton* cancel_button = buttons->button(QDialogButtonBox::Cancel)) {
    cancel_button->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, cancel_button->style()));
    cancel_button->setAccessibleDescription(tr("Close without opening an installation."));
  }
  row->addWidget(buttons);

  layout->addWidget(action_bar);

  connect(list_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { update_ui_state(); });
  connect(list_, &QListView::doubleClicked, this, [this](const QModelIndex&) { open_selected(); });
  connect(add_button_, &QPushButton::clicked, this, [this]() { add_game_set(); });
  connect(configure_button_, &QPushButton::clicked, this, [this]() { configure_game_set(); });
  connect(remove_button_, &QPushButton::clicked, this, [this]() { remove_game_set(); });
  connect(auto_detect_button_, &QPushButton::clicked, this, [this]() { auto_detect(); });
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() { open_selected(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void GameSetDialog::load_state() {
  QString err;
  state_ = load_game_set_state(&err);
  if (!err.isEmpty()) {
    QMessageBox::warning(this, tr("Installations"), err);
  }
}

void GameSetDialog::save_state_or_warn() {
  QString err;
  if (!save_game_set_state(state_, &err)) {
    QMessageBox::warning(this, tr("Installations"), err.isEmpty() ? tr("Failed to save installations.") : err);
  }
}

void GameSetDialog::refresh_list() {
  if (!list_ || !list_model_) {
    return;
  }

  const QString restore_uid = state_.selected_uid.isEmpty() ? selected_uid() : state_.selected_uid;
  list_model_->clear();

  QVector<const GameSet*> sorted;
  sorted.reserve(state_.sets.size());
  for (const GameSet& set : state_.sets) {
    sorted.push_back(&set);
  }
  std::sort(sorted.begin(), sorted.end(), installation_less);

  for (const GameSet* set : sorted) {
    if (!set) {
      continue;
    }
    auto* item = new QStandardItem(installation_list_label(*set));
    item->setEditable(false);
    item->setData(set->uid, Qt::UserRole);
    item->setToolTip(detail_tooltip_for(*set));
    item->setData(detail_accessible_description_for(*set), Qt::AccessibleDescriptionRole);
    list_model_->appendRow(item);
  }

  if (!restore_uid.isEmpty()) {
    for (int row = 0; row < list_model_->rowCount(); ++row) {
      const QModelIndex index = list_model_->index(row, 0);
      if (index.data(Qt::UserRole).toString() == restore_uid) {
        list_->setCurrentIndex(index);
        break;
      }
    }
  }
}

QString GameSetDialog::selected_uid() const {
  if (!list_) {
    return {};
  }
  const QModelIndex index = list_->currentIndex();
  if (!index.isValid()) {
    return {};
  }
  return index.data(Qt::UserRole).toString();
}

GameSet* GameSetDialog::selected_set() {
  const QString uid = selected_uid();
  if (uid.isEmpty()) {
    return nullptr;
  }
  return find_game_set(state_, uid);
}

void GameSetDialog::update_ui_state() {
  const bool has_selection = selected_set() != nullptr;
  if (configure_button_) {
    configure_button_->setEnabled(has_selection);
  }
  if (remove_button_) {
    remove_button_->setEnabled(has_selection);
  }
  if (open_button_) {
    open_button_->setEnabled(has_selection);
  }
}

void GameSetDialog::add_game_set() {
  GameSet set = make_new_game_set_template();
  GameSetEditorDialog editor(set, this);
  editor.setWindowTitle(tr("Add Installation"));
  if (editor.exec() != QDialog::Accepted) {
    return;
  }

  const GameSet edited = editor.edited_game_set();
  state_.sets.push_back(edited);
  state_.selected_uid = edited.uid;
  save_state_or_warn();
  refresh_list();
  update_ui_state();
}

void GameSetDialog::configure_game_set() {
  GameSet* current = selected_set();
  if (!current) {
    return;
  }
  GameSetEditorDialog editor(*current, this);
  editor.setWindowTitle(tr("Configure Installation"));
  if (editor.exec() != QDialog::Accepted) {
    return;
  }
  *current = editor.edited_game_set();
  save_state_or_warn();
  refresh_list();
  update_ui_state();
}

void GameSetDialog::remove_game_set() {
  const QString uid = selected_uid();
  if (uid.isEmpty()) {
    return;
  }

  const GameSet* set = find_game_set(state_, uid);
  const QString name = set ? set->name : QString();
  const auto reply =
    QMessageBox::question(this, tr("Remove Installation"),
                          name.isEmpty() ? tr("Remove selected installation?") : tr("Remove \"%1\"?").arg(name));
  if (reply != QMessageBox::Yes) {
    return;
  }

  for (int i = state_.sets.size() - 1; i >= 0; --i) {
    if (state_.sets[i].uid == uid) {
      state_.sets.removeAt(i);
      break;
    }
  }

  if (state_.selected_uid == uid) {
    state_.selected_uid.clear();
  }

  save_state_or_warn();
  refresh_list();
  update_ui_state();
}

void GameSetDialog::auto_detect() {
  const GameAutoDetectResult detected = auto_detect_supported_games();

  int added = 0;
  int updated = 0;
  for (const DetectedGameInstall& install : detected.installs) {
    GameSet* existing = nullptr;
    for (GameSet& set : state_.sets) {
      if (set.game == install.game) {
        existing = &set;
        break;
      }
    }

    if (existing) {
      existing->root_dir = install.root_dir;
      existing->default_dir = install.default_dir;
      if (!install.launch.executable_path.isEmpty()) {
        existing->launch.executable_path = install.launch.executable_path;
      }
      if (!install.launch.working_dir.isEmpty()) {
        existing->launch.working_dir = install.launch.working_dir;
      }
      if (existing->palette_id.isEmpty()) {
        existing->palette_id = default_palette_for_game(existing->game);
      }
      if (existing->name.isEmpty()) {
        existing->name = game_display_name(existing->game);
      }
      ++updated;
      continue;
    }

    GameSet set;
    set.uid = new_uid();
    set.game = install.game;
    set.name = game_display_name(set.game);
    set.root_dir = install.root_dir;
    set.default_dir = install.default_dir;
    set.palette_id = default_palette_for_game(set.game);
    set.launch = install.launch;
    state_.sets.push_back(set);
    ++added;
  }

  if (state_.selected_uid.isEmpty() && !state_.sets.isEmpty()) {
    state_.selected_uid = state_.sets.first().uid;
  }

  if (added == 0 && updated == 0) {
    if (hint_label_) {
      const QString log = detected.log.join("\n");
      hint_label_->setText(log.isEmpty() ? tr("No supported games were detected.")
                                         : tr("No supported games were detected. See the tooltip for scan details."));
      hint_label_->setToolTip(log);
      hint_label_->setAccessibleDescription(log.isEmpty() ? hint_label_->text()
                                                          : hint_label_->text() + QStringLiteral(" ") + log);
    }
    return;
  }

  save_state_or_warn();
  refresh_list();
  update_ui_state();

  if (hint_label_) {
    hint_label_->setText(tr("Auto-detect found %1 game(s). Updated %2 existing set(s).")
                           .arg(added + updated)
                           .arg(updated));
    hint_label_->setToolTip(detected.log.join("\n"));
    hint_label_->setAccessibleDescription(hint_label_->text());
  }
}

void GameSetDialog::open_selected() {
  const QString uid = selected_uid();
  if (uid.isEmpty()) {
    return;
  }

  state_.selected_uid = uid;
  save_state_or_warn();
  accept();
}

std::optional<GameSet> GameSetDialog::selected_game_set() const {
  const GameSet* set = find_game_set(state_, state_.selected_uid);
  if (!set) {
    return std::nullopt;
  }
  return *set;
}
