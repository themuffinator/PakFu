#include "ui/game_set_editor_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/file_dialog_utils.h"
#include "ui/ui_icons.h"

namespace {
struct PaletteEntry {
  QString id;
  QString name;
};

QVector<PaletteEntry> palette_entries() {
  return {
    {"quake", "Quake"},
    {"doom", "DOOM"},
    {"quake2", "Quake II"},
  };
}
}  // namespace

GameSetEditorDialog::GameSetEditorDialog(const GameSet& initial, QWidget* parent)
    : QDialog(parent), edited_(initial) {
  build_ui();
  load_from_set(initial);
}

void GameSetEditorDialog::build_ui() {
  setModal(true);
  setMinimumWidth(560);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(12);

  auto* title = new QLabel(tr("Installation"), this);
  title->setAccessibleName(tr("Installation editor title"));
  QFont title_font = title->font();
  title_font.setPointSize(title_font.pointSize() + 4);
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form->setFormAlignment(Qt::AlignTop);

  game_combo_ = new QComboBox(this);
  game_combo_->setAccessibleName(tr("Game"));
  game_combo_->setAccessibleDescription(tr("Game type for this installation."));
  for (const GameId id : supported_game_ids()) {
    game_combo_->addItem(game_display_name(id), static_cast<int>(id));
  }
  form->addRow(tr("Game"), game_combo_);

  name_edit_ = new QLineEdit(this);
  name_edit_->setPlaceholderText(tr("e.g. Quake"));
  name_edit_->setAccessibleName(tr("Name"));
  name_edit_->setAccessibleDescription(tr("Display name for this installation."));
  form->addRow(tr("Name"), name_edit_);

  root_dir_edit_ = new QLineEdit(this);
  root_dir_edit_->setPlaceholderText(tr("Game install root (optional, but recommended)"));
  root_dir_edit_->setAccessibleName(tr("Root directory"));
  root_dir_edit_->setAccessibleDescription(tr("Game install root directory."));
  auto* root_row = new QWidget(this);
  root_row->setAccessibleName(tr("Root directory row"));
  auto* root_row_layout = new QHBoxLayout(root_row);
  root_row_layout->setContentsMargins(0, 0, 0, 0);
  root_row_layout->setSpacing(8);
  root_row_layout->addWidget(root_dir_edit_, 1);
  auto* root_browse = new QPushButton(tr("Browse…"), root_row);
  root_browse->setIcon(UiIcons::icon(UiIcons::Id::Browse, root_browse->style()));
  root_browse->setAccessibleName(tr("Browse for root directory"));
  root_browse->setAccessibleDescription(tr("Choose the game install root directory."));
  root_row_layout->addWidget(root_browse, 0);
  form->addRow(tr("Root Dir"), root_row);

  default_dir_edit_ = new QLineEdit(this);
  default_dir_edit_->setPlaceholderText(tr("Default directory for file dialogs (optional)"));
  default_dir_edit_->setAccessibleName(tr("Default directory"));
  default_dir_edit_->setAccessibleDescription(tr("Default directory used when opening file dialogs."));
  auto* def_row = new QWidget(this);
  def_row->setAccessibleName(tr("Default directory row"));
  auto* def_row_layout = new QHBoxLayout(def_row);
  def_row_layout->setContentsMargins(0, 0, 0, 0);
  def_row_layout->setSpacing(8);
  def_row_layout->addWidget(default_dir_edit_, 1);
  auto* def_browse = new QPushButton(tr("Browse…"), def_row);
  def_browse->setIcon(UiIcons::icon(UiIcons::Id::Browse, def_browse->style()));
  def_browse->setAccessibleName(tr("Browse for default directory"));
  def_browse->setAccessibleDescription(tr("Choose the default directory for file dialogs."));
  def_row_layout->addWidget(def_browse, 0);
  form->addRow(tr("Default Dir"), def_row);

  palette_combo_ = new QComboBox(this);
  palette_combo_->setAccessibleName(tr("Palette"));
  palette_combo_->setAccessibleDescription(tr("Palette used to preview game assets."));
  for (const PaletteEntry& p : palette_entries()) {
    palette_combo_->addItem(p.name, p.id);
  }
  form->addRow(tr("Palette"), palette_combo_);

  exe_edit_ = new QLineEdit(this);
  exe_edit_->setPlaceholderText(tr("Game executable (optional)"));
  exe_edit_->setAccessibleName(tr("Launch executable"));
  exe_edit_->setAccessibleDescription(tr("Optional executable to launch this game."));
  auto* exe_row = new QWidget(this);
  exe_row->setAccessibleName(tr("Launch executable row"));
  auto* exe_row_layout = new QHBoxLayout(exe_row);
  exe_row_layout->setContentsMargins(0, 0, 0, 0);
  exe_row_layout->setSpacing(8);
  exe_row_layout->addWidget(exe_edit_, 1);
  auto* exe_browse = new QPushButton(tr("Browse…"), exe_row);
  exe_browse->setIcon(UiIcons::icon(UiIcons::Id::Browse, exe_browse->style()));
  exe_browse->setAccessibleName(tr("Browse for launch executable"));
  exe_browse->setAccessibleDescription(tr("Choose the game executable."));
  exe_row_layout->addWidget(exe_browse, 0);
  form->addRow(tr("Launch EXE"), exe_row);

  args_edit_ = new QLineEdit(this);
  args_edit_->setPlaceholderText(tr("Launch arguments (optional)"));
  args_edit_->setAccessibleName(tr("Launch arguments"));
  args_edit_->setAccessibleDescription(tr("Optional command-line arguments for launching this game."));
  form->addRow(tr("Launch Args"), args_edit_);

  working_dir_edit_ = new QLineEdit(this);
  working_dir_edit_->setPlaceholderText(tr("Working directory (optional)"));
  working_dir_edit_->setAccessibleName(tr("Working directory"));
  working_dir_edit_->setAccessibleDescription(tr("Optional working directory used when launching this game."));
  auto* work_row = new QWidget(this);
  work_row->setAccessibleName(tr("Working directory row"));
  auto* work_row_layout = new QHBoxLayout(work_row);
  work_row_layout->setContentsMargins(0, 0, 0, 0);
  work_row_layout->setSpacing(8);
  work_row_layout->addWidget(working_dir_edit_, 1);
  auto* work_browse = new QPushButton(tr("Browse…"), work_row);
  work_browse->setIcon(UiIcons::icon(UiIcons::Id::Browse, work_browse->style()));
  work_browse->setAccessibleName(tr("Browse for working directory"));
  work_browse->setAccessibleDescription(tr("Choose the launch working directory."));
  work_row_layout->addWidget(work_browse, 0);
  form->addRow(tr("Working Dir"), work_row);

  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  if (QPushButton* ok_button = buttons->button(QDialogButtonBox::Ok)) {
    ok_button->setIcon(UiIcons::icon(UiIcons::Id::Configure, ok_button->style()));
    ok_button->setAccessibleDescription(tr("Save this installation."));
  }
  if (QPushButton* cancel_button = buttons->button(QDialogButtonBox::Cancel)) {
    cancel_button->setIcon(UiIcons::icon(UiIcons::Id::ExitApp, cancel_button->style()));
    cancel_button->setAccessibleDescription(tr("Close without saving changes."));
  }
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (validate_and_apply()) {
      accept();
    }
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  connect(game_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
    apply_game_defaults(game_from_combo());
  });
  connect(root_browse, &QPushButton::clicked, this, [this]() { browse_root_dir(); });
  connect(def_browse, &QPushButton::clicked, this, [this]() { browse_default_dir(); });
  connect(exe_browse, &QPushButton::clicked, this, [this]() { browse_executable(); });
  connect(work_browse, &QPushButton::clicked, this, [this]() { browse_working_dir(); });
}

void GameSetEditorDialog::load_from_set(const GameSet& set) {
  if (game_combo_) {
    const int idx = game_combo_->findData(static_cast<int>(set.game));
    if (idx >= 0) {
      game_combo_->setCurrentIndex(idx);
    }
  }
  if (name_edit_) {
    name_edit_->setText(set.name);
  }
  if (root_dir_edit_) {
    root_dir_edit_->setText(set.root_dir);
  }
  if (default_dir_edit_) {
    default_dir_edit_->setText(set.default_dir);
  }
  if (palette_combo_) {
    const QString palette = set.palette_id.isEmpty() ? default_palette_for_game(set.game) : set.palette_id;
    const int idx = palette_combo_->findData(palette);
    if (idx >= 0) {
      palette_combo_->setCurrentIndex(idx);
    }
  }
  if (exe_edit_) {
    exe_edit_->setText(set.launch.executable_path);
  }
  if (args_edit_) {
    args_edit_->setText(set.launch.arguments);
  }
  if (working_dir_edit_) {
    working_dir_edit_->setText(set.launch.working_dir);
  }

  apply_game_defaults(game_from_combo());
}

void GameSetEditorDialog::apply_game_defaults(GameId game) {
  if (name_edit_) {
    if (name_edit_->text().trimmed().isEmpty()) {
      name_edit_->setText(game_display_name(game));
    }
  }

  if (palette_combo_) {
    const QString default_palette = default_palette_for_game(game);
    const int idx = palette_combo_->findData(default_palette);
    if (idx >= 0) {
      palette_combo_->setCurrentIndex(idx);
    }
  }

  if (default_dir_edit_) {
    const QString root_dir = root_dir_edit_ ? root_dir_edit_->text().trimmed() : QString();
    if (!root_dir.isEmpty() && default_dir_edit_->text().trimmed().isEmpty()) {
      default_dir_edit_->setText(suggested_default_dir(game, root_dir));
    }
  }

  if (working_dir_edit_) {
    const QString root_dir = root_dir_edit_ ? root_dir_edit_->text().trimmed() : QString();
    if (!root_dir.isEmpty() && working_dir_edit_->text().trimmed().isEmpty()) {
      working_dir_edit_->setText(root_dir);
    }
  }
}

GameId GameSetEditorDialog::game_from_combo() const {
  if (!game_combo_) {
    return edited_.game;
  }
  const QVariant data = game_combo_->currentData();
  if (!data.isValid()) {
    return edited_.game;
  }
  return static_cast<GameId>(data.toInt());
}

QString GameSetEditorDialog::palette_from_combo() const {
  if (!palette_combo_) {
    return {};
  }
  return palette_combo_->currentData().toString();
}

QString GameSetEditorDialog::suggested_default_dir(GameId game, const QString& root_dir) const {
  const QDir root(root_dir);
  switch (game) {
    case GameId::Quake:
      return root.filePath("id1");
    case GameId::QuakeRerelease:
      if (QFileInfo::exists(root.filePath("rerelease/id1"))) {
        return root.filePath("rerelease/id1");
      }
      return root.filePath("rerelease");
    case GameId::HalfLife:
      if (QFileInfo::exists(root.filePath("valve"))) {
        return root.filePath("valve");
      }
      if (QFileInfo::exists(root.filePath("valve_hd"))) {
        return root.filePath("valve_hd");
      }
      return root.filePath("valve");
    case GameId::Doom:
    case GameId::Doom2:
    case GameId::FinalDoom:
    case GameId::Heretic:
    case GameId::Hexen:
    case GameId::Strife:
      if (QFileInfo::exists(root.filePath("base"))) {
        return root.filePath("base");
      }
      return root_dir;
    case GameId::Quake2:
      return root.filePath("baseq2");
    case GameId::Quake2Rerelease:
      if (QFileInfo::exists(root.filePath("rerelease/baseq2"))) {
        return root.filePath("rerelease/baseq2");
      }
      if (QFileInfo::exists(root.filePath("baseq2"))) {
        return root.filePath("baseq2");
      }
      return root.filePath("rerelease");
    case GameId::Quake2RTX:
      if (QFileInfo::exists(root.filePath("baseq2"))) {
        return root.filePath("baseq2");
      }
      if (QFileInfo::exists(root.filePath("q2rtx"))) {
        return root.filePath("q2rtx");
      }
      return root.filePath("baseq2");
    case GameId::SiNGold:
      if (QFileInfo::exists(root.filePath("sin"))) {
        return root.filePath("sin");
      }
      return root_dir;
    case GameId::KingpinLifeOfCrime:
      if (QFileInfo::exists(root.filePath("main"))) {
        return root.filePath("main");
      }
      return root_dir;
    case GameId::Daikatana:
    case GameId::Anachronox:
      if (QFileInfo::exists(root.filePath("data"))) {
        return root.filePath("data");
      }
      return root_dir;
    case GameId::Heretic2:
      if (QFileInfo::exists(root.filePath("base"))) {
        return root.filePath("base");
      }
      return root_dir;
    case GameId::GravityBone:
      if (QFileInfo::exists(root.filePath("ValveTestApp242720"))) {
        return root.filePath("ValveTestApp242720");
      }
      if (QFileInfo::exists(root.filePath("gravitybone"))) {
        return root.filePath("gravitybone");
      }
      return root_dir;
    case GameId::ThirtyFlightsOfLoving:
      if (QFileInfo::exists(root.filePath("ValveTestApp214700"))) {
        return root.filePath("ValveTestApp214700");
      }
      if (QFileInfo::exists(root.filePath("thirty_flights_of_loving"))) {
        return root.filePath("thirty_flights_of_loving");
      }
      return root_dir;
    case GameId::Quake3Arena:
    case GameId::QuakeLive:
      return root.filePath("baseq3");
    case GameId::ReturnToCastleWolfenstein:
      if (QFileInfo::exists(root.filePath("Main"))) {
        return root.filePath("Main");
      }
      return root.filePath("main");
    case GameId::WolfensteinEnemyTerritory:
      return root.filePath("etmain");
    case GameId::JediOutcast:
    case GameId::JediAcademy:
      if (QFileInfo::exists(root.filePath("GameData/base"))) {
        return root.filePath("GameData/base");
      }
      if (QFileInfo::exists(root.filePath("gamedata/base"))) {
        return root.filePath("gamedata/base");
      }
      return root.filePath("base");
    case GameId::StarTrekVoyagerEliteForce:
      if (QFileInfo::exists(root.filePath("baseEF"))) {
        return root.filePath("baseEF");
      }
      return root.filePath("baseef");
    case GameId::EliteForce2:
      if (QFileInfo::exists(root.filePath("base"))) {
        return root.filePath("base");
      }
      return root_dir;
    case GameId::Warsow:
    case GameId::Warfork:
      if (QFileInfo::exists(root.filePath("basewsw"))) {
        return root.filePath("basewsw");
      }
      return root_dir;
    case GameId::WorldOfPadman:
      if (QFileInfo::exists(root.filePath("wop"))) {
        return root.filePath("wop");
      }
      return root_dir;
    case GameId::HeavyMetalFakk2:
      if (QFileInfo::exists(root.filePath("fakk"))) {
        return root.filePath("fakk");
      }
      return root_dir;
    case GameId::AmericanMcGeesAlice:
      if (QFileInfo::exists(root.filePath("base"))) {
        return root.filePath("base");
      }
      return root_dir;
    case GameId::Quake4:
      return root.filePath("q4base");
    case GameId::Doom3:
    case GameId::Doom3BFGEdition:
    case GameId::Prey:
    case GameId::EnemyTerritoryQuakeWars:
      return root.filePath("base");
  }
  return root_dir;
}

void GameSetEditorDialog::browse_root_dir() {
  const QString initial = root_dir_edit_ ? root_dir_edit_->text().trimmed() : QString();
  QFileDialog dialog(this);
  dialog.setWindowTitle(tr("Choose Game Root Directory"));
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  FileDialogUtils::Options options;
  options.settings_key = "game_set_editor/root_dir";
  options.fallback_directory = initial;

  QStringList selected;
  if (!FileDialogUtils::exec_with_state(&dialog, options, &selected)) {
    return;
  }
  const QString dir = selected.first();
  if (root_dir_edit_) {
    root_dir_edit_->setText(QDir::cleanPath(dir));
  }

  if (default_dir_edit_ && default_dir_edit_->text().trimmed().isEmpty()) {
    default_dir_edit_->setText(suggested_default_dir(game_from_combo(), dir));
  }
  if (working_dir_edit_ && working_dir_edit_->text().trimmed().isEmpty()) {
    working_dir_edit_->setText(QDir::cleanPath(dir));
  }
}

void GameSetEditorDialog::browse_default_dir() {
  const QString initial = default_dir_edit_ ? default_dir_edit_->text().trimmed() : QString();
  QFileDialog dialog(this);
  dialog.setWindowTitle(tr("Choose Default Directory"));
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  FileDialogUtils::Options options;
  options.settings_key = "game_set_editor/default_dir";
  options.fallback_directory = initial;

  QStringList selected;
  if (!FileDialogUtils::exec_with_state(&dialog, options, &selected)) {
    return;
  }
  const QString dir = selected.first();
  if (default_dir_edit_) {
    default_dir_edit_->setText(QDir::cleanPath(dir));
  }
}

void GameSetEditorDialog::browse_executable() {
  const QString initial = exe_edit_ ? exe_edit_->text().trimmed() : QString();
  QFileDialog dialog(this);
  dialog.setWindowTitle(tr("Choose Game Executable"));
  dialog.setFileMode(QFileDialog::ExistingFile);
  FileDialogUtils::Options options;
  options.settings_key = "game_set_editor/executable";
  options.fallback_directory = initial.isEmpty() ? QString() : QFileInfo(initial).absolutePath();
  options.initial_selection = initial;

  QStringList selected;
  if (!FileDialogUtils::exec_with_state(&dialog, options, &selected)) {
    return;
  }
  const QString file = selected.first();
  if (exe_edit_) {
    exe_edit_->setText(QDir::cleanPath(file));
  }
}

void GameSetEditorDialog::browse_working_dir() {
  const QString initial = working_dir_edit_ ? working_dir_edit_->text().trimmed() : QString();
  QFileDialog dialog(this);
  dialog.setWindowTitle(tr("Choose Working Directory"));
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  FileDialogUtils::Options options;
  options.settings_key = "game_set_editor/working_dir";
  options.fallback_directory = initial;

  QStringList selected;
  if (!FileDialogUtils::exec_with_state(&dialog, options, &selected)) {
    return;
  }
  const QString dir = selected.first();
  if (working_dir_edit_) {
    working_dir_edit_->setText(QDir::cleanPath(dir));
  }
}

bool GameSetEditorDialog::validate_and_apply() {
  if (!name_edit_) {
    return false;
  }

  const GameId game = game_from_combo();
  const QString name = name_edit_->text().trimmed();
  const QString root_dir = root_dir_edit_ ? root_dir_edit_->text().trimmed() : QString();
  const QString default_dir = default_dir_edit_ ? default_dir_edit_->text().trimmed() : QString();
  const QString exe = exe_edit_ ? exe_edit_->text().trimmed() : QString();
  const QString args = args_edit_ ? args_edit_->text() : QString();
  const QString working_dir = working_dir_edit_ ? working_dir_edit_->text().trimmed() : QString();
  const QString palette = palette_from_combo();

  if (name.isEmpty()) {
    QMessageBox::warning(this, tr("Installation"), tr("Name cannot be empty."));
    return false;
  }

  const auto validate_dir = [this](const QString& label, const QString& dir) -> bool {
    if (dir.isEmpty()) {
      return true;
    }
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir()) {
      QMessageBox::warning(this, tr("Installation"), tr("%1 is not a valid directory:\n%2").arg(label, dir));
      return false;
    }
    return true;
  };

  const auto validate_file = [this](const QString& label, const QString& file) -> bool {
    if (file.isEmpty()) {
      return true;
    }
    const QFileInfo info(file);
    if (!info.exists() || !info.isFile()) {
      QMessageBox::warning(this, tr("Installation"), tr("%1 is not a valid file:\n%2").arg(label, file));
      return false;
    }
    return true;
  };

  if (!validate_dir(tr("Root Dir"), root_dir)) {
    return false;
  }
  if (!validate_dir(tr("Default Dir"), default_dir)) {
    return false;
  }
  if (!validate_file(tr("Launch EXE"), exe)) {
    return false;
  }
  if (!validate_dir(tr("Working Dir"), working_dir)) {
    return false;
  }

  edited_.game = game;
  edited_.name = name;
  edited_.root_dir = root_dir;
  edited_.default_dir = default_dir;
  edited_.palette_id = palette.isEmpty() ? default_palette_for_game(game) : palette;
  edited_.launch.executable_path = exe;
  edited_.launch.arguments = args.trimmed();
  edited_.launch.working_dir = working_dir.isEmpty() ? root_dir : working_dir;

  return true;
}
