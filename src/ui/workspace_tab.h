#pragma once

#include <functional>

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "game/game_set.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QStandardItemModel;
class QTableView;

class WorkspaceTab : public QWidget {
public:
  struct ArchiveSummary {
    QString title;
    QString archive_path;
    QString format;
    QString current_prefix;
    int entry_count = 0;
    int added_count = 0;
    int deleted_count = 0;
    bool loaded = false;
    bool dirty = false;
    bool protected_archive = false;
  };

  struct Capability {
    QString area;
    QString status;
    QString details;
    bool ok = true;
  };

  struct SearchResult {
    QString archive_title;
    QString archive_path;
    QString item_path;
    QString scope;
    quint32 size = 0;
    bool is_dir = false;
    bool is_added = false;
    bool is_overridden = false;
  };

  struct State {
    GameSet active_install;
    QVector<GameSet> installations;
    QVector<ArchiveSummary> archives;
    QStringList recent_files;
    QVector<Capability> capabilities;
    QStringList dependency_notes;
    QStringList validation_issues;
  };

  struct Callbacks {
    std::function<void()> new_archive;
    std::function<void()> open_file;
    std::function<void()> open_archive;
    std::function<void()> open_folder;
    std::function<void()> manage_installations;
    std::function<void()> refresh;
    std::function<void(const QString&)> focus_archive;
    std::function<void(const QString&)> search;
  };

  explicit WorkspaceTab(Callbacks callbacks, QWidget* parent = nullptr);

  QString search_query() const;
  void focus_search(const QString& query = QString());
  void set_state(const State& state);
  void set_search_results(const QVector<SearchResult>& results);

private:
  void build_ui();
  void add_section(const QString& label, QWidget* page);
  void run_search();
  void populate_overview();
  void populate_installations();
  void populate_search_results();
  void populate_changes();
  void populate_dependencies();
  void populate_validation();
  void populate_capabilities();

  Callbacks callbacks_;
  State state_;
  QVector<SearchResult> search_results_;

  QLabel* install_label_ = nullptr;
  QLabel* install_detail_label_ = nullptr;
  QLabel* archive_count_label_ = nullptr;
  QLabel* recent_count_label_ = nullptr;
  QListWidget* section_nav_ = nullptr;
  QStackedWidget* sections_ = nullptr;
  QTableView* archives_table_ = nullptr;
  QStandardItemModel* archives_model_ = nullptr;
  QTableView* recent_table_ = nullptr;
  QStandardItemModel* recent_model_ = nullptr;
  QTableView* installations_table_ = nullptr;
  QStandardItemModel* installations_model_ = nullptr;
  QLineEdit* search_edit_ = nullptr;
  QTableView* search_results_table_ = nullptr;
  QStandardItemModel* search_results_model_ = nullptr;
  QTableView* changes_table_ = nullptr;
  QStandardItemModel* changes_model_ = nullptr;
  QTableView* dependencies_table_ = nullptr;
  QStandardItemModel* dependencies_model_ = nullptr;
  QTableView* validation_table_ = nullptr;
  QStandardItemModel* validation_model_ = nullptr;
  QTableView* capabilities_table_ = nullptr;
  QStandardItemModel* capabilities_model_ = nullptr;
};
