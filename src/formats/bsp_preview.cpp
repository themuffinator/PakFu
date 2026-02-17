#include "formats/bsp_preview.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <QtMath>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QVector2D>
#include <QVector3D>
#include <QtGlobal>

#include "formats/miptex_image.h"

namespace {
struct BspLump {
  int offset = 0;
  int length = 0;
};

struct BspHeader {
  QString magic;
  int version = 0;
  int lump_count = 0;
  QVector<BspLump> lumps;
  BspFamily family = BspFamily::Unknown;
  bool q1_bsp2 = false;
  int q3_textures_lump = -1;
  int q3_models_lump = -1;
  int q3_vertices_lump = -1;
  int q3_meshverts_lump = -1;
  int q3_faces_lump = -1;
  int q3_lightmaps_lump = -1;
  int q3_vertex_stride = 44;
  int q3_texture_stride = 72;
  int q3_face_stride = 104;
};

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Tri {
  QVector3D a;
  QVector3D b;
  QVector3D c;
  QVector2D ua;
  QVector2D ub;
  QVector2D uc;
  QVector2D lma;
  QVector2D lmb;
  QVector2D lmc;
  int lightmap_index = -1;
  QColor color;
  QString texture;
  bool uv_normalized = false;
};

constexpr int kQ1Version = 29;
constexpr int kGoldSrcVersion = 30;
constexpr int kQ1BetaVersion = 28;
constexpr int kQ1AlphaVersion = 27;
constexpr int kQ2Version = 38;
constexpr int kQ2ExtendedVersion = 41;
constexpr int kQ3Version = 46;
constexpr int kQ3DerivedVersion = 47;  // Quake Live, RtCW, Wolf:ET
constexpr int kRavenBspVersion = 1;
constexpr int kFusionBspVersion = 1;
constexpr int kFakk2Version = 12;
constexpr int kEf2DemoVersion = 19;
constexpr int kEf2Version = 20;

constexpr bool is_q1_release_or_goldsrc_bsp_version(int version) {
  return version == kQ1Version || version == kGoldSrcVersion;
}

constexpr bool is_q1_legacy_compatible_bsp_version(int version) {
  return is_q1_release_or_goldsrc_bsp_version(version) || version == kQ1BetaVersion || version == kQ1AlphaVersion;
}

constexpr bool is_q3_family_ibsp_version(int version) {
  return version == kQ3Version || version == kQ3DerivedVersion;
}

constexpr int kQ1LumpCount = 15;
constexpr int kQ2LumpCount = 19;
constexpr int kQ3LumpCount = 17;
constexpr int kQ3ExtendedLumpCount = 18;
constexpr int kFakk2LumpCount = 20;
constexpr int kEf2LumpCount = 30;

enum Q1Lump {
  Q1_ENTITIES = 0,
  Q1_PLANES = 1,
  Q1_TEXTURES = 2,
  Q1_VERTICES = 3,
  Q1_VISIBILITY = 4,
  Q1_NODES = 5,
  Q1_TEXINFO = 6,
  Q1_FACES = 7,
  Q1_LIGHTING = 8,
  Q1_CLIPNODES = 9,
  Q1_LEAFS = 10,
  Q1_MARKSURFACES = 11,
  Q1_EDGES = 12,
  Q1_SURFEDGES = 13,
  Q1_MODELS = 14,
};

enum Q2Lump {
  Q2_ENTITIES = 0,
  Q2_PLANES = 1,
  Q2_VERTICES = 2,
  Q2_VISIBILITY = 3,
  Q2_NODES = 4,
  Q2_TEXINFO = 5,
  Q2_FACES = 6,
  Q2_LIGHTING = 7,
  Q2_LEAFS = 8,
  Q2_LEAFFACES = 9,
  Q2_LEAFBRUSHES = 10,
  Q2_EDGES = 11,
  Q2_SURFEDGES = 12,
  Q2_MODELS = 13,
  Q2_BRUSHES = 14,
  Q2_BRUSHSIDES = 15,
  Q2_POP = 16,
  Q2_AREAS = 17,
  Q2_AREAPORTALS = 18,
};

enum Q3Lump {
  Q3_ENTITIES = 0,
  Q3_TEXTURES = 1,
  Q3_PLANES = 2,
  Q3_NODES = 3,
  Q3_LEAFS = 4,
  Q3_LEAFFACES = 5,
  Q3_LEAFBRUSHES = 6,
  Q3_MODELS = 7,
  Q3_BRUSHES = 8,
  Q3_BRUSHSIDES = 9,
  Q3_VERTICES = 10,
  Q3_MESHVERTS = 11,
  Q3_EFFECTS = 12,
  Q3_FACES = 13,
  Q3_LIGHTMAPS = 14,
  Q3_LIGHTVOLS = 15,
  Q3_VISDATA = 16,
};

bool classify_format(const QString& magic, int version, int file_size, BspHeader* out, QString* error) {
  if (!out) {
    return false;
  }

  out->family = BspFamily::Unknown;
  out->lump_count = 0;
  out->q1_bsp2 = false;
  out->q3_textures_lump = -1;
  out->q3_models_lump = -1;
  out->q3_vertices_lump = -1;
  out->q3_meshverts_lump = -1;
  out->q3_faces_lump = -1;
  out->q3_lightmaps_lump = -1;
  out->q3_vertex_stride = 44;
  out->q3_texture_stride = 72;
  out->q3_face_stride = 104;

  auto set_q3_layout = [&](int lump_count,
                           int textures,
                           int models,
                           int vertices,
                           int meshverts,
                           int faces,
                           int lightmaps,
                           int vertex_stride,
                           int texture_stride,
                           int face_stride) {
    out->family = BspFamily::Quake3;
    out->lump_count = lump_count;
    out->q3_textures_lump = textures;
    out->q3_models_lump = models;
    out->q3_vertices_lump = vertices;
    out->q3_meshverts_lump = meshverts;
    out->q3_faces_lump = faces;
    out->q3_lightmaps_lump = lightmaps;
    out->q3_vertex_stride = vertex_stride;
    out->q3_texture_stride = texture_stride;
    out->q3_face_stride = face_stride;
  };

  if (magic == "BSP2" || magic == "2PSB") {
    out->family = BspFamily::Quake1;
    out->lump_count = kQ1LumpCount;
    out->q1_bsp2 = true;
    return true;
  }

  if (magic == "IBSP" || magic == "Q1BS") {
    const bool q1_ok = (magic == "IBSP")
                         ? is_q1_release_or_goldsrc_bsp_version(version)
                         : is_q1_legacy_compatible_bsp_version(version);
    if (q1_ok) {
      out->family = BspFamily::Quake1;
      out->lump_count = kQ1LumpCount;
      return true;
    }
    if (magic == "Q1BS") {
      if (error) {
        *error = QString("Unsupported Quake-family BSP version %1.").arg(QString::number(version));
      }
      return false;
    }
    if (version == kQ2Version || version == kQ2ExtendedVersion) {
      out->family = BspFamily::Quake2;
      out->lump_count = kQ2LumpCount;
      return true;
    }
    if (is_q3_family_ibsp_version(version)) {
      set_q3_layout(kQ3LumpCount, Q3_TEXTURES, Q3_MODELS, Q3_VERTICES, Q3_MESHVERTS, Q3_FACES, Q3_LIGHTMAPS, 44, 72, 104);
      return true;
    }
  }

  if (magic == "QBSP" && version == kQ2Version) {
    out->family = BspFamily::Quake2;
    out->lump_count = kQ2LumpCount;
    return true;
  }

  if (magic == "RBSP" && version == kRavenBspVersion) {
    const int lump_count = (file_size >= (8 + kQ3ExtendedLumpCount * 8)) ? kQ3ExtendedLumpCount : kQ3LumpCount;
    set_q3_layout(lump_count, Q3_TEXTURES, Q3_MODELS, Q3_VERTICES, Q3_MESHVERTS, Q3_FACES, Q3_LIGHTMAPS, 44, 72, 104);
    return true;
  }

  if (magic == "FBSP" && version == kFusionBspVersion) {
    const int lump_count = (file_size >= (8 + kQ3ExtendedLumpCount * 8)) ? kQ3ExtendedLumpCount : kQ3LumpCount;
    set_q3_layout(lump_count, Q3_TEXTURES, Q3_MODELS, Q3_VERTICES, Q3_MESHVERTS, Q3_FACES, Q3_LIGHTMAPS, 80, 72, 148);
    return true;
  }

  if (magic == "FAKK" && version == kFakk2Version) {
    set_q3_layout(kFakk2LumpCount, 0, 13, 4, 5, 3, 2, 44, 76, 108);
    return true;
  }

  if (magic == "FAKK" && version == kEf2DemoVersion) {
    set_q3_layout(kEf2LumpCount, 0, 13, 6, 7, 5, 2, 44, 76, 132);
    return true;
  }

  if (magic == "EF2!" && version == kEf2Version) {
    set_q3_layout(kEf2LumpCount, 0, 13, 6, 7, 5, 2, 44, 76, 132);
    return true;
  }

  if (error) {
    *error = QString("Unsupported BSP format: %1 version %2").arg(magic, QString::number(version));
  }
  return false;
}

bool read_bytes(const QByteArray& data, int offset, void* dst, int size) {
  if (offset < 0 || size < 0 || offset + size > data.size()) {
    return false;
  }
  std::memcpy(dst, data.constData() + offset, static_cast<size_t>(size));
  return true;
}

quint32 read_u32_le(const QByteArray& data, int offset, bool* ok = nullptr) {
  if (ok) {
    *ok = false;
  }
  if (offset < 0 || offset + 4 > data.size()) {
    return 0;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
  const quint32 v = static_cast<quint32>(p[0]) |
                    (static_cast<quint32>(p[1]) << 8) |
                    (static_cast<quint32>(p[2]) << 16) |
                    (static_cast<quint32>(p[3]) << 24);
  if (ok) {
    *ok = true;
  }
  return v;
}

qint32 read_i32_le(const QByteArray& data, int offset, bool* ok = nullptr) {
  return static_cast<qint32>(read_u32_le(data, offset, ok));
}

qint16 read_i16_le(const QByteArray& data, int offset, bool* ok = nullptr) {
  if (ok) {
    *ok = false;
  }
  if (offset < 0 || offset + 2 > data.size()) {
    return 0;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
  const qint16 v = static_cast<qint16>(p[0] | (static_cast<qint16>(p[1]) << 8));
  if (ok) {
    *ok = true;
  }
  return v;
}

quint16 read_u16_le(const QByteArray& data, int offset, bool* ok = nullptr) {
  if (ok) {
    *ok = false;
  }
  if (offset < 0 || offset + 2 > data.size()) {
    return 0;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
  const quint16 v = static_cast<quint16>(p[0] | (static_cast<quint16>(p[1]) << 8));
  if (ok) {
    *ok = true;
  }
  return v;
}

float read_f32_le(const QByteArray& data, int offset, bool* ok = nullptr) {
  quint32 u = read_u32_le(data, offset, ok);
  float f = 0.0f;
  std::memcpy(&f, &u, sizeof(float));
  return f;
}

bool lump_in_bounds(const QByteArray& data, const BspLump& lump) {
  if (lump.offset < 0 || lump.length < 0) {
    return false;
  }
  const qint64 end = static_cast<qint64>(lump.offset) + static_cast<qint64>(lump.length);
  return end <= data.size();
}

QHash<QByteArray, BspLump> parse_bspx_lumps(const QByteArray& data, const BspHeader& header) {
  QHash<QByteArray, BspLump> out;
  if (header.lumps.isEmpty()) {
    return out;
  }

  qint64 max_end = 0;
  for (const BspLump& lump : header.lumps) {
    if (lump.offset < 0 || lump.length < 0) {
      continue;
    }
    const qint64 end = static_cast<qint64>(lump.offset) + static_cast<qint64>(lump.length);
    max_end = std::max(max_end, end);
  }

  const qint64 bspx_ofs = (max_end + 3LL) & ~3LL;
  if (bspx_ofs < 0 || bspx_ofs + 8 > data.size()) {
    return out;
  }

  char ident[4]{};
  if (!read_bytes(data, static_cast<int>(bspx_ofs), ident, 4)) {
    return out;
  }
  if (std::memcmp(ident, "BSPX", 4) != 0) {
    return out;
  }

  bool ok = false;
  const quint32 lump_count = read_u32_le(data, static_cast<int>(bspx_ofs + 4), &ok);
  if (!ok) {
    return out;
  }
  const qint64 table_ofs = bspx_ofs + 8;
  const qint64 table_bytes = static_cast<qint64>(lump_count) * 32;
  if (table_ofs < 0 || table_ofs + table_bytes > data.size()) {
    return out;
  }

  for (quint32 i = 0; i < lump_count; ++i) {
    const qint64 base = table_ofs + static_cast<qint64>(i) * 32;
    if (base < 0 || base + 32 > data.size()) {
      break;
    }

    char name_raw[24]{};
    if (!read_bytes(data, static_cast<int>(base), name_raw, 24)) {
      continue;
    }
    QByteArray name(name_raw, 24);
    const int nul = name.indexOf('\0');
    if (nul >= 0) {
      name.truncate(nul);
    }
    name = name.trimmed().toUpper();
    if (name.isEmpty()) {
      continue;
    }

    bool ok_ofs = false;
    bool ok_len = false;
    const quint32 file_ofs_u = read_u32_le(data, static_cast<int>(base + 24), &ok_ofs);
    const quint32 file_len_u = read_u32_le(data, static_cast<int>(base + 28), &ok_len);
    if (!ok_ofs || !ok_len) {
      continue;
    }
    const BspLump lump{static_cast<int>(file_ofs_u), static_cast<int>(file_len_u)};
    if (!lump_in_bounds(data, lump)) {
      continue;
    }
    out.insert(name, lump);
  }

  return out;
}

bool parse_header(const QByteArray& data, BspHeader* out, QString* error) {
  if (!out) {
    return false;
  }
  if (data.size() < 8) {
    if (error) {
      *error = "File too small for BSP header.";
    }
    return false;
  }
  auto parse_lumps = [&](BspHeader* h, int lumps_offset, QString* err) -> bool {
    if (!h) {
      return false;
    }
    const int lump_count = h->lump_count;
    if (lump_count <= 0) {
      if (err) {
        *err = QString("Unsupported BSP format: %1 version %2").arg(h->magic, QString::number(h->version));
      }
      return false;
    }
    const int header_size = lumps_offset + lump_count * 8;
    if (data.size() < header_size) {
      if (err) {
        *err = "Truncated BSP header.";
      }
      return false;
    }

    QVector<BspLump> lumps;
    lumps.reserve(lump_count);
    int offset = lumps_offset;
    for (int i = 0; i < lump_count; ++i) {
      bool ok1 = false;
      bool ok2 = false;
      const int lofs = read_i32_le(data, offset, &ok1);
      const int llen = read_i32_le(data, offset + 4, &ok2);
      if (!ok1 || !ok2) {
        if (err) {
          *err = "Failed to parse BSP lumps.";
        }
        return false;
      }
      lumps.push_back(BspLump{lofs, llen});
      offset += 8;
    }
    h->lumps = std::move(lumps);
    return true;
  };

  // First try modern layout: [magic][version][lumps...].
  char magic_raw[5]{};
  if (!read_bytes(data, 0, magic_raw, 4)) {
    if (error) {
      *error = "Unable to read BSP header.";
    }
    return false;
  }

  const QString header_magic = QString::fromLatin1(magic_raw, 4);
  if (header_magic == "BSP2" || header_magic == "2PSB") {
    BspHeader bsp2{};
    bsp2.magic = header_magic;
    bsp2.version = 0;
    QString bsp2_err;
    if (classify_format(bsp2.magic, bsp2.version, data.size(), &bsp2, &bsp2_err)) {
      if (parse_lumps(&bsp2, 4, &bsp2_err)) {
        *out = std::move(bsp2);
        return true;
      }
    }
    if (error && !bsp2_err.isEmpty()) {
      *error = bsp2_err;
    }
  }

  bool ok_version_modern = false;
  const int version_modern = read_i32_le(data, 4, &ok_version_modern);
  if (ok_version_modern) {
    BspHeader modern{};
    modern.magic = QString::fromLatin1(magic_raw, 4);
    modern.version = version_modern;
    QString modern_err;
    if (classify_format(modern.magic, modern.version, data.size(), &modern, &modern_err)) {
      if (parse_lumps(&modern, 8, &modern_err)) {
        *out = std::move(modern);
        return true;
      }
    }
    if (error && !modern_err.isEmpty()) {
      *error = modern_err;
    }
  }

  // Fallback for Quake/GoldSrc classic layout: [version][lumps...].
  bool ok_version_legacy = false;
  const int version_legacy = read_i32_le(data, 0, &ok_version_legacy);
  if (ok_version_legacy && is_q1_legacy_compatible_bsp_version(version_legacy)) {
    BspHeader legacy{};
    legacy.magic = "Q1BS";
    legacy.version = version_legacy;
    QString legacy_err;
    if (classify_format(legacy.magic, legacy.version, data.size(), &legacy, &legacy_err)) {
      if (parse_lumps(&legacy, 4, &legacy_err)) {
        *out = std::move(legacy);
        return true;
      }
    }
    if (error && !legacy_err.isEmpty()) {
      *error = legacy_err;
    }
  }

  if (error && error->isEmpty()) {
    *error = "Unable to parse BSP header.";
  }
  return false;
}

struct Q1TexInfo {
  float vecs[2][4];
  int miptex = -1;
  int flags = 0;
};

struct Q2TexInfo {
  float vecs[2][4];
  int flags = 0;
  int value = 0;
  char texture[32];
  int nexttexinfo = -1;
};

struct Q1Face {
  int plane = 0;
  int side = 0;
  int firstedge = 0;
  int numedges = 0;
  int texinfo = 0;
  unsigned char styles[4]{};
  int lightofs = -1;
};

struct Q2Face {
  int plane = 0;
  int side = 0;
  int firstedge = 0;
  int numedges = 0;
  int texinfo = 0;
  unsigned char styles[4]{};
  int lightofs = -1;
};

struct Q2DecoupledLightmap {
  bool valid = false;
  int width = 0;
  int height = 0;
  int offset = -1;
  float world_to_lm[2][4]{};
};

enum class Q2LightSampleFormat {
  None,
  Gray8,
  Rgb8,
  HdrE5Bgr9,
};

struct Q2LightSampleSource {
  Q2LightSampleFormat format = Q2LightSampleFormat::None;
  BspLump lump;
  float hdr_inv_peak = 1.0f;
};

struct Edge16 {
  quint16 v0 = 0;
  quint16 v1 = 0;
};

struct Edge32 {
  int v0 = 0;
  int v1 = 0;
};

struct Q3Vertex {
  Vec3 pos;
  float st[2]{};
  float lmst[2]{};
};

struct Q3Face {
  int shader = 0;
  int effect = 0;
  int type = 0;
  int firstVert = 0;
  int numVerts = 0;
  int firstMeshVert = 0;
  int numMeshVerts = 0;
  int lmIndex = -1;
  int lmStart[2]{};
  int lmSize[2]{};
  float lmOrigin[3]{};
  float lmVecs[2][3]{};
  float normal[3]{};
  int size[2]{};
};

struct ModelFaceRange {
  int first_face = 0;
  int num_faces = 0;
};

QString texture_name_from_q1_miptex(const QByteArray& data,
                                    const BspLump& tex_lump,
                                    const QVector<int>& offsets,
                                    int index) {
  if (index < 0 || index >= offsets.size()) {
    return {};
  }
  if (tex_lump.offset < 0 || tex_lump.length < 16) {
    return {};
  }
  const int base_rel = offsets[index];
  const qint64 base_abs = static_cast<qint64>(tex_lump.offset) + static_cast<qint64>(base_rel);
  if (base_rel < 0 ||
      static_cast<qint64>(base_rel) + 16 > tex_lump.length ||
      base_abs < 0 ||
      base_abs + 16 > data.size()) {
    return {};
  }

  const char* p = data.constData() + static_cast<int>(base_abs);
  int len = 0;
  for (; len < 16; ++len) {
    if (p[len] == '\0') {
      break;
    }
  }
  return QString::fromLatin1(p, len);
}

bool is_non_visible_texture_name(const QString& name) {
  if (name.isEmpty()) {
    return false;
  }
  const QString lower = name.toLower();
  static const QStringList needles = {
    "clip",
    "playerclip",
    "monsterclip",
    "trigger",
    "hint",
    "skip",
    "nodraw",
    "caulk",
    "origin",
  };
  for (const QString& n : needles) {
    if (lower.contains(n)) {
      return true;
    }
  }
  if (lower.startsWith("common/") || lower.startsWith("tools/")) {
    return true;
  }
  return false;
}

bool is_sky_texture_name(const QString& name) {
  if (name.isEmpty()) {
    return false;
  }
  const QString lower = name.toLower();
  if (!lower.contains("sky")) {
    return false;
  }
  if (lower.startsWith("sky") || lower.contains("/sky") || lower.contains("sky/")) {
    return true;
  }
  if (lower.contains("skies") || lower.contains("_sky")) {
    return true;
  }
  return false;
}

template <typename EdgeT>
float average_light_q1q2(const QVector<Vec3>& verts,
                         const QVector<EdgeT>& edges,
                         const QVector<int>& surfedges,
                         const QVector<Q1TexInfo>& texinfo,
                         const Q1Face& face,
                         const QByteArray& data,
                         const BspLump& light_lump) {
  if (face.texinfo < 0 || face.texinfo >= texinfo.size() || face.lightofs < 0) {
    return 0.6f;
  }
  if (light_lump.offset < 0 || light_lump.length <= 0) {
    return 0.6f;
  }
  const Q1TexInfo& tx = texinfo[face.texinfo];
  if (face.firstedge < 0 || face.numedges <= 0 || face.firstedge + face.numedges > surfedges.size()) {
    return 0.6f;
  }

  float min_s = 0.0f, max_s = 0.0f, min_t = 0.0f, max_t = 0.0f;
  bool first = true;
  for (int i = 0; i < face.numedges; ++i) {
    const int se = surfedges[face.firstedge + i];
    const int edge_index = (se >= 0) ? se : -se;
    if (edge_index < 0 || edge_index >= edges.size()) {
      continue;
    }
    const EdgeT& e = edges[edge_index];
    const int v_index = (se >= 0) ? e.v0 : e.v1;
    if (v_index < 0 || v_index >= verts.size()) {
      continue;
    }
    const Vec3& v = verts[v_index];
    const float s = v.x * tx.vecs[0][0] + v.y * tx.vecs[0][1] + v.z * tx.vecs[0][2] + tx.vecs[0][3];
    const float t = v.x * tx.vecs[1][0] + v.y * tx.vecs[1][1] + v.z * tx.vecs[1][2] + tx.vecs[1][3];
    if (first) {
      min_s = max_s = s;
      min_t = max_t = t;
      first = false;
    } else {
      min_s = qMin(min_s, s);
      max_s = qMax(max_s, s);
      min_t = qMin(min_t, t);
      max_t = qMax(max_t, t);
    }
  }
  if (first) {
    return 0.6f;
  }

  const int smin = static_cast<int>(std::floor(min_s / 16.0f));
  const int smax = static_cast<int>(std::floor(max_s / 16.0f));
  const int tmin = static_cast<int>(std::floor(min_t / 16.0f));
  const int tmax = static_cast<int>(std::floor(max_t / 16.0f));
  const int w = smax - smin + 1;
  const int h = tmax - tmin + 1;
  if (w <= 0 || h <= 0) {
    return 0.6f;
  }
  const int count = w * h;
  if (face.lightofs < 0 || face.lightofs + count > light_lump.length) {
    return 0.6f;
  }

  const qint64 abs_ofs = static_cast<qint64>(light_lump.offset) + static_cast<qint64>(face.lightofs);
  if (abs_ofs < 0 || abs_ofs + count > data.size()) {
    return 0.6f;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data.constData() + static_cast<int>(abs_ofs));
  qint64 sum = 0;
  for (int i = 0; i < count; ++i) {
    sum += p[i];
  }
  const float avg = static_cast<float>(sum) / static_cast<float>(count) / 255.0f;
  return qBound(0.1f, avg, 1.0f);
}

QVector<Vec3> parse_q1_vertices(const QByteArray& data, const BspLump& lump) {
  QVector<Vec3> out;
  const int stride = 12;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    Vec3 v;
    v.x = read_f32_le(data, o + 0, &ok);
    if (!ok) {
      break;
    }
    v.y = read_f32_le(data, o + 4, &ok);
    if (!ok) {
      break;
    }
    v.z = read_f32_le(data, o + 8, &ok);
    if (!ok) {
      break;
    }
    out.push_back(v);
  }
  return out;
}

QVector<Edge16> parse_q1_edges(const QByteArray& data, const BspLump& lump) {
  QVector<Edge16> out;
  const int stride = 4;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    const quint32 v01 = read_u32_le(data, o, &ok);
    if (!ok) {
      break;
    }
    Edge16 e;
    e.v0 = static_cast<quint16>(v01 & 0xFFFF);
    e.v1 = static_cast<quint16>((v01 >> 16) & 0xFFFF);
    out.push_back(e);
  }
  return out;
}

QVector<Edge32> parse_q1_edges_bsp2(const QByteArray& data, const BspLump& lump) {
  QVector<Edge32> out;
  const int stride = 8;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    const quint32 v0_u = read_u32_le(data, o + 0, &ok);
    if (!ok) {
      break;
    }
    const quint32 v1_u = read_u32_le(data, o + 4, &ok);
    if (!ok) {
      break;
    }
    Edge32 e;
    e.v0 = static_cast<int>(v0_u);
    e.v1 = static_cast<int>(v1_u);
    out.push_back(e);
  }
  return out;
}

QVector<int> parse_surfedges(const QByteArray& data, const BspLump& lump) {
  QVector<int> out;
  const int stride = 4;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    bool ok = false;
    const int v = read_i32_le(data, lump.offset + i * stride, &ok);
    if (!ok) {
      break;
    }
    out.push_back(v);
  }
  return out;
}

QVector<Q1TexInfo> parse_q1_texinfo(const QByteArray& data, const BspLump& lump) {
  QVector<Q1TexInfo> out;
  const int stride = 40;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    Q1TexInfo tx{};
    for (int r = 0; r < 2; ++r) {
      for (int c = 0; c < 4; ++c) {
        tx.vecs[r][c] = read_f32_le(data, o + (r * 4 + c) * 4, &ok);
        if (!ok) {
          return out;
        }
      }
    }
    tx.miptex = read_i32_le(data, o + 32, &ok);
    if (!ok) {
      return out;
    }
    tx.flags = read_i32_le(data, o + 36, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(tx);
  }
  return out;
}

QVector<Q2TexInfo> parse_q2_texinfo(const QByteArray& data, const BspLump& lump) {
  QVector<Q2TexInfo> out;
  const int stride = 76;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    Q2TexInfo tx{};
    for (int r = 0; r < 2; ++r) {
      for (int c = 0; c < 4; ++c) {
        tx.vecs[r][c] = read_f32_le(data, o + (r * 4 + c) * 4, &ok);
        if (!ok) {
          return out;
        }
      }
    }
    tx.flags = read_i32_le(data, o + 32, &ok);
    if (!ok) {
      return out;
    }
    tx.value = read_i32_le(data, o + 36, &ok);
    if (!ok) {
      return out;
    }
    if (!read_bytes(data, o + 40, tx.texture, 32)) {
      return out;
    }
    tx.nexttexinfo = read_i32_le(data, o + 72, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(tx);
  }
  return out;
}

QVector<Q1Face> parse_q1_faces(const QByteArray& data, const BspLump& lump) {
  QVector<Q1Face> out;
  const int stride = 20;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
Q1Face f{};
    f.plane = read_i16_le(data, o + 0, &ok);
    if (!ok) {
      return out;
    }
    f.side = read_i16_le(data, o + 2, &ok);
    if (!ok) {
      return out;
    }
    f.firstedge = read_i32_le(data, o + 4, &ok);
    if (!ok) {
      return out;
    }
    f.numedges = read_i16_le(data, o + 8, &ok);
    if (!ok) {
      return out;
    }
    f.texinfo = read_i16_le(data, o + 10, &ok);
    if (!ok) {
      return out;
    }
    if (!read_bytes(data, o + 12, f.styles, 4)) {
      return out;
    }
    f.lightofs = read_i32_le(data, o + 16, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(f);
  }
  return out;
}

QVector<Q1Face> parse_q1_faces_bsp2(const QByteArray& data, const BspLump& lump) {
  QVector<Q1Face> out;
  const int stride = 28;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    Q1Face f{};
    f.plane = read_i32_le(data, o + 0, &ok);
    if (!ok) {
      return out;
    }
    f.side = read_i32_le(data, o + 4, &ok);
    if (!ok) {
      return out;
    }
    f.firstedge = read_i32_le(data, o + 8, &ok);
    if (!ok) {
      return out;
    }
    f.numedges = read_i32_le(data, o + 12, &ok);
    if (!ok) {
      return out;
    }
    f.texinfo = read_i32_le(data, o + 16, &ok);
    if (!ok) {
      return out;
    }
    if (!read_bytes(data, o + 20, f.styles, 4)) {
      return out;
    }
    f.lightofs = read_i32_le(data, o + 24, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(f);
  }
  return out;
}

QVector<Q2Face> parse_q2_faces(const QByteArray& data, const BspLump& lump) {
  QVector<Q2Face> out;
  const int stride = 20;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
Q2Face f{};
    f.plane = read_i16_le(data, o + 0, &ok);
    if (!ok) {
      return out;
    }
    f.side = read_i16_le(data, o + 2, &ok);
    if (!ok) {
      return out;
    }
    f.firstedge = read_i32_le(data, o + 4, &ok);
    if (!ok) {
      return out;
    }
    f.numedges = read_i16_le(data, o + 8, &ok);
    if (!ok) {
      return out;
    }
    f.texinfo = read_i16_le(data, o + 10, &ok);
    if (!ok) {
      return out;
    }
    if (!read_bytes(data, o + 12, f.styles, 4)) {
      return out;
    }
    f.lightofs = read_i32_le(data, o + 16, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(f);
  }
  return out;
}

QVector<ModelFaceRange> parse_q2_model_face_ranges(const QByteArray& data, const BspLump& lump) {
  QVector<ModelFaceRange> out;
  const int stride = 48;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    ModelFaceRange range{};
    range.first_face = read_i32_le(data, o + 40, &ok);
    if (!ok) {
      return out;
    }
    range.num_faces = read_i32_le(data, o + 44, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(range);
  }
  return out;
}

QVector<int> parse_q1_miptex_offsets(const QByteArray& data, const BspLump& lump) {
  QVector<int> out;
  if (lump.length < 4) {
    return out;
  }
  bool ok = false;
  const int count = read_i32_le(data, lump.offset, &ok);
  if (!ok || count <= 0) {
    return out;
  }
  const int base = lump.offset + 4;
  if (base + count * 4 > lump.offset + lump.length) {
    return out;
  }
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    bool ok2 = false;
    int ofs = read_i32_le(data, base + i * 4, &ok2);
    if (!ok2) {
      return out;
    }
    out.push_back(ofs);
  }
  return out;
}

QVector<Q3Vertex> parse_q3_vertices(const QByteArray& data, const BspLump& lump, int stride) {
  QVector<Q3Vertex> out;
  if (stride < 28) {
    return out;
  }
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    Q3Vertex v{};
    v.pos.x = read_f32_le(data, o + 0, &ok);
    if (!ok) {
      return out;
    }
    v.pos.y = read_f32_le(data, o + 4, &ok);
    if (!ok) {
      return out;
    }
    v.pos.z = read_f32_le(data, o + 8, &ok);
    if (!ok) {
      return out;
    }
    v.st[0] = read_f32_le(data, o + 12, &ok);
    if (!ok) {
      return out;
    }
    v.st[1] = read_f32_le(data, o + 16, &ok);
    if (!ok) {
      return out;
    }
    v.lmst[0] = read_f32_le(data, o + 20, &ok);
    if (!ok) {
      return out;
    }
    v.lmst[1] = read_f32_le(data, o + 24, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(v);
  }
  return out;
}

QVector<int> parse_q3_meshverts(const QByteArray& data, const BspLump& lump) {
  QVector<int> out;
  const int stride = 4;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    bool ok = false;
    const int v = read_i32_le(data, lump.offset + i * stride, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(v);
  }
  return out;
}

QVector<Q3Face> parse_q3_faces(const QByteArray& data, const BspLump& lump, int stride) {
  QVector<Q3Face> out;
  if (stride < 104) {
    return out;
  }
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    Q3Face f{};
    f.shader = read_i32_le(data, o + 0, &ok);
    if (!ok) {
      return out;
    }
    f.effect = read_i32_le(data, o + 4, &ok);
    if (!ok) {
      return out;
    }
    f.type = read_i32_le(data, o + 8, &ok);
    if (!ok) {
      return out;
    }
    f.firstVert = read_i32_le(data, o + 12, &ok);
    if (!ok) {
      return out;
    }
    f.numVerts = read_i32_le(data, o + 16, &ok);
    if (!ok) {
      return out;
    }
    f.firstMeshVert = read_i32_le(data, o + 20, &ok);
    if (!ok) {
      return out;
    }
    f.numMeshVerts = read_i32_le(data, o + 24, &ok);
    if (!ok) {
      return out;
    }
    f.lmIndex = read_i32_le(data, o + 28, &ok);
    if (!ok) {
      return out;
    }
    f.lmStart[0] = read_i32_le(data, o + 32, &ok);
    if (!ok) {
      return out;
    }
    f.lmStart[1] = read_i32_le(data, o + 36, &ok);
    if (!ok) {
      return out;
    }
    f.lmSize[0] = read_i32_le(data, o + 40, &ok);
    if (!ok) {
      return out;
    }
    f.lmSize[1] = read_i32_le(data, o + 44, &ok);
    if (!ok) {
      return out;
    }
    f.lmOrigin[0] = read_f32_le(data, o + 48, &ok);
    if (!ok) {
      return out;
    }
    f.lmOrigin[1] = read_f32_le(data, o + 52, &ok);
    if (!ok) {
      return out;
    }
    f.lmOrigin[2] = read_f32_le(data, o + 56, &ok);
    if (!ok) {
      return out;
    }
    f.lmVecs[0][0] = read_f32_le(data, o + 60, &ok);
    if (!ok) {
      return out;
    }
    f.lmVecs[0][1] = read_f32_le(data, o + 64, &ok);
    if (!ok) {
      return out;
    }
    f.lmVecs[0][2] = read_f32_le(data, o + 68, &ok);
    if (!ok) {
      return out;
    }
    f.lmVecs[1][0] = read_f32_le(data, o + 72, &ok);
    if (!ok) {
      return out;
    }
    f.lmVecs[1][1] = read_f32_le(data, o + 76, &ok);
    if (!ok) {
      return out;
    }
    f.lmVecs[1][2] = read_f32_le(data, o + 80, &ok);
    if (!ok) {
      return out;
    }
    f.normal[0] = read_f32_le(data, o + 84, &ok);
    if (!ok) {
      return out;
    }
    f.normal[1] = read_f32_le(data, o + 88, &ok);
    if (!ok) {
      return out;
    }
    f.normal[2] = read_f32_le(data, o + 92, &ok);
    if (!ok) {
      return out;
    }
    f.size[0] = read_i32_le(data, o + 96, &ok);
    if (!ok) {
      return out;
    }
    f.size[1] = read_i32_le(data, o + 100, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(f);
  }
  return out;
}

QVector<ModelFaceRange> parse_q3_model_face_ranges(const QByteArray& data, const BspLump& lump) {
  QVector<ModelFaceRange> out;
  const int stride = 40;
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    bool ok = false;
    ModelFaceRange range{};
    range.first_face = read_i32_le(data, o + 24, &ok);
    if (!ok) {
      return out;
    }
    range.num_faces = read_i32_le(data, o + 28, &ok);
    if (!ok) {
      return out;
    }
    out.push_back(range);
  }
  return out;
}

QVector<bool> build_inline_face_mask(int total_faces, const QVector<ModelFaceRange>& models) {
  QVector<bool> mask;
  if (total_faces <= 0) {
    return mask;
  }
  mask.fill(false, total_faces);
  for (int i = 1; i < models.size(); ++i) {
    const ModelFaceRange& m = models[i];
    if (m.first_face < 0 || m.num_faces <= 0) {
      continue;
    }
    const qint64 start = static_cast<qint64>(m.first_face);
    const qint64 end = std::min(static_cast<qint64>(total_faces), start + static_cast<qint64>(m.num_faces));
    for (qint64 f = std::max<qint64>(0, start); f < end; ++f) {
      mask[static_cast<int>(f)] = true;
    }
  }
  return mask;
}

QVector<QString> parse_q3_textures(const QByteArray& data, const BspLump& lump, int stride) {
  QVector<QString> out;
  if (stride < 64) {
    return out;
  }
  if (lump.length < stride) {
    return out;
  }
  const int count = lump.length / stride;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int o = lump.offset + i * stride;
    char name[64]{};
    if (!read_bytes(data, o, name, 64)) {
      return out;
    }
    QByteArray name_bytes(name, 64);
    const int nul = name_bytes.indexOf('\0');
    if (nul >= 0) {
      name_bytes.truncate(nul);
    }
    out.push_back(QString::fromLatin1(name_bytes));
  }
  return out;
}

QVector<QImage> parse_q3_lightmaps(const QByteArray& data, const BspLump& lump) {
  QVector<QImage> out;
  constexpr int kLightmapDim = 128;
  constexpr int kLightmapChannels = 3;
  constexpr int kLightmapSize = kLightmapDim * kLightmapDim * kLightmapChannels;
  if (lump.length < kLightmapSize || lump.offset < 0 || lump.offset >= data.size()) {
    return out;
  }

  const int count = lump.length / kLightmapSize;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int base = lump.offset + i * kLightmapSize;
    if (base < 0 || base + kLightmapSize > data.size()) {
      break;
    }

    QImage img(kLightmapDim, kLightmapDim, QImage::Format_RGBA8888);
    if (img.isNull()) {
      continue;
    }

    const unsigned char* src = reinterpret_cast<const unsigned char*>(data.constData() + base);
    for (int y = 0; y < kLightmapDim; ++y) {
      unsigned char* dst = img.scanLine(y);
      for (int x = 0; x < kLightmapDim; ++x) {
        const int s = (y * kLightmapDim + x) * kLightmapChannels;
        const int d = x * 4;
        dst[d + 0] = src[s + 0];
        dst[d + 1] = src[s + 1];
        dst[d + 2] = src[s + 2];
        dst[d + 3] = 255;
      }
    }
    out.push_back(std::move(img));
  }

  return out;
}

QVector<QColor> parse_q3_lightmap_colors(const QByteArray& data, const BspLump& lump) {
  QVector<QColor> out;
  const int lightmap_size = 128 * 128 * 3;
  if (lump.length < lightmap_size) {
    return out;
  }
  const int count = lump.length / lightmap_size;
  out.reserve(count);
  for (int i = 0; i < count; ++i) {
    const int base = lump.offset + i * lightmap_size;
    if (base + lightmap_size > data.size()) {
      break;
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data.constData() + base);
    qint64 sum_r = 0;
    qint64 sum_g = 0;
    qint64 sum_b = 0;
    const int samples = 128 * 128;
    for (int s = 0; s < samples; ++s) {
      sum_r += p[s * 3 + 0];
      sum_g += p[s * 3 + 1];
      sum_b += p[s * 3 + 2];
    }
    const int r = static_cast<int>(sum_r / samples);
    const int g = static_cast<int>(sum_g / samples);
    const int b = static_cast<int>(sum_b / samples);
    out.push_back(QColor(r, g, b));
  }
  return out;
}

QVector3D unpack_e5bgr9(quint32 packed) {
  const quint32 biased_exponent = packed >> 27;
  const int exponent = static_cast<int>(biased_exponent) - 24;
  const float multiplier = std::ldexp(1.0f, exponent);

  const float blue = static_cast<float>((packed >> 18) & 0x1FF) * multiplier;
  const float green = static_cast<float>((packed >> 9) & 0x1FF) * multiplier;
  const float red = static_cast<float>(packed & 0x1FF) * multiplier;
  return QVector3D(red, green, blue);
}

unsigned char linear_to_srgb_u8(float linear) {
  linear = std::max(0.0f, linear);
  const float srgb = std::pow(std::min(linear, 1.0f), 1.0f / 2.2f);
  const int v = static_cast<int>(std::lround(srgb * 255.0f));
  return static_cast<unsigned char>(std::clamp(v, 0, 255));
}

int q2_light_stride_bytes(Q2LightSampleFormat format) {
  switch (format) {
    case Q2LightSampleFormat::Gray8:
      return 1;
    case Q2LightSampleFormat::Rgb8:
      return 3;
    case Q2LightSampleFormat::HdrE5Bgr9:
      return 4;
    case Q2LightSampleFormat::None:
    default:
      break;
  }
  return 0;
}

Q2LightSampleSource select_q2_light_source(const QByteArray& data,
                                           const BspHeader& header,
                                           const QHash<QByteArray, BspLump>& bspx) {
  Q2LightSampleSource out;

  auto choose_hdr = [&](const BspLump& lump) -> bool {
    if (!lump_in_bounds(data, lump) || lump.length < 4) {
      return false;
    }

    float peak = 0.0f;
    const int count = lump.length / 4;
    for (int i = 0; i < count; ++i) {
      bool ok = false;
      const quint32 packed = read_u32_le(data, lump.offset + i * 4, &ok);
      if (!ok) {
        break;
      }
      const QVector3D rgb = unpack_e5bgr9(packed);
      peak = std::max(peak, std::max(rgb.x(), std::max(rgb.y(), rgb.z())));
    }

    out.format = Q2LightSampleFormat::HdrE5Bgr9;
    out.lump = lump;
    out.hdr_inv_peak = (peak > 1e-6f) ? (1.0f / peak) : 1.0f;
    return true;
  };

  if (const auto it = bspx.constFind("LIGHTING_E5BGR9"); it != bspx.constEnd()) {
    if (choose_hdr(*it)) {
      return out;
    }
  }

  if (const auto it = bspx.constFind("RGBLIGHTING"); it != bspx.constEnd()) {
    if (lump_in_bounds(data, *it) && it->length >= 3) {
      out.format = Q2LightSampleFormat::Rgb8;
      out.lump = *it;
      out.hdr_inv_peak = 1.0f;
      return out;
    }
  }

  if (header.lumps.size() > Q2_LIGHTING) {
    const BspLump base = header.lumps[Q2_LIGHTING];
    if (lump_in_bounds(data, base) && base.length > 0) {
      out.lump = base;
      out.hdr_inv_peak = 1.0f;
      // Quake2 stores internal lightmaps as RGB; some tools may add trailing pad bytes.
      out.format = (base.length >= 3) ? Q2LightSampleFormat::Rgb8 : Q2LightSampleFormat::Gray8;
    }
  }

  return out;
}

QVector<QVector<int>> parse_q2_face_styles(const QByteArray& data,
                                           const QVector<Q2Face>& faces,
                                           const QHash<QByteArray, BspLump>& bspx) {
  QVector<QVector<int>> out;
  out.resize(faces.size());
  if (faces.isEmpty()) {
    return out;
  }

  auto fill_default = [&]() {
    for (int i = 0; i < faces.size(); ++i) {
      QVector<int> styles;
      styles.reserve(4);
      for (int s = 0; s < 4; ++s) {
        const int style = static_cast<int>(faces[i].styles[s]);
        if (style == 255) {
          break;
        }
        styles.push_back(style);
      }
      out[i] = std::move(styles);
    }
  };

  auto parse_lmstyle16 = [&](const BspLump& lump) -> bool {
    if (!lump_in_bounds(data, lump) || lump.length < static_cast<int>(faces.size() * 2)) {
      return false;
    }
    const int entries = lump.length / 2;
    const int styles_per_face = entries / faces.size();
    if (styles_per_face <= 0) {
      return false;
    }
    for (int i = 0; i < faces.size(); ++i) {
      QVector<int> styles;
      styles.reserve(styles_per_face);
      for (int s = 0; s < styles_per_face; ++s) {
        bool ok = false;
        const quint16 raw = read_u16_le(data, lump.offset + (i * styles_per_face + s) * 2, &ok);
        if (!ok || raw == 0xFFFFu) {
          break;
        }
        styles.push_back(static_cast<int>(raw));
      }
      out[i] = std::move(styles);
    }
    return true;
  };

  auto parse_lmstyle8 = [&](const BspLump& lump) -> bool {
    if (!lump_in_bounds(data, lump) || lump.length < faces.size()) {
      return false;
    }
    const int entries = lump.length;
    const int styles_per_face = entries / faces.size();
    if (styles_per_face <= 0) {
      return false;
    }
    for (int i = 0; i < faces.size(); ++i) {
      QVector<int> styles;
      styles.reserve(styles_per_face);
      for (int s = 0; s < styles_per_face; ++s) {
        const int ofs = lump.offset + (i * styles_per_face + s);
        if (ofs < 0 || ofs >= data.size()) {
          break;
        }
        const unsigned char raw = static_cast<unsigned char>(data.at(ofs));
        if (raw == 255u) {
          break;
        }
        styles.push_back(static_cast<int>(raw));
      }
      out[i] = std::move(styles);
    }
    return true;
  };

  bool parsed = false;
  if (const auto it = bspx.constFind("LMSTYLE16"); it != bspx.constEnd()) {
    parsed = parse_lmstyle16(*it);
  }
  if (!parsed) {
    if (const auto it = bspx.constFind("LMSTYLE"); it != bspx.constEnd()) {
      parsed = parse_lmstyle8(*it);
    }
  }
  if (!parsed) {
    fill_default();
  }
  return out;
}

QVector<int> parse_q2_light_offsets(const QByteArray& data,
                                    const QVector<Q2Face>& faces,
                                    const QHash<QByteArray, BspLump>& bspx) {
  QVector<int> out;
  out.reserve(faces.size());
  for (const Q2Face& f : faces) {
    out.push_back(f.lightofs);
  }

  if (faces.isEmpty()) {
    return out;
  }

  const auto it = bspx.constFind("LMOFFSET");
  if (it == bspx.constEnd()) {
    return out;
  }
  const BspLump lump = *it;
  if (!lump_in_bounds(data, lump) || lump.length < static_cast<int>(faces.size() * 4)) {
    return out;
  }

  for (int i = 0; i < faces.size(); ++i) {
    bool ok = false;
    const int ofs = read_i32_le(data, lump.offset + i * 4, &ok);
    if (ok) {
      out[i] = ofs;
    }
  }

  return out;
}

QVector<Q2DecoupledLightmap> parse_q2_decoupled_lightmaps(const QByteArray& data,
                                                          const QVector<Q2Face>& faces,
                                                          const QHash<QByteArray, BspLump>& bspx) {
  QVector<Q2DecoupledLightmap> out;
  out.resize(faces.size());
  if (faces.isEmpty()) {
    return out;
  }

  const auto it = bspx.constFind("DECOUPLED_LM");
  if (it == bspx.constEnd()) {
    return out;
  }
  const BspLump lump = *it;
  constexpr int kStride = 40;  // uint16 w, uint16 h, int32 ofs, float[2][4]
  if (!lump_in_bounds(data, lump) || lump.length < static_cast<int>(faces.size() * kStride)) {
    return out;
  }

  for (int i = 0; i < faces.size(); ++i) {
    const int base = lump.offset + i * kStride;
    bool ok = false;
    Q2DecoupledLightmap d{};
    d.width = static_cast<int>(read_u16_le(data, base + 0, &ok));
    if (!ok) {
      continue;
    }
    d.height = static_cast<int>(read_u16_le(data, base + 2, &ok));
    if (!ok) {
      continue;
    }
    d.offset = read_i32_le(data, base + 4, &ok);
    if (!ok) {
      continue;
    }
    bool matrix_ok = true;
    for (int r = 0; r < 2 && matrix_ok; ++r) {
      for (int c = 0; c < 4; ++c) {
        d.world_to_lm[r][c] = read_f32_le(data, base + 8 + (r * 4 + c) * 4, &ok);
        if (!ok) {
          matrix_ok = false;
          break;
        }
      }
    }
    d.valid = matrix_ok && d.width > 0 && d.height > 0 && d.offset >= 0;
    out[i] = d;
  }

  return out;
}

bool q2_resolve_style_base_offset(const Q2LightSampleSource& source,
                                  int lightofs,
                                  int style_slot,
                                  int samples,
                                  qint64* out_offset) {
  if (!out_offset || source.format == Q2LightSampleFormat::None || source.lump.length <= 0) {
    return false;
  }
  if (lightofs < 0 || style_slot < 0 || samples <= 0) {
    return false;
  }

  const int stride = q2_light_stride_bytes(source.format);
  if (stride <= 0) {
    return false;
  }

  const qint64 sample_span = static_cast<qint64>(samples) * static_cast<qint64>(stride);
  const qint64 rel_byte_offset = static_cast<qint64>(lightofs) + static_cast<qint64>(style_slot) * sample_span;
  const qint64 rel_sample_offset = (static_cast<qint64>(lightofs) + static_cast<qint64>(style_slot) * samples) *
                                   static_cast<qint64>(stride);

  auto in_range = [&](qint64 rel) {
    return rel >= 0 && rel + sample_span <= source.lump.length;
  };

  // Some tools encode HDR offsets in sample units, while legacy data stores byte offsets.
  if (source.format == Q2LightSampleFormat::HdrE5Bgr9) {
    if (in_range(rel_sample_offset)) {
      *out_offset = static_cast<qint64>(source.lump.offset) + rel_sample_offset;
      return true;
    }
    if (in_range(rel_byte_offset)) {
      *out_offset = static_cast<qint64>(source.lump.offset) + rel_byte_offset;
      return true;
    }
    return false;
  }

  if (in_range(rel_byte_offset)) {
    *out_offset = static_cast<qint64>(source.lump.offset) + rel_byte_offset;
    return true;
  }
  if (stride > 1 && in_range(rel_sample_offset)) {
    *out_offset = static_cast<qint64>(source.lump.offset) + rel_sample_offset;
    return true;
  }
  return false;
}

bool q2_read_light_rgb(const QByteArray& data,
                       const Q2LightSampleSource& source,
                       qint64 style_base_offset,
                       int sample_index,
                       unsigned char* r,
                       unsigned char* g,
                       unsigned char* b) {
  if (!r || !g || !b || sample_index < 0 || style_base_offset < 0) {
    return false;
  }
  const int stride = q2_light_stride_bytes(source.format);
  if (stride <= 0) {
    return false;
  }
  const qint64 ofs = style_base_offset + static_cast<qint64>(sample_index) * stride;
  if (ofs < 0 || ofs + stride > data.size()) {
    return false;
  }

  switch (source.format) {
    case Q2LightSampleFormat::Gray8: {
      const unsigned char v = static_cast<unsigned char>(data.at(static_cast<int>(ofs)));
      *r = v;
      *g = v;
      *b = v;
      return true;
    }
    case Q2LightSampleFormat::Rgb8: {
      const unsigned char* p = reinterpret_cast<const unsigned char*>(data.constData() + ofs);
      *r = p[0];
      *g = p[1];
      *b = p[2];
      return true;
    }
    case Q2LightSampleFormat::HdrE5Bgr9: {
      bool ok = false;
      const quint32 packed = read_u32_le(data, static_cast<int>(ofs), &ok);
      if (!ok) {
        return false;
      }
      const QVector3D linear = unpack_e5bgr9(packed) * source.hdr_inv_peak;
      *r = linear_to_srgb_u8(linear.x());
      *g = linear_to_srgb_u8(linear.y());
      *b = linear_to_srgb_u8(linear.z());
      return true;
    }
    case Q2LightSampleFormat::None:
    default:
      break;
  }
  return false;
}

struct PatchSample {
  QVector3D pos;
  QVector2D st;
  QVector2D lmst;
};

template <typename T>
T bezier3(const T& p0, const T& p1, const T& p2, float t) {
  const float it = 1.0f - t;
  return p0 * (it * it) + p1 * (2.0f * it * t) + p2 * (t * t);
}

PatchSample evaluate_patch_sample(const Q3Vertex (&ctrl)[3][3], float u, float v) {
  QVector3D pos_rows[3];
  QVector2D st_rows[3];
  QVector2D lm_rows[3];
  for (int r = 0; r < 3; ++r) {
    const QVector3D p0(ctrl[r][0].pos.x, ctrl[r][0].pos.y, ctrl[r][0].pos.z);
    const QVector3D p1(ctrl[r][1].pos.x, ctrl[r][1].pos.y, ctrl[r][1].pos.z);
    const QVector3D p2(ctrl[r][2].pos.x, ctrl[r][2].pos.y, ctrl[r][2].pos.z);
    pos_rows[r] = bezier3(p0, p1, p2, u);
    st_rows[r] = bezier3(QVector2D(ctrl[r][0].st[0], ctrl[r][0].st[1]),
                         QVector2D(ctrl[r][1].st[0], ctrl[r][1].st[1]),
                         QVector2D(ctrl[r][2].st[0], ctrl[r][2].st[1]),
                         u);
    lm_rows[r] = bezier3(QVector2D(ctrl[r][0].lmst[0], ctrl[r][0].lmst[1]),
                         QVector2D(ctrl[r][1].lmst[0], ctrl[r][1].lmst[1]),
                         QVector2D(ctrl[r][2].lmst[0], ctrl[r][2].lmst[1]),
                         u);
  }

  PatchSample out;
  out.pos = bezier3(pos_rows[0], pos_rows[1], pos_rows[2], v);
  out.st = bezier3(st_rows[0], st_rows[1], st_rows[2], v);
  out.lmst = bezier3(lm_rows[0], lm_rows[1], lm_rows[2], v);
  return out;
}

int patch_subdivisions(const Q3Vertex (&ctrl)[3][3]) {
  float max_len = 0.0f;
  auto dist = [&](int x0, int y0, int x1, int y1) {
    const QVector3D a(ctrl[y0][x0].pos.x, ctrl[y0][x0].pos.y, ctrl[y0][x0].pos.z);
    const QVector3D b(ctrl[y1][x1].pos.x, ctrl[y1][x1].pos.y, ctrl[y1][x1].pos.z);
    max_len = std::max(max_len, (b - a).length());
  };

  for (int y = 0; y < 3; ++y) {
    dist(0, y, 1, y);
    dist(1, y, 2, y);
  }
  for (int x = 0; x < 3; ++x) {
    dist(x, 0, x, 1);
    dist(x, 1, x, 2);
  }

  const int adaptive = 4 + static_cast<int>(max_len / 96.0f);
  return std::clamp(adaptive, 6, 20);
}

void append_tri(QVector<Tri>& tris,
                const Vec3& a,
                const Vec3& b,
                const Vec3& c,
                const QVector2D& ua,
                const QVector2D& ub,
                const QVector2D& uc,
                const QColor& color,
                const QString& texture,
                bool uv_normalized,
                const QVector2D& lma = QVector2D(0.0f, 0.0f),
                const QVector2D& lmb = QVector2D(0.0f, 0.0f),
                const QVector2D& lmc = QVector2D(0.0f, 0.0f),
                int lightmap_index = -1) {
  Tri t;
  t.a = QVector3D(a.x, a.y, a.z);
  t.b = QVector3D(b.x, b.y, b.z);
  t.c = QVector3D(c.x, c.y, c.z);
  t.ua = ua;
  t.ub = ub;
  t.uc = uc;
  t.lma = lma;
  t.lmb = lmb;
  t.lmc = lmc;
  t.lightmap_index = lightmap_index;
  t.color = color;
  t.texture = texture;
  t.uv_normalized = uv_normalized;
  tris.push_back(t);
}

void append_tri(QVector<Tri>& tris,
                const QVector3D& a,
                const QVector3D& b,
                const QVector3D& c,
                const QVector2D& ua,
                const QVector2D& ub,
                const QVector2D& uc,
                const QColor& color,
                const QString& texture,
                bool uv_normalized,
                const QVector2D& lma = QVector2D(0.0f, 0.0f),
                const QVector2D& lmb = QVector2D(0.0f, 0.0f),
                const QVector2D& lmc = QVector2D(0.0f, 0.0f),
                int lightmap_index = -1) {
  Tri t;
  t.a = a;
  t.b = b;
  t.c = c;
  t.ua = ua;
  t.ub = ub;
  t.uc = uc;
  t.lma = lma;
  t.lmb = lmb;
  t.lmc = lmc;
  t.lightmap_index = lightmap_index;
  t.color = color;
  t.texture = texture;
  t.uv_normalized = uv_normalized;
  tris.push_back(t);
}


QImage render_overhead(const QVector<Tri>& tris, BspPreviewStyle style, int image_size) {
  if (tris.isEmpty()) {
    return {};
  }

  QVector3D mins(FLT_MAX, FLT_MAX, FLT_MAX);
  QVector3D maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);
  for (const Tri& t : tris) {
    mins.setX(qMin(mins.x(), qMin(t.a.x(), qMin(t.b.x(), t.c.x()))));
    mins.setY(qMin(mins.y(), qMin(t.a.y(), qMin(t.b.y(), t.c.y()))));
    mins.setZ(qMin(mins.z(), qMin(t.a.z(), qMin(t.b.z(), t.c.z()))));
    maxs.setX(qMax(maxs.x(), qMax(t.a.x(), qMax(t.b.x(), t.c.x()))));
    maxs.setY(qMax(maxs.y(), qMax(t.a.y(), qMax(t.b.y(), t.c.y()))));
    maxs.setZ(qMax(maxs.z(), qMax(t.a.z(), qMax(t.b.z(), t.c.z()))));
  }

  const float width = maxs.x() - mins.x();
  const float height = maxs.y() - mins.y();
  if (width <= 0.01f || height <= 0.01f) {
    return {};
  }

  const int size = qMax(128, image_size);
  const int pad = qMax(6, size / 64);
  const float scale = (size - pad * 2) / qMax(width, height);

  auto project = [&](const QVector3D& p) -> QPointF {
    const float x = (p.x() - mins.x()) * scale + pad;
    const float y = (maxs.y() - p.y()) * scale + pad;
    return QPointF(x, y);
  };

  QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
  if (style == BspPreviewStyle::Silhouette) {
    img.fill(Qt::transparent);
  } else {
    img.fill(QColor(18, 18, 20));
  }
  QPainter painter(&img);
  const bool smooth = (style != BspPreviewStyle::Silhouette);
  painter.setRenderHint(QPainter::Antialiasing, smooth);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);

  QPen outline(QColor(0, 0, 0, 60), 1.0);
  if (style == BspPreviewStyle::WireframeFlat) {
    outline.setColor(QColor(200, 200, 200, 140));
  }
  if (style == BspPreviewStyle::Silhouette) {
    painter.setPen(Qt::NoPen);
  } else {
    painter.setPen(outline);
  }

  QColor flat_fill(100, 100, 110, 210);
  for (const Tri& t : tris) {
    const QPointF p0 = project(t.a);
    const QPointF p1 = project(t.b);
    const QPointF p2 = project(t.c);
    QPolygonF poly;
    poly.reserve(3);
    poly.push_back(p0);
    poly.push_back(p1);
    poly.push_back(p2);
    QColor fill = (style == BspPreviewStyle::Lightmapped) ? t.color : flat_fill;
    if (style == BspPreviewStyle::WireframeFlat) {
      fill.setAlpha(180);
    }
    if (style == BspPreviewStyle::Silhouette) {
      fill = QColor(255, 255, 255, 255);
    }
    painter.setBrush(fill);
    painter.drawPolygon(poly);
  }

  if (style == BspPreviewStyle::WireframeFlat) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 180), 1.0));
    for (const Tri& t : tris) {
      QPolygonF poly;
      poly.reserve(3);
      poly.push_back(project(t.a));
      poly.push_back(project(t.b));
      poly.push_back(project(t.c));
      painter.drawPolyline(poly);
    }
  }

  return img;
}

bool build_mesh_from_tris(const QVector<Tri>& tris, BspMesh* out) {
  if (!out) {
    return false;
  }
  out->vertices.clear();
  out->indices.clear();
  out->surfaces.clear();

  if (tris.isEmpty()) {
    return false;
  }

  out->mins = QVector3D(FLT_MAX, FLT_MAX, FLT_MAX);
  out->maxs = QVector3D(-FLT_MAX, -FLT_MAX, -FLT_MAX);
  out->vertices.reserve(tris.size() * 3);
  out->indices.reserve(tris.size() * 3);

  BspMeshSurface current_surface;
  bool has_surface = false;

  for (const Tri& t : tris) {
    if (!has_surface ||
        current_surface.texture != t.texture ||
        current_surface.uv_normalized != t.uv_normalized ||
        current_surface.lightmap_index != t.lightmap_index) {
      if (has_surface) {
        out->surfaces.push_back(current_surface);
      }
      current_surface = BspMeshSurface{};
      current_surface.first_index = out->indices.size();
      current_surface.index_count = 0;
      current_surface.texture = t.texture;
      current_surface.uv_normalized = t.uv_normalized;
      current_surface.lightmap_index = t.lightmap_index;
      has_surface = true;
    }

    const QVector3D ab = t.b - t.a;
    const QVector3D ac = t.c - t.a;
    QVector3D n = QVector3D::crossProduct(ab, ac);
    const float len = n.length();
    if (len < 1e-6f) {
      continue;
    }
    n /= len;

    const int base = out->vertices.size();
    const auto add_vert = [&](const QVector3D& p, const QVector2D& uv, const QVector2D& lm_uv) {
      BspMeshVertex v;
      v.pos = p;
      v.normal = n;
      v.color = t.color;
      v.uv = uv;
      v.lightmap_uv = lm_uv;
      out->vertices.push_back(v);
      out->mins.setX(qMin(out->mins.x(), p.x()));
      out->mins.setY(qMin(out->mins.y(), p.y()));
      out->mins.setZ(qMin(out->mins.z(), p.z()));
      out->maxs.setX(qMax(out->maxs.x(), p.x()));
      out->maxs.setY(qMax(out->maxs.y(), p.y()));
      out->maxs.setZ(qMax(out->maxs.z(), p.z()));
    };

    add_vert(t.a, t.ua, t.lma);
    add_vert(t.b, t.ub, t.lmb);
    add_vert(t.c, t.uc, t.lmc);

    out->indices.push_back(static_cast<std::uint32_t>(base + 0));
    out->indices.push_back(static_cast<std::uint32_t>(base + 1));
    out->indices.push_back(static_cast<std::uint32_t>(base + 2));
    current_surface.index_count += 3;
  }

  if (has_surface) {
    out->surfaces.push_back(current_surface);
  }

  if (out->vertices.isEmpty() || out->indices.isEmpty()) {
    return false;
  }

  return true;
}

template <typename EdgeT>
QVector<Tri> build_q1_mesh_impl(const QVector<Vec3>& verts,
                                const QVector<EdgeT>& edges,
                                const QVector<int>& surfedges,
                                const QVector<Q1TexInfo>& texinfo,
                                const QVector<Q1Face>& faces,
                                const QVector<int>& miptex_offsets,
                                const QByteArray& data,
                                const BspLump& tex_lump,
                                const BspLump& light_lump,
                                bool lightmapped) {
  QVector<Tri> tris;
  tris.reserve(faces.size() * 2);

  for (const Q1Face& f : faces) {
    if (f.texinfo < 0 || f.texinfo >= texinfo.size()) {
      continue;
    }
    const QString tex_name = texture_name_from_q1_miptex(data, tex_lump, miptex_offsets, texinfo[f.texinfo].miptex);
    if (is_non_visible_texture_name(tex_name) || is_sky_texture_name(tex_name)) {
      continue;
    }

    if (f.firstedge < 0 || f.numedges < 3 || f.firstedge + f.numedges > surfedges.size()) {
      continue;
    }

    QVector<Vec3> poly;
    QVector<QVector2D> poly_uv;
    poly.reserve(f.numedges);
    poly_uv.reserve(f.numedges);
    for (int i = 0; i < f.numedges; ++i) {
      const int se = surfedges[f.firstedge + i];
      const int edge_index = (se >= 0) ? se : -se;
      if (edge_index < 0 || edge_index >= edges.size()) {
        continue;
      }
      const EdgeT& e = edges[edge_index];
      const int v_index = (se >= 0) ? e.v0 : e.v1;
      if (v_index < 0 || v_index >= verts.size()) {
        continue;
      }
      const Vec3& v = verts[v_index];
      const float s = v.x * texinfo[f.texinfo].vecs[0][0]
                      + v.y * texinfo[f.texinfo].vecs[0][1]
                      + v.z * texinfo[f.texinfo].vecs[0][2]
                      + texinfo[f.texinfo].vecs[0][3];
      const float t = v.x * texinfo[f.texinfo].vecs[1][0]
                      + v.y * texinfo[f.texinfo].vecs[1][1]
                      + v.z * texinfo[f.texinfo].vecs[1][2]
                      + texinfo[f.texinfo].vecs[1][3];
      poly.push_back(v);
      poly_uv.push_back(QVector2D(s, t));
    }
    if (poly.size() < 3 || poly_uv.size() != poly.size()) {
      continue;
    }

    float light = 0.7f;
    if (lightmapped) {
      light = average_light_q1q2(verts, edges, surfedges, texinfo, f, data, light_lump);
    }
    const int shade = static_cast<int>(qBound(40.0f, 40.0f + light * 180.0f, 240.0f));
    const QColor color(shade, shade, shade, 220);

    const Vec3& v0 = poly[0];
    const QVector2D& uv0 = poly_uv[0];
    for (int i = 1; i + 1 < poly.size(); ++i) {
      append_tri(tris,
                 v0, poly[i], poly[i + 1],
                 uv0, poly_uv[i], poly_uv[i + 1],
                 color, tex_name, false);
    }
  }
  return tris;
}

QVector<Tri> build_q1_mesh(const QByteArray& data, const BspHeader& header, bool lightmapped, QString* error) {
  QVector<Tri> tris;
  if (header.lumps.size() < kQ1LumpCount) {
    if (error) {
      *error = "Invalid BSP header.";
    }
    return tris;
  }

  const QVector<Vec3> verts = parse_q1_vertices(data, header.lumps[Q1_VERTICES]);
  const QVector<int> surfedges = parse_surfedges(data, header.lumps[Q1_SURFEDGES]);
  const QVector<Q1TexInfo> texinfo = parse_q1_texinfo(data, header.lumps[Q1_TEXINFO]);
  const QVector<Q1Face> faces = header.q1_bsp2
                                  ? parse_q1_faces_bsp2(data, header.lumps[Q1_FACES])
                                  : parse_q1_faces(data, header.lumps[Q1_FACES]);
  const QVector<int> miptex_offsets = parse_q1_miptex_offsets(data, header.lumps[Q1_TEXTURES]);
  const BspLump tex_lump = header.lumps[Q1_TEXTURES];
  const BspLump light_lump = header.lumps[Q1_LIGHTING];

  if (verts.isEmpty() || faces.isEmpty() || surfedges.isEmpty()) {
    if (error) {
      *error = "Unable to parse BSP geometry.";
    }
    return tris;
  }

  if (header.q1_bsp2) {
    const QVector<Edge32> edges = parse_q1_edges_bsp2(data, header.lumps[Q1_EDGES]);
    if (edges.isEmpty()) {
      if (error) {
        *error = "Unable to parse BSP geometry.";
      }
      return tris;
    }
    return build_q1_mesh_impl(verts, edges, surfedges, texinfo, faces, miptex_offsets, data, tex_lump, light_lump, lightmapped);
  }

  const QVector<Edge16> edges = parse_q1_edges(data, header.lumps[Q1_EDGES]);
  if (edges.isEmpty()) {
    if (error) {
      *error = "Unable to parse BSP geometry.";
    }
    return tris;
  }
  return build_q1_mesh_impl(verts, edges, surfedges, texinfo, faces, miptex_offsets, data, tex_lump, light_lump, lightmapped);
}

QVector<Tri> build_q2_mesh(const QByteArray& data,
                           const BspHeader& header,
                           bool lightmapped,
                           QString* error,
                           QVector<QImage>* out_lightmaps = nullptr) {
  QVector<Tri> tris;
  if (header.lumps.size() < kQ2LumpCount) {
    if (error) {
      *error = "Invalid BSP header.";
    }
    return tris;
  }
  if (out_lightmaps) {
    out_lightmaps->clear();
  }

  const QVector<Vec3> verts = parse_q1_vertices(data, header.lumps[Q2_VERTICES]);
  const QVector<Edge16> edges = parse_q1_edges(data, header.lumps[Q2_EDGES]);
  const QVector<int> surfedges = parse_surfedges(data, header.lumps[Q2_SURFEDGES]);
  const QVector<Q2TexInfo> texinfo = parse_q2_texinfo(data, header.lumps[Q2_TEXINFO]);
  const QVector<Q2Face> faces = parse_q2_faces(data, header.lumps[Q2_FACES]);
  const QVector<ModelFaceRange> models = parse_q2_model_face_ranges(data, header.lumps[Q2_MODELS]);
  const QVector<bool> inline_face_mask = build_inline_face_mask(faces.size(), models);
  const BspLump base_light_lump = lump_in_bounds(data, header.lumps[Q2_LIGHTING])
                                    ? header.lumps[Q2_LIGHTING]
                                    : BspLump{};
  const QHash<QByteArray, BspLump> bspx = parse_bspx_lumps(data, header);
  const Q2LightSampleSource light_source = select_q2_light_source(data, header, bspx);
  const QVector<QVector<int>> face_styles = parse_q2_face_styles(data, faces, bspx);
  const QVector<int> face_lightofs = parse_q2_light_offsets(data, faces, bspx);
  const QVector<Q2DecoupledLightmap> decoupled = parse_q2_decoupled_lightmaps(data, faces, bspx);

  if (verts.isEmpty() || faces.isEmpty() || edges.isEmpty() || surfedges.isEmpty()) {
    if (error) {
      *error = "Unable to parse BSP geometry.";
    }
    return tris;
  }

  tris.reserve(faces.size() * 2);
  constexpr int SURF_NODRAW = 0x80;

  for (int face_index = 0; face_index < faces.size(); ++face_index) {
    const Q2Face& f = faces[face_index];
    const bool is_inline_model_face =
      (face_index >= 0 && face_index < inline_face_mask.size() && inline_face_mask[face_index]);
    if (f.texinfo < 0 || f.texinfo >= texinfo.size()) {
      continue;
    }
    const Q2TexInfo& tx = texinfo[f.texinfo];
    if (!is_inline_model_face && (tx.flags & SURF_NODRAW)) {
      continue;
    }
    const QString tex_name = QString::fromLatin1(tx.texture).trimmed();
    if (is_sky_texture_name(tex_name)) {
      continue;
    }
    if (!is_inline_model_face && is_non_visible_texture_name(tex_name)) {
      continue;
    }

    if (f.firstedge < 0 || f.numedges < 3 || f.firstedge + f.numedges > surfedges.size()) {
      continue;
    }

    QVector<Vec3> poly;
    QVector<QVector2D> poly_uv;
    QVector<QVector2D> poly_decoupled_lm;
    poly.reserve(f.numedges);
    poly_uv.reserve(f.numedges);
    poly_decoupled_lm.reserve(f.numedges);

    const bool has_decoupled = (face_index >= 0 &&
                                face_index < decoupled.size() &&
                                decoupled[face_index].valid);
    const Q2DecoupledLightmap* decoupled_lm = has_decoupled ? &decoupled[face_index] : nullptr;

    for (int i = 0; i < f.numedges; ++i) {
      const int se = surfedges[f.firstedge + i];
      const int edge_index = (se >= 0) ? se : -se;
      if (edge_index < 0 || edge_index >= edges.size()) {
        continue;
      }
      const Edge16& e = edges[edge_index];
      const int v_index = (se >= 0) ? e.v0 : e.v1;
      if (v_index < 0 || v_index >= verts.size()) {
        continue;
      }
      const Vec3& v = verts[v_index];
      const float s = v.x * tx.vecs[0][0]
                      + v.y * tx.vecs[0][1]
                      + v.z * tx.vecs[0][2]
                      + tx.vecs[0][3];
      const float t = v.x * tx.vecs[1][0]
                      + v.y * tx.vecs[1][1]
                      + v.z * tx.vecs[1][2]
                      + tx.vecs[1][3];
      poly.push_back(v);
      poly_uv.push_back(QVector2D(s, t));
      if (decoupled_lm) {
        const float ls = v.x * decoupled_lm->world_to_lm[0][0] +
                         v.y * decoupled_lm->world_to_lm[0][1] +
                         v.z * decoupled_lm->world_to_lm[0][2] +
                         decoupled_lm->world_to_lm[0][3];
        const float lt = v.x * decoupled_lm->world_to_lm[1][0] +
                         v.y * decoupled_lm->world_to_lm[1][1] +
                         v.z * decoupled_lm->world_to_lm[1][2] +
                         decoupled_lm->world_to_lm[1][3];
        poly_decoupled_lm.push_back(QVector2D(ls, lt));
      }
    }
    if (poly.size() < 3 || poly_uv.size() != poly.size()) {
      continue;
    }

    Q1TexInfo q1tx{};
    std::memcpy(q1tx.vecs, tx.vecs, sizeof(q1tx.vecs));
    QVector<Q1TexInfo> tmp;
    tmp.push_back(q1tx);
    Q1Face q1f{};
    q1f.firstedge = f.firstedge;
    q1f.numedges = f.numedges;
    q1f.texinfo = 0;
    q1f.lightofs = f.lightofs;

    float light = 0.7f;
    if (lightmapped && base_light_lump.length > 0) {
      light = average_light_q1q2(verts, edges, surfedges, tmp, q1f, data, base_light_lump);
    }
    const int shade = static_cast<int>(qBound(40.0f, 40.0f + light * 180.0f, 240.0f));
    QColor color(shade, shade, shade, 220);

    int lightmap_index = -1;
    QVector<QVector2D> poly_lm_uv(poly_uv.size(), QVector2D(0.0f, 0.0f));

    if (lightmapped && !poly_uv.isEmpty() && light_source.format != Q2LightSampleFormat::None) {
      int lm_w = 0;
      int lm_h = 0;
      int lm_lightofs = (face_index >= 0 && face_index < face_lightofs.size()) ? face_lightofs[face_index] : f.lightofs;
      QVector<QVector2D> lm_coord(poly_uv.size(), QVector2D(0.0f, 0.0f));

      if (decoupled_lm && poly_decoupled_lm.size() == poly_uv.size()) {
        lm_w = decoupled_lm->width;
        lm_h = decoupled_lm->height;
        lm_lightofs = decoupled_lm->offset;
        lm_coord = poly_decoupled_lm;
      } else {
        float min_s = poly_uv[0].x();
        float max_s = min_s;
        float min_t = poly_uv[0].y();
        float max_t = min_t;
        for (int i = 1; i < poly_uv.size(); ++i) {
          const QVector2D uv = poly_uv[i];
          min_s = std::min(min_s, uv.x());
          max_s = std::max(max_s, uv.x());
          min_t = std::min(min_t, uv.y());
          max_t = std::max(max_t, uv.y());
        }

        const int smin = static_cast<int>(std::floor(min_s / 16.0f));
        const int smax = static_cast<int>(std::ceil(max_s / 16.0f));
        const int tmin = static_cast<int>(std::floor(min_t / 16.0f));
        const int tmax = static_cast<int>(std::ceil(max_t / 16.0f));
        lm_w = smax - smin + 1;
        lm_h = tmax - tmin + 1;

        for (int i = 0; i < poly_uv.size(); ++i) {
          const float ls = poly_uv[i].x() / 16.0f - static_cast<float>(smin);
          const float lt = poly_uv[i].y() / 16.0f - static_cast<float>(tmin);
          lm_coord[i] = QVector2D(ls, lt);
        }
      }

      if (lm_w > 0 && lm_h > 0 && lm_lightofs >= 0) {
        const int samples = lm_w * lm_h;
        const QVector<int>& styles = (face_index >= 0 && face_index < face_styles.size())
                                       ? face_styles[face_index]
                                       : QVector<int>();
        int style_slot = -1;
        for (int i = 0; i < styles.size(); ++i) {
          if (styles[i] >= 0) {
            style_slot = i;
            break;
          }
        }

        qint64 style_base = -1;
        if (style_slot >= 0 &&
            q2_resolve_style_base_offset(light_source, lm_lightofs, style_slot, samples, &style_base)) {
          qint64 sum_r = 0;
          qint64 sum_g = 0;
          qint64 sum_b = 0;
          int valid_samples = 0;

          QImage lm_img;
          if (out_lightmaps) {
            lm_img = QImage(lm_w, lm_h, QImage::Format_RGBA8888);
            if (!lm_img.isNull()) {
              lm_img.fill(Qt::black);
            }
          }

          for (int y = 0; y < lm_h; ++y) {
            unsigned char* dst = lm_img.isNull() ? nullptr : lm_img.scanLine(y);
            for (int x = 0; x < lm_w; ++x) {
              const int sample_index = y * lm_w + x;
              unsigned char r = 0;
              unsigned char g = 0;
              unsigned char b = 0;
              if (!q2_read_light_rgb(data, light_source, style_base, sample_index, &r, &g, &b)) {
                continue;
              }

              sum_r += r;
              sum_g += g;
              sum_b += b;
              ++valid_samples;

              if (dst) {
                const int d = x * 4;
                dst[d + 0] = r;
                dst[d + 1] = g;
                dst[d + 2] = b;
                dst[d + 3] = 255;
              }
            }
          }

          if (valid_samples > 0) {
            color = QColor(static_cast<int>(sum_r / valid_samples),
                           static_cast<int>(sum_g / valid_samples),
                           static_cast<int>(sum_b / valid_samples),
                           220);
            if (out_lightmaps && !lm_img.isNull()) {
              lightmap_index = out_lightmaps->size();
              out_lightmaps->push_back(std::move(lm_img));
            }
          }

          for (int i = 0; i < lm_coord.size(); ++i) {
            const float u = (lm_coord[i].x() + 0.5f) / static_cast<float>(lm_w);
            const float v = (lm_coord[i].y() + 0.5f) / static_cast<float>(lm_h);
            poly_lm_uv[i] = QVector2D(std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f));
          }
        }
      }
    }

    const Vec3& v0 = poly[0];
    const QVector2D& uv0 = poly_uv[0];
    for (int i = 1; i + 1 < poly.size(); ++i) {
      append_tri(tris,
                 v0, poly[i], poly[i + 1],
                 uv0, poly_uv[i], poly_uv[i + 1],
                 color,
                 tex_name,
                 false,
                 poly_lm_uv[0],
                 poly_lm_uv[i],
                 poly_lm_uv[i + 1],
                 lightmap_index);
    }
  }
  return tris;
}

QVector<Tri> build_q3_mesh(const QByteArray& data,
                           const BspHeader& header,
                           bool lightmapped,
                           QString* error,
                           QVector<QImage>* out_lightmaps = nullptr) {
  QVector<Tri> tris;
  if (header.lumps.size() < header.lump_count) {
    if (error) {
      *error = "Invalid BSP header.";
    }
    return tris;
  }
  if (header.q3_vertices_lump < 0 || header.q3_vertices_lump >= header.lumps.size() ||
      header.q3_meshverts_lump < 0 || header.q3_meshverts_lump >= header.lumps.size() ||
      header.q3_faces_lump < 0 || header.q3_faces_lump >= header.lumps.size() ||
      header.q3_textures_lump < 0 || header.q3_textures_lump >= header.lumps.size() ||
      header.q3_lightmaps_lump < 0 || header.q3_lightmaps_lump >= header.lumps.size()) {
    if (error) {
      *error = "Unsupported BSP lump layout.";
    }
    return tris;
  }

  const QVector<Q3Vertex> verts = parse_q3_vertices(data, header.lumps[header.q3_vertices_lump], header.q3_vertex_stride);
  const QVector<int> meshverts = parse_q3_meshverts(data, header.lumps[header.q3_meshverts_lump]);
  const QVector<Q3Face> faces = parse_q3_faces(data, header.lumps[header.q3_faces_lump], header.q3_face_stride);
  QVector<ModelFaceRange> models;
  if (header.q3_models_lump >= 0 &&
      header.q3_models_lump < header.lumps.size() &&
      lump_in_bounds(data, header.lumps[header.q3_models_lump])) {
    models = parse_q3_model_face_ranges(data, header.lumps[header.q3_models_lump]);
  }
  const QVector<bool> inline_face_mask = build_inline_face_mask(faces.size(), models);
  const QVector<QString> shaders = parse_q3_textures(data, header.lumps[header.q3_textures_lump], header.q3_texture_stride);
  const QVector<QColor> lightmap_colors = parse_q3_lightmap_colors(data, header.lumps[header.q3_lightmaps_lump]);
  if (out_lightmaps) {
    out_lightmaps->clear();
    if (lightmapped) {
      *out_lightmaps = parse_q3_lightmaps(data, header.lumps[header.q3_lightmaps_lump]);
    }
  }

  if (verts.isEmpty() || faces.isEmpty()) {
    if (error) {
      *error = "Unable to parse BSP geometry.";
    }
    return tris;
  }

  tris.reserve(faces.size() * 2);

  auto face_color = [&](const Q3Face& f) -> QColor {
    if (!lightmapped) {
      return QColor(160, 160, 170, 220);
    }
    if (f.lmIndex >= 0 && f.lmIndex < lightmap_colors.size()) {
      QColor c = lightmap_colors[f.lmIndex];
      if (!c.isValid()) {
        return QColor(120, 120, 120, 220);
      }
      c.setAlpha(220);
      return c;
    }
    return QColor(120, 120, 120, 220);
  };

  for (int face_index = 0; face_index < faces.size(); ++face_index) {
    const Q3Face& f = faces[face_index];
    const bool is_inline_model_face =
      (face_index >= 0 && face_index < inline_face_mask.size() && inline_face_mask[face_index]);
    if (f.shader >= 0 && f.shader < shaders.size()) {
      const QString& shader = shaders[f.shader];
      if (is_sky_texture_name(shader)) {
        continue;
      }
      if (!is_inline_model_face && is_non_visible_texture_name(shader)) {
        continue;
      }
    }

    if (f.type == 4) {
      continue;
    }

    const QColor color = face_color(f);
    QString shader_name;
    if (f.shader >= 0 && f.shader < shaders.size()) {
      shader_name = shaders[f.shader];
    }
    const int lightmap_index = lightmapped ? f.lmIndex : -1;

    // MST_PLANAR (1), MST_TRIANGLE_SOUP (3), and Wolf:ET MST_FOLIAGE (5).
    if (f.type == 1 || f.type == 3 || f.type == 5) {
      if (f.firstVert < 0 || f.numVerts <= 0 || f.firstVert + f.numVerts > verts.size()) {
        continue;
      }
      if (f.firstMeshVert < 0 || f.numMeshVerts <= 0 || f.firstMeshVert + f.numMeshVerts > meshverts.size()) {
        continue;
      }
      for (int i = 0; i + 2 < f.numMeshVerts; i += 3) {
        const int a = f.firstVert + meshverts[f.firstMeshVert + i + 0];
        const int b = f.firstVert + meshverts[f.firstMeshVert + i + 1];
        const int c = f.firstVert + meshverts[f.firstMeshVert + i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= verts.size() || b >= verts.size() || c >= verts.size()) {
          continue;
        }
        append_tri(tris,
                   QVector3D(verts[a].pos.x, verts[a].pos.y, verts[a].pos.z),
                   QVector3D(verts[b].pos.x, verts[b].pos.y, verts[b].pos.z),
                   QVector3D(verts[c].pos.x, verts[c].pos.y, verts[c].pos.z),
                   QVector2D(verts[a].st[0], verts[a].st[1]),
                   QVector2D(verts[b].st[0], verts[b].st[1]),
                   QVector2D(verts[c].st[0], verts[c].st[1]),
                   color,
                   shader_name,
                   true,
                   QVector2D(verts[a].lmst[0], verts[a].lmst[1]),
                   QVector2D(verts[b].lmst[0], verts[b].lmst[1]),
                   QVector2D(verts[c].lmst[0], verts[c].lmst[1]),
                   lightmap_index);
      }
    } else if (f.type == 2) {
      if (f.firstVert < 0 || f.numVerts <= 0 || f.firstVert + f.numVerts > verts.size()) {
        continue;
      }
      const int w = f.size[0];
      const int h = f.size[1];
      if (w < 3 || h < 3) {
        continue;
      }

      for (int py = 0; py + 2 < h; py += 2) {
        for (int px = 0; px + 2 < w; px += 2) {
          Q3Vertex ctrl[3][3];
          bool patch_valid = true;
          for (int cy = 0; cy < 3 && patch_valid; ++cy) {
            for (int cx = 0; cx < 3; ++cx) {
              const int local_idx = (py + cy) * w + (px + cx);
              if (local_idx < 0 || local_idx >= f.numVerts) {
                patch_valid = false;
                break;
              }
              const int vi = f.firstVert + local_idx;
              if (vi < 0 || vi >= verts.size()) {
                patch_valid = false;
                break;
              }
              ctrl[cy][cx] = verts[vi];
            }
          }
          if (!patch_valid) {
            continue;
          }

          const int subdiv = patch_subdivisions(ctrl);
          const int stride = subdiv + 1;
          QVector<PatchSample> patch_verts;
          patch_verts.resize(stride * stride);

          for (int y = 0; y <= subdiv; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(subdiv);
            for (int x = 0; x <= subdiv; ++x) {
              const float u = static_cast<float>(x) / static_cast<float>(subdiv);
              patch_verts[y * stride + x] = evaluate_patch_sample(ctrl, u, v);
            }
          }

          for (int y = 0; y < subdiv; ++y) {
            for (int x = 0; x < subdiv; ++x) {
              const PatchSample& p00 = patch_verts[y * stride + x];
              const PatchSample& p10 = patch_verts[y * stride + (x + 1)];
              const PatchSample& p01 = patch_verts[(y + 1) * stride + x];
              const PatchSample& p11 = patch_verts[(y + 1) * stride + (x + 1)];

              append_tri(tris,
                         p00.pos,
                         p10.pos,
                         p01.pos,
                         p00.st,
                         p10.st,
                         p01.st,
                         color,
                         shader_name,
                         true,
                         p00.lmst,
                         p10.lmst,
                         p01.lmst,
                         lightmap_index);
              append_tri(tris,
                         p10.pos,
                         p11.pos,
                         p01.pos,
                         p10.st,
                         p11.st,
                         p01.st,
                         color,
                         shader_name,
                         true,
                         p10.lmst,
                         p11.lmst,
                         p01.lmst,
                         lightmap_index);
            }
          }
        }
      }
    }
  }

  return tris;
}
}  // namespace

BspPreviewResult render_bsp_preview_bytes(const QByteArray& bytes,
                                          const QString& file_name,
                                          BspPreviewStyle style,
                                          int image_size) {
  BspPreviewResult out;
  if (bytes.isEmpty()) {
    out.error = "Empty BSP file.";
    return out;
  }

  BspHeader header;
  if (!parse_header(bytes, &header, &out.error)) {
    if (out.error.isEmpty()) {
      out.error = "Unable to parse BSP header.";
    }
    return out;
  }

  QVector<Tri> tris;
  const bool lightmapped = (style == BspPreviewStyle::Lightmapped);
  if (header.family == BspFamily::Quake1) {
    tris = build_q1_mesh(bytes, header, lightmapped, &out.error);
  } else if (header.family == BspFamily::Quake2) {
    tris = build_q2_mesh(bytes, header, lightmapped, &out.error);
  } else if (header.family == BspFamily::Quake3) {
    tris = build_q3_mesh(bytes, header, lightmapped, &out.error);
  } else {
    out.error = QString("Unsupported BSP format: %1 version %2")
      .arg(header.magic, QString::number(header.version));
    return out;
  }

  if (tris.isEmpty()) {
    if (out.error.isEmpty()) {
      out.error = "No visible geometry found.";
    }
    return out;
  }

  QImage img = render_overhead(tris, style, image_size);
  if (img.isNull()) {
    out.error = "Unable to render BSP preview.";
    return out;
  }

  out.image = std::move(img);
  out.error.clear();
  Q_UNUSED(file_name);
  return out;
}

BspPreviewResult render_bsp_preview_file(const QString& file_path,
                                         BspPreviewStyle style,
                                         int image_size) {
  BspPreviewResult out;
  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    out.error = "Unable to open BSP file.";
    return out;
  }
  const QByteArray bytes = f.readAll();
  return render_bsp_preview_bytes(bytes, QFileInfo(file_path).fileName(), style, image_size);
}

bool load_bsp_mesh_bytes(const QByteArray& bytes,
                         const QString& file_name,
                         BspMesh* out,
                         QString* error,
                         bool use_lightmap) {
  if (error) {
    error->clear();
  }
  if (!out) {
    if (error) {
      *error = "No BSP mesh output provided.";
    }
    return false;
  }

  out->vertices.clear();
  out->indices.clear();
  out->surfaces.clear();
  out->lightmaps.clear();
  out->mins = QVector3D(0, 0, 0);
  out->maxs = QVector3D(0, 0, 0);

  if (bytes.isEmpty()) {
    if (error) {
      *error = "Empty BSP file.";
    }
    return false;
  }

  BspHeader header;
  if (!parse_header(bytes, &header, error)) {
    if (error && error->isEmpty()) {
      *error = "Unable to parse BSP header.";
    }
    return false;
  }

  QVector<Tri> tris;
  if (header.family == BspFamily::Quake1) {
    tris = build_q1_mesh(bytes, header, use_lightmap, error);
  } else if (header.family == BspFamily::Quake2) {
    tris = build_q2_mesh(bytes, header, use_lightmap, error, &out->lightmaps);
  } else if (header.family == BspFamily::Quake3) {
    tris = build_q3_mesh(bytes, header, use_lightmap, error, &out->lightmaps);
  } else {
    if (error) {
      *error = QString("Unsupported BSP format: %1 version %2")
        .arg(header.magic, QString::number(header.version));
    }
    return false;
  }

  if (tris.isEmpty()) {
    if (error && error->isEmpty()) {
      *error = "No visible geometry found.";
    }
    return false;
  }

  if (!build_mesh_from_tris(tris, out)) {
    if (error && error->isEmpty()) {
      *error = "Unable to build BSP mesh.";
    }
    return false;
  }

  Q_UNUSED(file_name);
  return true;
}

bool load_bsp_mesh_file(const QString& file_path,
                        BspMesh* out,
                        QString* error,
                        bool use_lightmap) {
  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open BSP file.";
    }
    return false;
  }
  const QByteArray bytes = f.readAll();
  return load_bsp_mesh_bytes(bytes, QFileInfo(file_path).fileName(), out, error, use_lightmap);
}

QHash<QString, QImage> extract_bsp_embedded_textures_bytes(const QByteArray& bytes,
                                                           const QVector<QRgb>* quake_palette,
                                                           QString* error) {
  if (error) {
    error->clear();
  }

  QHash<QString, QImage> out;
  if (bytes.isEmpty()) {
    return out;
  }

  BspHeader header;
  if (!parse_header(bytes, &header, error)) {
    return out;
  }

  if (header.family != BspFamily::Quake1 ||
      (!is_q1_legacy_compatible_bsp_version(header.version) && !header.q1_bsp2)) {
    return out;
  }

  if (header.lumps.size() < kQ1LumpCount) {
    return out;
  }

  const BspLump& tex_lump_info = header.lumps[Q1_TEXTURES];
  if (tex_lump_info.length <= 0 || tex_lump_info.offset < 0) {
    return out;
  }

  const QVector<int> miptex_offsets = parse_q1_miptex_offsets(bytes, tex_lump_info);
  if (miptex_offsets.isEmpty()) {
    return out;
  }

  for (int i = 0; i < miptex_offsets.size(); ++i) {
    const int base_rel = miptex_offsets[i];
    const qint64 base_abs = static_cast<qint64>(tex_lump_info.offset) + static_cast<qint64>(base_rel);
    if (base_rel < 0 ||
        static_cast<qint64>(base_rel) + 40 > tex_lump_info.length ||
        base_abs < 0 ||
        base_abs + 40 > bytes.size()) {
      continue;
    }

    const QString name = texture_name_from_q1_miptex(bytes, tex_lump_info, miptex_offsets, i).toLower();
    if (name.isEmpty()) {
      continue;
    }

    bool ok = false;
    const quint32 width_u = read_u32_le(bytes, static_cast<int>(base_abs) + 16, &ok);
    if (!ok) {
      continue;
    }
    const quint32 height_u = read_u32_le(bytes, static_cast<int>(base_abs) + 20, &ok);
    if (!ok || width_u == 0 || height_u == 0) {
      continue;
    }
    const quint32 ofs0 = read_u32_le(bytes, static_cast<int>(base_abs) + 24, &ok);
    if (!ok || ofs0 == 0) {
      continue;
    }

    const int width = static_cast<int>(width_u);
    const int height = static_cast<int>(height_u);
    const qint64 mip0 = static_cast<qint64>(width) * static_cast<qint64>(height);
    const qint64 mip1 = static_cast<qint64>(qMax(1, width / 2)) * static_cast<qint64>(qMax(1, height / 2));
    const qint64 mip2 = static_cast<qint64>(qMax(1, width / 4)) * static_cast<qint64>(qMax(1, height / 4));
    const qint64 mip3 = static_cast<qint64>(qMax(1, width / 8)) * static_cast<qint64>(qMax(1, height / 8));
    const qint64 mip_total = mip0 + mip1 + mip2 + mip3;
    const qint64 slice_len = static_cast<qint64>(ofs0) + mip_total;
    if (slice_len <= 0 ||
        static_cast<qint64>(base_rel) + slice_len > tex_lump_info.length ||
        base_abs + slice_len > bytes.size()) {
      continue;
    }

    const QByteArray mip_bytes = bytes.mid(static_cast<int>(base_abs), static_cast<int>(slice_len));
    QString mip_err;
    const QImage img = decode_miptex_image(mip_bytes, quake_palette, 0, name, &mip_err);
    if (!img.isNull()) {
      out.insert(name, img);
    }
  }

  return out;
}

int bsp_version_bytes(const QByteArray& bytes, QString* error) {
  if (error) {
    error->clear();
  }
  BspHeader header;
  if (!parse_header(bytes, &header, error)) {
    return -1;
  }
  return header.version;
}

BspFamily bsp_family_bytes(const QByteArray& bytes, QString* error) {
  if (error) {
    error->clear();
  }
  BspHeader header;
  if (!parse_header(bytes, &header, error)) {
    return BspFamily::Unknown;
  }
  return header.family;
}
