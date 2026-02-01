#pragma once

#include <QString>

class QIODevice;

// Quake Live Beta used an XOR-obfuscated PK3 (ZIP) container; encryption/decryption is the same operation.
[[nodiscard]] bool looks_like_quakelive_encrypted_zip_header(const QString& file_path);
[[nodiscard]] bool quakelive_pk3_xor_stream(QIODevice& in, QIODevice& out, QString* error);

