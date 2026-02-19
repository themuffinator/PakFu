#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QPoint>
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
#include "game/game_set.h"

class BreadcrumbBar;
class QAction;
class QActionGroup;
class QListWidget;
class QListWidgetItem;
class PreviewPane;
enum class PreviewRenderer;
class QStackedWidget;
class QSplitter;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;
class QMimeData;
class QProgressDialog;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QTimer;

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
  void set_game_id(GameId id);
  void set_pure_pak_protector(bool enabled, bool is_official);
  bool is_editable() const;
  bool is_pure_protected() const;

  QString pak_path() const { return pak_path_; }
  bool is_loaded() const { return loaded_; }
  Archive::Format archive_format() const { return archive_.format(); }
  QString load_error() const { return load_error_; }
  bool is_dirty() const { return dirty_; }
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
  void undo();
  void redo();

  struct AddedFile {
    QString pak_name;
    QString source_path;
    quint32 size = 0;
    qint64 mtime_utc_secs = -1;
  };

signals:
  void dirty_changed(bool dirty);

protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void build_ui();
  void load_archive();
  void set_current_dir(const QStringList& parts);
  void select_path(const QString& pak_path);
  void refresh_listing();
  void update_preview();
	void select_adjacent_audio(int delta);
	void select_adjacent_video(int delta);
  bool mount_wad_from_selected_file(const QString& pak_path, QString* error);
  void unmount_wad();
  [[nodiscard]] bool is_wad_mounted() const { return !mounted_archives_.empty(); }
  [[nodiscard]] const Archive& view_archive() const {
    return (is_wad_mounted() && mounted_archives_.back().archive) ? *mounted_archives_.back().archive : archive_;
  }
  [[nodiscard]] Archive& view_archive_mut() {
    return (is_wad_mounted() && mounted_archives_.back().archive) ? *mounted_archives_.back().archive : archive_;
  }
  bool ensure_quake1_palette(QString* error);
  bool ensure_quake2_palette(QString* error);
  SaveOptions default_save_options_for_current_path() const;
  bool write_archive_file(const QString& dest_path, const SaveOptions& options, QString* error);

  void setup_actions();
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
                   QStringList* failures,
                   QProgressDialog* progress = nullptr);
  void import_urls_with_undo(const QList<QUrl>& urls,
                             const QString& dest_prefix,
                             const QString& label,
                             const QVector<QPair<QString, bool>>& cut_items = {},
                             bool is_cut = false);
  QMimeData* make_mime_data_for_items(const QVector<QPair<QString, bool>>& items,
                                      bool cut,
                                      QStringList* failures,
                                      QProgressDialog* progress = nullptr);
  bool add_file_mapping(const QString& pak_name, const QString& source_path, QString* error);
  bool write_pak_file(const QString& dest_path, QString* error);
  bool write_wad2_file(const QString& dest_path, QString* error);
  bool write_zip_file(const QString& dest_path, bool quakelive_encrypt_pk3, QString* error);
  void set_dirty(bool dirty);
  bool ensure_editable(const QString& action);

  void set_view_mode(ViewMode mode);
  void apply_auto_view(int file_count, int image_count, int video_count, int model_count, int bsp_count);
  void update_view_controls();
  void configure_icon_view();
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
  bool can_accept_mime(const QMimeData* mime) const;
  bool handle_drop_event(QDropEvent* event, const QString& dest_prefix);
  QString ensure_export_root();
  bool export_path_to_temp(const QString& pak_path, bool is_dir, QString* out_fs_path, QString* error);
  bool export_dir_prefix_to_fs(const QString& dir_prefix, const QString& dest_dir, QString* error);
  bool open_entry_with_associated_app(const QString& pak_path, const QString& display_name);
  void activate_entry(const QString& item_name, bool is_dir, const QString& pak_path);

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

  QAction* view_auto_action_ = nullptr;
  QAction* view_details_action_ = nullptr;
  QAction* view_list_action_ = nullptr;
  QAction* view_small_icons_action_ = nullptr;
  QAction* view_large_icons_action_ = nullptr;
  QAction* view_gallery_action_ = nullptr;

  QSplitter* splitter_ = nullptr;
  QStackedWidget* view_stack_ = nullptr;
  QTreeWidget* details_view_ = nullptr;
  QListWidget* icon_view_ = nullptr;
  PreviewPane* preview_ = nullptr;
  QHash<QString, QListWidgetItem*> icon_items_by_path_;
  QHash<QString, QTreeWidgetItem*> detail_items_by_path_;
  struct SpriteIconAnimation {
    QVector<QIcon> icon_frames;
    QVector<QIcon> detail_frames;
    QVector<int> frame_durations_ms;
    int frame_index = 0;
    int elapsed_ms = 0;
  };
  QHash<QString, SpriteIconAnimation> sprite_icon_animations_;
  QTimer* sprite_icon_timer_ = nullptr;
  QThreadPool thumbnail_pool_;
  quint64 thumbnail_generation_ = 0;
  QUndoStack* undo_stack_ = nullptr;
  QScopedPointer<QTemporaryDir> export_temp_dir_;
  int export_seq_ = 1;
  QStringList current_dir_;
  Archive archive_;
  struct MountedArchiveLayer {
    std::unique_ptr<Archive> archive;
    QString mount_name;
    QString mount_fs_path;
    QStringList outer_dir_before_mount;
  };
  std::vector<MountedArchiveLayer> mounted_archives_;
  QVector<AddedFile> added_files_;
  QHash<QString, int> added_index_by_name_;
  QSet<QString> virtual_dirs_;
  QSet<QString> deleted_files_;
  QSet<QString> deleted_dir_prefixes_;
  ViewMode view_mode_ = ViewMode::Auto;
  ViewMode effective_view_ = ViewMode::Details;
  bool dirty_ = false;
  bool quake1_palette_loaded_ = false;
  QVector<QRgb> quake1_palette_;
  QString quake1_palette_error_;
  bool quake2_palette_loaded_ = false;
  QVector<QRgb> quake2_palette_;
  QString quake2_palette_error_;
  bool pure_pak_protector_enabled_ = true;
  bool official_archive_ = false;
  bool image_texture_smoothing_ = false;
  GameId game_id_ = GameId::Quake;
  QString drag_source_uid_;
};
