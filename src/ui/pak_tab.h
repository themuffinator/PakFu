#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QScopedPointer>
#include <QSet>
#include <QVector>
#include <QPair>
#include <QThreadPool>
#include <QWidget>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>
#include <vector>

#include "archive/archive.h"
#include "archive/archive_session.h"
#include "archive/archive_search_index.h"
#include "game/game_set.h"
#include "ui/background_task_coordinator.h"
#include "ui/preview_io_cache.h"

class BreadcrumbBar;
class QAction;
class QActionGroup;
class QListView;
class QLineEdit;
class PreviewPane;
enum class PreviewRenderer;
class QAbstractTableModel;
class QSortFilterProxyModel;
class QStackedWidget;
class QSplitter;
class QToolBar;
class QToolButton;
class QTreeView;
class QUndoStack;
class QMimeData;
class QProgressDialog;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QTimer;
struct ExtensionCommand;
struct ExtensionImportEntry;
struct ExtensionRunResult;

class PakTabDetailsView;
class PakTabIconView;
class PakTabStateCommand;

class PakTab : public QWidget {
  Q_OBJECT
  friend class PakTabDetailsView;
  friend class PakTabIconView;
  friend class PakTabStateCommand;

public:
  enum class Mode {
    NewPak,
    ExistingPak,
  };

  enum class ViewMode {
    Auto,
    Details,
    List,
    SmallIcons,
    LargeIcons,
    Gallery,
  };

  explicit PakTab(Mode mode, const QString& pak_path, QWidget* parent = nullptr);
  ~PakTab() override;

  void set_default_directory(const QString& path) { default_directory_ = path; }
  QString default_directory() const { return default_directory_; }

  void set_model_texture_smoothing(bool enabled);
  void set_image_texture_smoothing(bool enabled);
  void set_preview_renderer(PreviewRenderer renderer);
  void set_3d_fov_degrees(int degrees);
  void apply_preferences_from_settings();
  void set_game_id(GameId id);
  void set_pure_pak_protector(bool enabled, bool is_official);
  bool is_editable() const;
  bool is_pure_protected() const;

  QString pak_path() const { return pak_path_; }
  bool is_loaded() const { return loaded_; }
  Archive::Format archive_format() const { return archive_.format(); }
  QString load_error() const { return load_error_; }
  bool is_dirty() const { return dirty_; }
  int archive_entry_count() const;
  int added_file_count() const { return added_files_.size(); }
  int deleted_file_count() const { return deleted_files_.size(); }
  int deleted_dir_count() const { return deleted_dir_prefixes_.size(); }
  QVector<ArchiveSearchIndex::Item> search_workspace(const QString& query, int max_results = 1000) const;
  QStringList workspace_dependency_hints(int max_items = 64) const;
  QStringList workspace_validation_issues() const;
  bool save(QString* error);
  QString current_prefix() const;
  QString selected_pak_path(bool* is_dir) const;
  void restore_workspace(const QString& dir_prefix, const QString& selected_path);
  struct SaveOptions {
    // When saving as a ZIP-based archive, optionally encrypt using Quake Live Beta PK3 XOR.
    bool quakelive_encrypt_pk3 = false;
    // Force output format (default: inferred from destination path).
    Archive::Format format = Archive::Format::Unknown;
  };
  bool save_as(const QString& dest_path, const SaveOptions& options, QString* error);
  QUndoStack* undo_stack() const;

  // High-level UI actions (used by menus/shortcuts).
  void cut();
  void copy();
  void paste();
  void rename();
  void extract_selected();
  void extract_all();
  void convert_selected_assets();
  [[nodiscard]] bool can_extract_all() const;
  void undo();
  void redo();
  [[nodiscard]] bool can_execute_extension_command(const ExtensionCommand& command, QString* error) const;
  bool execute_extension_command(const ExtensionCommand& command, ExtensionRunResult* result, QString* error);
  // Drop helpers used by tab views and main-window tab-bar integration.
  bool can_accept_mime(const QMimeData* mime) const;
  bool handle_drop_event(QDropEvent* event, const QString& dest_prefix);

  struct AddedFile {
    QString pak_name;
    QString source_path;
    quint32 size = 0;
    qint64 mtime_utc_secs = -1;
  };

signals:
  void dirty_changed(bool dirty);
  void status_message(const QString& message, int timeout_ms);

protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  enum class CollisionChoice {
    Overwrite,
    KeepBoth,
    Skip,
    Cancel,
  };

  void build_ui();
  void load_archive();
  void set_current_dir(const QStringList& parts);
  void select_path(const QString& pak_path);
  void refresh_listing();
  void update_preview();
  void update_search_index(qint64 fallback_mtime_utc_secs);
  [[nodiscard]] bool is_search_active() const;
	void select_adjacent_audio(int delta);
	void select_adjacent_video(int delta);
  bool mount_wad_from_selected_file(const QString& pak_path, QString* error);
  void unmount_wad();
  using MountedArchiveLayer = ArchiveSession::MountedArchiveLayer;
  [[nodiscard]] bool is_wad_mounted() const { return archive_session_.has_mounted_archive(); }
  [[nodiscard]] const Archive& view_archive() const {
    return archive_session_.current_archive();
  }
  [[nodiscard]] Archive& view_archive_mut() {
    return archive_session_.current_archive();
  }
  bool ensure_quake1_palette(QString* error);
  bool ensure_quake2_palette(QString* error);
  SaveOptions default_save_options_for_current_path() const;
  bool write_archive_file(const QString& dest_path, const SaveOptions& options, QString* error);

  void setup_actions();
  void toggle_preview_detached();
  void detach_preview();
  void dock_preview();
  void update_preview_detach_action();
  void show_context_menu(QWidget* view, const QPoint& pos);
  void add_files();
  void add_folder();
  void new_folder();
  void delete_selected(bool skip_confirmation);
  void copy_selected(bool cut);
  void paste_from_clipboard();
  bool try_copy_shader_selection_to_clipboard();
  bool try_paste_shader_blocks_from_clipboard();
  void rename_selected();
  bool add_folder_from_path(const QString& folder_path,
                            const QString& dest_prefix,
                            const QString& forced_folder_name,
                            QStringList* failures,
                            QProgressDialog* progress = nullptr);
  bool import_urls(const QList<QUrl>& urls,
                   const QString& dest_prefix,
                   QVector<bool>* imported,
                   QStringList* failures,
                   QProgressDialog* progress = nullptr);
  bool import_urls_with_undo(const QList<QUrl>& urls,
                             const QString& dest_prefix,
                             const QString& label,
                             const QVector<QPair<QString, bool>>& cut_items = {},
                             bool is_cut = false,
                             QVector<bool>* imported_out = nullptr);
  QMimeData* make_mime_data_for_items(const QVector<QPair<QString, bool>>& items,
                                      bool cut,
                                      QStringList* failures,
                                      QProgressDialog* progress = nullptr);
  bool add_file_mapping(const QString& pak_name, const QString& source_path, QString* error);
  bool apply_extension_imports(const QVector<ExtensionImportEntry>& imports, QString* error);
  bool apply_external_move_deletions(const QVector<QPair<QString, bool>>& raw_items, QString* error);
  void reset_collision_prompt_state();
  CollisionChoice choose_collision_action(const QString& pak_path, bool is_dir);
  bool file_exists_in_current_state(const QString& pak_path) const;
  bool dir_exists_in_current_state(const QString& dir_prefix) const;
  QString unique_file_copy_path(const QString& pak_path) const;
  QString unique_dir_copy_prefix(const QString& dir_prefix) const;
  bool write_pak_file(const QString& dest_path, QString* error);
  bool write_wad2_file(const QString& dest_path, QString* error);
  bool write_zip_file(const QString& dest_path, bool quakelive_encrypt_pk3, QString* error);
  void set_dirty(bool dirty);
  bool ensure_editable(const QString& action);

  void set_view_mode(ViewMode mode);
  void apply_auto_view(int file_count, int image_count, int video_count, int model_count, int bsp_count);
  void update_view_controls();
  void configure_icon_view();
  void update_drag_drop_interaction_state();
  void stop_thumbnail_generation();
  void queue_thumbnail(const QString& pak_path, const QString& leaf, const QString& source_path, qint64 size, const QSize& icon_size);
  void register_sprite_icon_animation(const QString& pak_path,
                                      const QVector<QImage>& frames,
                                      const QVector<int>& frame_durations_ms,
                                      const QSize& icon_size);
  void clear_sprite_icon_animations();
  void advance_sprite_icon_animations();

  QVector<QPair<QString, bool>> selected_items() const;
  void rebuild_added_index();
  void remove_added_file_by_name(const QString& pak_name);
  bool is_deleted_path(const QString& pak_name) const;
  void clear_deletions_under(const QString& pak_name);
  QString ensure_export_root();
  bool export_path_to_temp(const QString& pak_path, bool is_dir, QString* out_fs_path, QString* error);
  bool export_path_to_temp_cached(const QString& pak_path,
                                  bool is_dir,
                                  qint64 size,
                                  qint64 mtime_utc_secs,
                                  QString* out_fs_path,
                                  QString* error,
                                  bool* cache_hit = nullptr);
  QString preview_export_cache_key(const QString& pak_path, bool is_dir) const;
  void clear_preview_temp_cache();
  QString preview_cache_scope() const;
  bool read_entry_bytes_cached(const QString& pak_path,
                               qint64 size,
                               qint64 mtime_utc_secs,
                               qint64 max_bytes,
                               QByteArray* out,
                               QString* error,
                               bool* cache_hit = nullptr);
  bool export_dir_prefix_to_fs(const QString& dir_prefix, const QString& dest_dir, QString* error);
  bool open_entry_with_associated_app(const QString& pak_path, const QString& display_name);
  void activate_entry(const QString& item_name, bool is_dir, const QString& pak_path);

  void enter_directory_path(const QString& pak_path);
  void enter_directory(const QString& name);
  void activate_crumb(int index);

  Mode mode_;
  QString default_directory_;
  QString pak_path_;
  bool loaded_ = false;
  QString load_error_;

  BreadcrumbBar* breadcrumbs_ = nullptr;
  QToolBar* toolbar_ = nullptr;
  QToolButton* view_button_ = nullptr;
  QActionGroup* view_group_ = nullptr;
  QAction* add_files_action_ = nullptr;
  QAction* add_folder_action_ = nullptr;
  QAction* new_folder_action_ = nullptr;
  QAction* delete_action_ = nullptr;
  QLineEdit* search_edit_ = nullptr;

  QAction* view_auto_action_ = nullptr;
  QAction* view_details_action_ = nullptr;
  QAction* view_list_action_ = nullptr;
  QAction* view_small_icons_action_ = nullptr;
  QAction* view_large_icons_action_ = nullptr;
  QAction* view_gallery_action_ = nullptr;

  QSplitter* splitter_ = nullptr;
  QStackedWidget* view_stack_ = nullptr;
  QAbstractTableModel* listing_model_ = nullptr;
  QSortFilterProxyModel* details_proxy_ = nullptr;
  QSortFilterProxyModel* icon_proxy_ = nullptr;
  QTreeView* details_view_ = nullptr;
  QListView* icon_view_ = nullptr;
  PreviewPane* preview_ = nullptr;
  QWidget* preview_placeholder_ = nullptr;
  QPointer<QWidget> detached_preview_window_;
  QAction* detach_preview_action_ = nullptr;
  struct SpriteIconAnimation {
    QVector<QIcon> icon_frames;
    QVector<int> frame_durations_ms;
    int frame_index = 0;
    int elapsed_ms = 0;
  };
  QHash<QString, SpriteIconAnimation> sprite_icon_animations_;
  QTimer* sprite_icon_timer_ = nullptr;
  QThreadPool thumbnail_pool_;
  BackgroundTaskCoordinator thumbnail_tasks_;
  QUndoStack* undo_stack_ = nullptr;
  QScopedPointer<QTemporaryDir> export_temp_dir_;
  int export_seq_ = 1;
  PreviewIoCache preview_io_cache_;
  QStringList current_dir_;
  ArchiveSession archive_session_;
  Archive& archive_;
  std::vector<MountedArchiveLayer>& mounted_archives_;
  QVector<AddedFile> added_files_;
  QHash<QString, int> added_index_by_name_;
  QSet<QString> virtual_dirs_;
  QSet<QString> deleted_files_;
  QSet<QString> deleted_dir_prefixes_;
  ArchiveSearchIndex search_index_;
  QString search_query_;
  ViewMode view_mode_ = ViewMode::Auto;
  ViewMode effective_view_ = ViewMode::Details;
  bool dirty_ = false;
  bool quake1_palette_loaded_ = false;
  QVector<QRgb> quake1_palette_;
  QString quake1_palette_error_;
  QString quake1_palette_source_;
  bool quake2_palette_loaded_ = false;
  QVector<QRgb> quake2_palette_;
  QString quake2_palette_error_;
  QString quake2_palette_source_;
  bool pure_pak_protector_enabled_ = true;
  bool official_archive_ = false;
  bool image_texture_smoothing_ = false;
  GameId game_id_ = GameId::Quake;
  QString drag_source_uid_;
  bool collision_apply_to_remaining_ = false;
  bool collision_choice_is_set_ = false;
  CollisionChoice collision_choice_ = CollisionChoice::Overwrite;
};
