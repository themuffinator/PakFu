#include "zip/zip_archive.h"

#include <algorithm>
#include <cstring>

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include "archive/path_safety.h"
#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#include "third_party/miniz/miniz.h"
#include "zip/quakelive_pk3_crypto.h"

namespace {
[[nodiscard]] size_t mz_read_qiodevice(void* opaque, mz_uint64 file_ofs, void* buf, size_t n) {
	auto* dev = static_cast<QIODevice*>(opaque);
	if (!dev || !dev->isOpen()) {
		return 0;
	}
	if (!dev->seek(static_cast<qint64>(file_ofs))) {
		return 0;
	}
	const qint64 got = dev->read(static_cast<char*>(buf), static_cast<qint64>(n));
	return got > 0 ? static_cast<size_t>(got) : 0;
}

[[nodiscard]] size_t mz_write_qiodevice(void* opaque, mz_uint64 file_ofs, const void* buf, size_t n) {
	auto* dev = static_cast<QIODevice*>(opaque);
	if (!dev || !dev->isOpen()) {
		return 0;
	}
	if (!dev->seek(static_cast<qint64>(file_ofs))) {
		return 0;
	}
	const qint64 wrote = dev->write(static_cast<const char*>(buf), static_cast<qint64>(n));
	return wrote > 0 ? static_cast<size_t>(wrote) : 0;
}

[[nodiscard]] mz_bool mz_keepalive_qiodevice(void* opaque) {
	(void)opaque;
	return MZ_TRUE;
}

struct MemWriteCtx {
	QByteArray* out = nullptr;
	qint64 max_bytes = -1;
};

[[nodiscard]] size_t mz_write_to_bytearray(void* opaque, mz_uint64 file_ofs, const void* buf, size_t n) {
	auto* ctx = static_cast<MemWriteCtx*>(opaque);
	if (!ctx || !ctx->out || !buf) {
		return 0;
	}
	QByteArray& out = *ctx->out;

	const qint64 ofs = static_cast<qint64>(file_ofs);
	if (ofs < 0) {
		return 0;
	}

	if (ctx->max_bytes >= 0 && ofs >= ctx->max_bytes) {
		return n;  // discard but report success for seamless truncation
	}

	qint64 to_copy = static_cast<qint64>(n);
	if (ctx->max_bytes >= 0) {
		to_copy = std::min<qint64>(to_copy, ctx->max_bytes - ofs);
	}
	if (to_copy <= 0) {
		return n;
	}

	const qint64 needed = ofs + to_copy;
	if (needed > out.size()) {
		out.resize(static_cast<int>(needed));
	}
	std::memcpy(out.data() + ofs, buf, static_cast<size_t>(to_copy));
	return n;
}

[[nodiscard]] bool zip_path_starts_with(const QString& path, const QString& prefix) {
	if (path.isEmpty() || prefix.isEmpty()) {
		return false;
	}
#if defined(Q_OS_WIN)
	return path.startsWith(prefix, Qt::CaseInsensitive);
#else
	return path.startsWith(prefix);
#endif
}

[[nodiscard]] QString normalize_zip_dir_prefix(QString path) {
	path = normalize_archive_entry_name(std::move(path));
	if (!path.isEmpty() && !path.endsWith('/')) {
		path += '/';
	}
	return path;
}

struct MzZipReaderGuard {
	mz_zip_archive* zip = nullptr;
	bool active = false;

	~MzZipReaderGuard() {
		if (active && zip) {
			mz_zip_reader_end(zip);
		}
	}

	void dismiss() { active = false; }
};

struct MzZipWriterGuard {
	mz_zip_archive* zip = nullptr;
	bool active = false;

	~MzZipWriterGuard() {
		if (active && zip) {
			mz_zip_writer_end(zip);
		}
	}

	void dismiss() { active = false; }
};
}  // namespace

struct ZipArchive::ZipState {
	mz_zip_archive zip{};
	QIODevice* device = nullptr;
};

void ZipArchive::ZipStateDeleter::operator()(ZipState* p) const noexcept {
	delete p;
}

ZipArchive::~ZipArchive() {
	if (state_) {
		mz_zip_reader_end(&state_->zip);
	}
}

QString ZipArchive::normalize_entry_name(QString name) {
	return normalize_archive_entry_name(std::move(name));
}

bool ZipArchive::init_from_device(QIODevice* device, qint64 device_size, QString* error) {
	if (error) {
		error->clear();
	}
	if (!device || !device->isOpen()) {
		if (error) {
			*error = "ZIP device is not open.";
		}
		return false;
	}
	if (device_size < 0) {
		if (error) {
			*error = "ZIP device size is invalid.";
		}
		return false;
	}

	if (state_) {
		mz_zip_reader_end(&state_->zip);
		state_.reset();
	}

	state_.reset(new ZipState());
	mz_zip_zero_struct(&state_->zip);
	state_->device = device;
	state_->zip.m_pRead = mz_read_qiodevice;
	state_->zip.m_pWrite = nullptr;
	state_->zip.m_pNeeds_keepalive = mz_keepalive_qiodevice;
	state_->zip.m_pIO_opaque = device;

	if (!mz_zip_reader_init(&state_->zip, static_cast<mz_uint64>(device_size), 0)) {
		const mz_zip_error err = mz_zip_get_last_error(&state_->zip);
		const char* msg = mz_zip_get_error_string(err);
		if (error) {
			*error = msg ? QString("Not a valid ZIP archive (%1).").arg(QString::fromLatin1(msg)) : "Not a valid ZIP archive.";
		}
		mz_zip_reader_end(&state_->zip);
		state_.reset();
		return false;
	}

	entries_.clear();
	index_by_name_.clear();

	const mz_uint count = mz_zip_reader_get_num_files(&state_->zip);
	entries_.reserve(static_cast<int>(count));
	index_by_name_.reserve(static_cast<int>(count));

	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat st{};
		if (!mz_zip_reader_file_stat(&state_->zip, i, &st)) {
			continue;
		}

		QString name = QString::fromUtf8(st.m_filename);
		name = normalize_entry_name(name);
		if (name.isEmpty()) {
			continue;
		}
		if (st.m_is_directory && !name.endsWith('/')) {
			name += '/';
		}
		if (!is_safe_archive_entry_name(name)) {
			continue;
		}

		ArchiveEntry e;
		e.name = name;
		e.offset = 0;
		e.size = static_cast<quint32>(std::min<mz_uint64>(st.m_uncomp_size, std::numeric_limits<quint32>::max()));
#if !defined(MINIZ_NO_TIME)
		e.mtime_utc_secs = static_cast<qint64>(st.m_time);
#else
		e.mtime_utc_secs = -1;
#endif
		entries_.push_back(e);
		index_by_name_.insert(name, static_cast<int>(i));
	}

	return true;
}

bool ZipArchive::load_zip_from_file(const QString& file_path, QString* error) {
	zip_file_ = std::make_unique<QFile>(file_path);
	if (!zip_file_->open(QIODevice::ReadOnly)) {
		if (error) {
			*error = "Unable to open archive file.";
		}
		zip_file_.reset();
		return false;
	}
	const qint64 size = zip_file_->size();
	if (!init_from_device(zip_file_.get(), size, error)) {
		zip_file_.reset();
		return false;
	}
	return true;
}

bool ZipArchive::maybe_load_quakelive_encrypted_pk3(const QString& file_path, QString* error) {
	if (!looks_like_quakelive_encrypted_zip_header(file_path)) {
		return false;
	}

	QFile in(file_path);
	if (!in.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = "Unable to open encrypted PK3.";
		}
		return false;
	}

	decrypted_temp_ = std::make_unique<QTemporaryFile>();
	decrypted_temp_->setAutoRemove(true);
	if (!decrypted_temp_->open()) {
		if (error) {
			*error = "Unable to create temporary file for decryption.";
		}
		decrypted_temp_.reset();
		return false;
	}

	QString xor_err;
	if (!quakelive_pk3_xor_stream(in, *decrypted_temp_, &xor_err)) {
		if (error) {
			*error = xor_err.isEmpty() ? "Unable to decrypt Quake Live PK3." : xor_err;
		}
		decrypted_temp_.reset();
		return false;
	}

	if (!decrypted_temp_->flush() || !decrypted_temp_->seek(0)) {
		if (error) {
			*error = "Unable to prepare decrypted PK3 for reading.";
		}
		decrypted_temp_.reset();
		return false;
	}

	const qint64 size = decrypted_temp_->size();
	if (!init_from_device(decrypted_temp_.get(), size, error)) {
		decrypted_temp_.reset();
		return false;
	}

	quakelive_encrypted_pk3_ = true;
	return true;
}

bool ZipArchive::load(const QString& path, QString* error) {
	if (error) {
		error->clear();
	}

	loaded_ = false;
	quakelive_encrypted_pk3_ = false;
	path_.clear();
	zip_path_.clear();
	entries_.clear();
	index_by_name_.clear();

	if (state_) {
		mz_zip_reader_end(&state_->zip);
		state_.reset();
	}
	zip_file_.reset();
	decrypted_temp_.reset();

	const QString abs = QFileInfo(path).absoluteFilePath();
	if (abs.isEmpty() || !QFileInfo::exists(abs)) {
		if (error) {
			*error = "Archive file not found.";
		}
		return false;
	}

	QString load_err;
	if (load_zip_from_file(abs, &load_err)) {
		loaded_ = true;
		path_ = abs;
		zip_path_ = abs;
		return true;
	}

	QString dec_err;
	if (maybe_load_quakelive_encrypted_pk3(abs, &dec_err)) {
		loaded_ = true;
		path_ = abs;
		zip_path_ = decrypted_temp_ ? decrypted_temp_->fileName() : abs;
		return true;
	}

	if (error) {
		*error = load_err.isEmpty() ? (dec_err.isEmpty() ? "Unable to load ZIP archive." : dec_err) : load_err;
	}
	return false;
}

bool ZipArchive::write_rebuilt(const QString& dest_path,
                               const WritePlan& plan,
                               bool quakelive_encrypt_pk3,
                               QString* error) {
	if (error) {
		error->clear();
	}

	const QString abs = QFileInfo(dest_path).absoluteFilePath();
	if (abs.isEmpty()) {
		if (error) {
			*error = "Invalid destination path.";
		}
		return false;
	}

	QSet<QString> deleted_files;
	deleted_files.reserve(plan.deleted_files.size());
	for (const QString& file_name : plan.deleted_files) {
		const QString normalized = normalize_entry_name(file_name);
		if (!normalized.isEmpty()) {
			deleted_files.insert(normalized);
		}
	}

	QVector<QString> deleted_dirs;
	deleted_dirs.reserve(plan.deleted_dir_prefixes.size());
	for (const QString& dir_name : plan.deleted_dir_prefixes) {
		const QString normalized = normalize_zip_dir_prefix(dir_name);
		if (!normalized.isEmpty()) {
			deleted_dirs.push_back(normalized);
		}
	}

	QSet<QString> replaced_entries;
	replaced_entries.reserve(plan.replaced_entries.size());
	for (const QString& name : plan.replaced_entries) {
		const QString normalized = normalize_entry_name(name);
		if (!normalized.isEmpty()) {
			replaced_entries.insert(normalized);
		}
	}

	const auto is_deleted_path = [&](const QString& path_in) -> bool {
		const QString path = normalize_entry_name(path_in);
		if (path.isEmpty()) {
			return false;
		}
		if (deleted_files.contains(path)) {
			return true;
		}
		for (const QString& dir_prefix : deleted_dirs) {
			if (zip_path_starts_with(path, dir_prefix)) {
				return true;
			}
		}
		return false;
	};

	const auto set_error = [&](const QString& message) {
		if (error) {
			*error = message;
		}
	};

	QFile src_file;
	mz_zip_archive src_zip{};
	bool have_src_zip = false;
	MzZipReaderGuard src_guard{&src_zip, false};

	if (!plan.source_zip_path.isEmpty()) {
		src_file.setFileName(plan.source_zip_path);
		if (!src_file.open(QIODevice::ReadOnly)) {
			set_error("Unable to open source ZIP for reading.");
			return false;
		}
		mz_zip_zero_struct(&src_zip);
		src_zip.m_pRead = mz_read_qiodevice;
		src_zip.m_pNeeds_keepalive = mz_keepalive_qiodevice;
		src_zip.m_pIO_opaque = &src_file;
		const qint64 src_size = src_file.size();
		if (src_size < 0 || !mz_zip_reader_init(&src_zip, static_cast<mz_uint64>(src_size), 0)) {
			const mz_zip_error zerr = mz_zip_get_last_error(&src_zip);
			const char* msg = mz_zip_get_error_string(zerr);
			set_error(msg ? QString("Unable to read source ZIP (%1).").arg(QString::fromLatin1(msg)) : "Unable to read source ZIP.");
			src_file.close();
			return false;
		}
		src_guard.active = true;
		have_src_zip = true;
	}

	QTemporaryFile temp_zip;
	temp_zip.setAutoRemove(true);
	if (!temp_zip.open()) {
		set_error("Unable to create temporary ZIP for writing.");
		return false;
	}

	mz_zip_archive out_zip{};
	mz_zip_zero_struct(&out_zip);
	out_zip.m_pWrite = mz_write_qiodevice;
	out_zip.m_pNeeds_keepalive = mz_keepalive_qiodevice;
	out_zip.m_pIO_opaque = &temp_zip;

	if (!mz_zip_writer_init(&out_zip, 0)) {
		const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
		const char* msg = mz_zip_get_error_string(zerr);
		set_error(msg ? QString("Unable to initialize ZIP writer (%1).").arg(QString::fromLatin1(msg)) : "Unable to initialize ZIP writer.");
		return false;
	}
	MzZipWriterGuard out_guard{&out_zip, true};

	if (have_src_zip) {
		const mz_uint file_count = mz_zip_reader_get_num_files(&src_zip);
		for (mz_uint i = 0; i < file_count; ++i) {
			mz_zip_archive_file_stat st{};
			if (!mz_zip_reader_file_stat(&src_zip, i, &st)) {
				continue;
			}
			QString name = QString::fromUtf8(st.m_filename);
			name = normalize_entry_name(name);
			if (name.isEmpty()) {
				continue;
			}
			if (st.m_is_directory && !name.endsWith('/')) {
				name += '/';
			}
			if (is_deleted_path(name)) {
				continue;
			}
			if (replaced_entries.contains(name)) {
				continue;
			}
			if (!is_safe_archive_entry_name(name)) {
				set_error(QString("Refusing to save unsafe entry: %1").arg(name));
				return false;
			}

			if (!mz_zip_writer_add_from_zip_reader(&out_zip, &src_zip, i)) {
				const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
				const char* msg = mz_zip_get_error_string(zerr);
				set_error(msg ? QString("Unable to copy ZIP entry (%1).").arg(QString::fromLatin1(msg)) : "Unable to copy ZIP entry.");
				return false;
			}
		}
	}

	QSet<QString> explicit_dirs_seen;
	explicit_dirs_seen.reserve(plan.explicit_directories.size());
	for (const QString& dir_path_in : plan.explicit_directories) {
		QString name = normalize_zip_dir_prefix(dir_path_in);
		if (name.isEmpty()) {
			continue;
		}
		if (explicit_dirs_seen.contains(name)) {
			continue;
		}
		explicit_dirs_seen.insert(name);
		if (is_deleted_path(name)) {
			continue;
		}
		if (!is_safe_archive_entry_name(name)) {
			set_error(QString("Refusing to save unsafe directory entry: %1").arg(name));
			return false;
		}

		const QByteArray name_utf8 = name.toUtf8();
		if (!mz_zip_writer_add_mem_ex(&out_zip, name_utf8.constData(), "", 0, nullptr, 0, 0, 0, 0)) {
			const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
			const char* msg = mz_zip_get_error_string(zerr);
			set_error(msg ? QString("Unable to add directory entry (%1).").arg(QString::fromLatin1(msg)) : "Unable to add directory entry.");
			return false;
		}
	}

	for (const DiskFile& file : plan.disk_files) {
		const QString name = normalize_entry_name(file.archive_name);
		if (name.isEmpty()) {
			continue;
		}
		if (is_deleted_path(name)) {
			continue;
		}
		if (!is_safe_archive_entry_name(name)) {
			set_error(QString("Refusing to save unsafe entry: %1").arg(name));
			return false;
		}

		QFile in(file.source_path);
		if (!in.open(QIODevice::ReadOnly)) {
			set_error(QString("Unable to open file: %1").arg(file.source_path));
			return false;
		}

		const qint64 size = in.size();
		if (size < 0) {
			set_error(QString("Unable to read file size: %1").arg(file.source_path));
			return false;
		}

		const QByteArray name_utf8 = name.toUtf8();
		MZ_TIME_T mtime = 0;
		const MZ_TIME_T* mtime_ptr = nullptr;
		if (file.mtime_utc_secs > 0) {
			mtime = static_cast<MZ_TIME_T>(file.mtime_utc_secs);
			mtime_ptr = &mtime;
		}

		if (!mz_zip_writer_add_read_buf_callback(&out_zip,
		                                         name_utf8.constData(),
		                                         mz_read_qiodevice,
		                                         &in,
		                                         static_cast<mz_uint64>(size),
		                                         mtime_ptr,
		                                         nullptr,
		                                         0,
		                                         MZ_DEFAULT_COMPRESSION,
		                                         nullptr,
		                                         0,
		                                         nullptr,
		                                         0)) {
			const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
			const char* msg = mz_zip_get_error_string(zerr);
			set_error(msg ? QString("Unable to add file to ZIP (%1).").arg(QString::fromLatin1(msg)) : "Unable to add file to ZIP.");
			return false;
		}
	}

	if (!mz_zip_writer_finalize_archive(&out_zip)) {
		const mz_zip_error zerr = mz_zip_get_last_error(&out_zip);
		const char* msg = mz_zip_get_error_string(zerr);
		set_error(msg ? QString("Unable to finalize ZIP (%1).").arg(QString::fromLatin1(msg)) : "Unable to finalize ZIP.");
		return false;
	}

	mz_zip_writer_end(&out_zip);
	out_guard.dismiss();

	if (have_src_zip) {
		mz_zip_reader_end(&src_zip);
		src_guard.dismiss();
		src_file.close();
	}

	if (!temp_zip.flush() || !temp_zip.seek(0)) {
		set_error("Unable to prepare ZIP output for commit.");
		return false;
	}

	QSaveFile out(abs);
	if (!out.open(QIODevice::WriteOnly)) {
		set_error("Unable to create destination archive.");
		return false;
	}

	if (quakelive_encrypt_pk3) {
		QString enc_err;
		if (!quakelive_pk3_xor_stream(temp_zip, out, &enc_err)) {
			set_error(enc_err.isEmpty() ? "Unable to encrypt Quake Live PK3." : enc_err);
			return false;
		}
	} else {
		QByteArray buf;
		buf.resize(1 << 16);
		for (;;) {
			const qint64 got = temp_zip.read(buf.data(), buf.size());
			if (got < 0) {
				set_error("Unable to read temporary ZIP.");
				return false;
			}
			if (got == 0) {
				break;
			}
			if (out.write(buf.constData(), got) != got) {
				set_error("Unable to write destination archive.");
				return false;
			}
		}
	}

	if (!out.commit()) {
		set_error("Unable to finalize destination archive.");
		return false;
	}

	return true;
}

bool ZipArchive::read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes) const {
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}
	if (!loaded_ || !state_) {
		if (error) {
			*error = "No ZIP is loaded.";
		}
		return false;
	}

	const QString key = normalize_entry_name(name);
	if (!is_safe_archive_entry_name(key)) {
		if (error) {
			*error = "Unsafe ZIP entry name.";
		}
		return false;
	}
	const int idx = index_by_name_.value(key, -1);
	if (idx < 0) {
		if (error) {
			*error = QString("Entry not found: %1").arg(name);
		}
		return false;
	}

	mz_zip_archive_file_stat st{};
	auto* zip = const_cast<mz_zip_archive*>(&state_->zip);
	if (!mz_zip_reader_file_stat(zip, static_cast<mz_uint>(idx), &st)) {
		if (error) {
			*error = "Unable to read ZIP entry metadata.";
		}
		return false;
	}
	if (st.m_is_directory) {
		if (out) {
			out->clear();
		}
		return true;
	}

	QByteArray bytes;
	const qint64 expected = static_cast<qint64>(std::min<mz_uint64>(st.m_uncomp_size, static_cast<mz_uint64>(std::numeric_limits<int>::max())));
	const qint64 want = (max_bytes >= 0) ? std::min(expected, max_bytes) : expected;
	bytes.resize(static_cast<int>(want));

	MemWriteCtx ctx;
	ctx.out = &bytes;
	ctx.max_bytes = max_bytes;

	if (!mz_zip_reader_extract_to_callback(zip, static_cast<mz_uint>(idx), mz_write_to_bytearray, &ctx, 0)) {
		const mz_zip_error zerr = mz_zip_get_last_error(zip);
		const char* msg = mz_zip_get_error_string(zerr);
		if (error) {
			*error = msg ? QString("Unable to extract ZIP entry (%1).").arg(QString::fromLatin1(msg)) : "Unable to extract ZIP entry.";
		}
		return false;
	}

	if (max_bytes >= 0 && bytes.size() > max_bytes) {
		bytes.truncate(static_cast<int>(max_bytes));
	}

	if (out) {
		*out = std::move(bytes);
	}
	return true;
}

bool ZipArchive::extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const {
	if (error) {
		error->clear();
	}
	if (!loaded_ || !state_) {
		if (error) {
			*error = "No ZIP is loaded.";
		}
		return false;
	}

	const QString key = normalize_entry_name(name);
	if (!is_safe_archive_entry_name(key)) {
		if (error) {
			*error = "Unsafe ZIP entry name.";
		}
		return false;
	}
	const int idx = index_by_name_.value(key, -1);
	if (idx < 0) {
		if (error) {
			*error = QString("Entry not found: %1").arg(name);
		}
		return false;
	}

	mz_zip_archive_file_stat st{};
	auto* zip = const_cast<mz_zip_archive*>(&state_->zip);
	if (!mz_zip_reader_file_stat(zip, static_cast<mz_uint>(idx), &st)) {
		if (error) {
			*error = "Unable to read ZIP entry metadata.";
		}
		return false;
	}

	const QFileInfo out_info(dest_path);
	if (!out_info.dir().exists()) {
		QDir d(out_info.dir().absolutePath());
		if (!d.mkpath(".")) {
			if (error) {
				*error = QString("Unable to create output directory: %1").arg(out_info.dir().absolutePath());
			}
			return false;
		}
	}

	if (st.m_is_directory) {
		QDir d(dest_path);
		if (!d.exists() && !d.mkpath(".")) {
			if (error) {
				*error = QString("Unable to create output directory: %1").arg(dest_path);
			}
			return false;
		}
		return true;
	}

	QSaveFile out(dest_path);
	if (!out.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = "Unable to create output file.";
		}
		return false;
	}

	out.resize(0);
	if (!mz_zip_reader_extract_to_callback(zip, static_cast<mz_uint>(idx), mz_write_qiodevice, &out, 0)) {
		const mz_zip_error zerr = mz_zip_get_last_error(zip);
		const char* msg = mz_zip_get_error_string(zerr);
		if (error) {
			*error = msg ? QString("Unable to extract ZIP entry (%1).").arg(QString::fromLatin1(msg)) : "Unable to extract ZIP entry.";
		}
		return false;
	}

	if (!out.commit()) {
		if (error) {
			*error = "Unable to finalize output file.";
		}
		return false;
	}

	return true;
}
