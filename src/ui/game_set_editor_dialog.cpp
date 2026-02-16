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

namespace {
struct PaletteEntry {
  QString id;
  QString name;
};

QVector<PaletteEntry> palette_entries() {
  return {
    {"quake", "Quake"},
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

  auto* title = new QLabel("Installation", this);
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
  for (const GameId id : supported_game_ids()) {
    game_combo_->addItem(game_display_name(id), static_cast<int>(id));
  }
  form->addRow("Game", game_combo_);

  name_edit_ = new QLineEdit(this);
  name_edit_->setPlaceholderText("e.g. Quake");
  form->addRow("Name", name_edit_);

  root_dir_edit_ = new QLineEdit(this);
  root_dir_edit_->setPlaceholderText("Game install root (optional, but recommended)");
  auto* root_row = new QWidget(this);
  auto* root_row_layout = new QHBoxLayout(root_row);
  root_row_layout->setContentsMargins(0, 0, 0, 0);
  root_row_layout->setSpacing(8);
  root_row_layout->addWidget(root_dir_edit_, 1);
  auto* root_browse = new QPushButton("Browse…", root_row);
  root_row_layout->addWidget(root_browse, 0);
  form->addRow("Root Dir", root_row);

  default_dir_edit_ = new QLineEdit(this);
  default_dir_edit_->setPlaceholderText("Default directory for file dialogs (optional)");
  auto* def_row = new QWidget(this);
  auto* def_row_layout = new QHBoxLayout(def_row);
  def_row_layout->setContentsMargins(0, 0, 0, 0);
  def_row_layout->setSpacing(8);
  def_row_layout->addWidget(default_dir_edit_, 1);
  auto* def_browse = new QPushButton("Browse…", def_row);
  def_row_layout->addWidget(def_browse, 0);
  form->addRow("Default Dir", def_row);

  palette_combo_ = new QComboBox(this);
  for (const PaletteEntry& p : palette_entries()) {
    palette_combo_->addItem(p.name, p.id);
  }
  form->addRow("Palette", palette_combo_);

  exe_edit_ = new QLineEdit(this);
  exe_edit_->setPlaceholderText("Game executable (optional)");
  auto* exe_row = new QWidget(this);
  auto* exe_row_layout = new QHBoxLayout(exe_row);
  exe_row_layout->setContentsMargins(0, 0, 0, 0);
  exe_row_layout->setSpacing(8);
  exe_row_layout->addWidget(exe_edit_, 1);
  auto* exe_browse = new QPushButton("Browse…", exe_row);
  exe_row_layout->addWidget(exe_browse, 0);
  form->addRow("Launch EXE", exe_row);

  args_edit_ = new QLineEdit(this);
  args_edit_->setPlaceholderText("Launch arguments (optional)");
  form->addRow("Launch Args", args_edit_);

  working_dir_edit_ = new QLineEdit(this);
  working_dir_edit_->setPlaceholderText("Working directory (optional)");
  auto* work_row = new QWidget(this);
  auto* work_row_layout = new QHBoxLayout(work_row);
  work_row_layout->setContentsMargins(0, 0, 0, 0);
  work_row_layout->setSpacing(8);
  work_row_layout->addWidget(working_dir_edit_, 1);
  auto* work_browse = new QPushButton("Browse…", work_row);
  work_row_layout->addWidget(work_browse, 0);
  form->addRow("Working Dir", work_row);

  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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
  dialog.setWindowTitle("Choose Game Root Directory");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  if (!initial.isEmpty()) {
    dialog.setDirectory(initial);
  }
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
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
  dialog.setWindowTitle("Choose Default Directory");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  if (!initial.isEmpty()) {
    dialog.setDirectory(initial);
  }
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
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
  dialog.setWindowTitle("Choose Game Executable");
  dialog.setFileMode(QFileDialog::ExistingFile);
  if (!initial.isEmpty()) {
    dialog.setDirectory(QFileInfo(initial).absolutePath());
    dialog.selectFile(initial);
  }
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
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
  dialog.setWindowTitle("Choose Working Directory");
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  if (!initial.isEmpty()) {
    dialog.setDirectory(initial);
  }
#if defined(Q_OS_WIN)
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
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
    QMessageBox::warning(this, "Installation", "Name cannot be empty.");
    return false;
  }

  const auto validate_dir = [this](const QString& label, const QString& dir) -> bool {
    if (dir.isEmpty()) {
      return true;
    }
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir()) {
      QMessageBox::warning(this, "Installation", QString("%1 is not a valid directory:\n%2").arg(label, dir));
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
      QMessageBox::warning(this, "Installation", QString("%1 is not a valid file:\n%2").arg(label, file));
      return false;
    }
    return true;
  };

  if (!validate_dir("Root Dir", root_dir)) {
    return false;
  }
  if (!validate_dir("Default Dir", default_dir)) {
    return false;
  }
  if (!validate_file("Launch EXE", exe)) {
    return false;
  }
  if (!validate_dir("Working Dir", working_dir)) {
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
