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
  QVector<BspLump> lumps;
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
  QColor color;
  QString texture;
  bool uv_normalized = false;
};

constexpr int kQ1Version = 29;
constexpr int kQ2Version = 38;
constexpr int kQ3Version = 46;
constexpr int kQLVersion = 47;

constexpr int kQ1LumpCount = 15;
constexpr int kQ2LumpCount = 19;
constexpr int kQ3LumpCount = 17;

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

float read_f32_le(const QByteArray& data, int offset, bool* ok = nullptr) {
  quint32 u = read_u32_le(data, offset, ok);
  float f = 0.0f;
  std::memcpy(&f, &u, sizeof(float));
  return f;
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
  char magic[5]{};
  if (!read_bytes(data, 0, magic, 4)) {
    if (error) {
      *error = "Unable to read BSP header.";
    }
    return false;
  }
  bool ok = false;
  const int version = read_i32_le(data, 4, &ok);
  if (!ok) {
    if (error) {
      *error = "Unable to read BSP version.";
    }
    return false;
  }

  int lump_count = 0;
  if (version == kQ1Version) {
    lump_count = kQ1LumpCount;
  } else if (version == kQ2Version) {
    lump_count = kQ2LumpCount;
  } else if (version == kQ3Version || version == kQLVersion) {
    lump_count = kQ3LumpCount;
  } else {
    if (error) {
      *error = QString("Unsupported BSP version: %1").arg(version);
    }
    return false;
  }

  const int header_size = 8 + lump_count * 8;
  if (data.size() < header_size) {
    if (error) {
      *error = "Truncated BSP header.";
    }
    return false;
  }

  QVector<BspLump> lumps;
  lumps.reserve(lump_count);
  int offset = 8;
  for (int i = 0; i < lump_count; ++i) {
    bool ok1 = false;
    bool ok2 = false;
    const int lofs = read_i32_le(data, offset, &ok1);
    const int llen = read_i32_le(data, offset + 4, &ok2);
    if (!ok1 || !ok2) {
      if (error) {
        *error = "Failed to parse BSP lumps.";
      }
      return false;
    }
    lumps.push_back(BspLump{lofs, llen});
    offset += 8;
  }

  out->magic = QString::fromLatin1(magic, 4);
  out->version = version;
  out->lumps = std::move(lumps);
  return true;
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

struct Edge16 {
  quint16 v0 = 0;
  quint16 v1 = 0;
};

struct Q3Vertex {
  Vec3 pos;
  float st[2]{};
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

QString texture_name_from_q1_miptex(const QByteArray& tex_lump, const QVector<int>& offsets, int index) {
  if (index < 0 || index >= offsets.size()) {
    return {};
  }
  const int base = offsets[index];
  if (base < 0 || base + 16 > tex_lump.size()) {
    return {};
  }
  QByteArray name_bytes = tex_lump.mid(base, 16);
  const int null_pos = name_bytes.indexOf('\0');
  if (null_pos >= 0) {
    name_bytes.truncate(null_pos);
  }
  return QString::fromLatin1(name_bytes);
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

float average_light_q1q2(const QVector<Vec3>& verts,
                         const QVector<Edge16>& edges,
                         const QVector<int>& surfedges,
                         const QVector<Q1TexInfo>& texinfo,
                         const Q1Face& face,
                         const QByteArray& lightdata) {
  if (face.texinfo < 0 || face.texinfo >= texinfo.size() || face.lightofs < 0) {
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
    const Edge16& e = edges[edge_index];
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
  if (face.lightofs < 0 || face.lightofs + count > lightdata.size()) {
    return 0.6f;
  }

  const unsigned char* p = reinterpret_cast<const unsigned char*>(lightdata.constData() + face.lightofs);
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

QVector<Q3Vertex> parse_q3_vertices(const QByteArray& data, const BspLump& lump) {
  QVector<Q3Vertex> out;
  const int stride = 44;
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

QVector<Q3Face> parse_q3_faces(const QByteArray& data, const BspLump& lump) {
  QVector<Q3Face> out;
  const int stride = 104;
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
    out.push_back(f);
  }
  return out;
}

QVector<QString> parse_q3_textures(const QByteArray& data, const BspLump& lump) {
  QVector<QString> out;
  const int stride = 72;
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

void append_tri(QVector<Tri>& tris,
                const Vec3& a,
                const Vec3& b,
                const Vec3& c,
                const QVector2D& ua,
                const QVector2D& ub,
                const QVector2D& uc,
                const QColor& color,
                const QString& texture,
                bool uv_normalized) {
  Tri t;
  t.a = QVector3D(a.x, a.y, a.z);
  t.b = QVector3D(b.x, b.y, b.z);
  t.c = QVector3D(c.x, c.y, c.z);
  t.ua = ua;
  t.ub = ub;
  t.uc = uc;
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
                bool uv_normalized) {
  Tri t;
  t.a = a;
  t.b = b;
  t.c = c;
  t.ua = ua;
  t.ub = ub;
  t.uc = uc;
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
        current_surface.uv_normalized != t.uv_normalized) {
      if (has_surface) {
        out->surfaces.push_back(current_surface);
      }
      current_surface = BspMeshSurface{};
      current_surface.first_index = out->indices.size();
      current_surface.index_count = 0;
      current_surface.texture = t.texture;
      current_surface.uv_normalized = t.uv_normalized;
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
    const auto add_vert = [&](const QVector3D& p, const QVector2D& uv) {
      BspMeshVertex v;
      v.pos = p;
      v.normal = n;
      v.color = t.color;
      v.uv = uv;
      out->vertices.push_back(v);
      out->mins.setX(qMin(out->mins.x(), p.x()));
      out->mins.setY(qMin(out->mins.y(), p.y()));
      out->mins.setZ(qMin(out->mins.z(), p.z()));
      out->maxs.setX(qMax(out->maxs.x(), p.x()));
      out->maxs.setY(qMax(out->maxs.y(), p.y()));
      out->maxs.setZ(qMax(out->maxs.z(), p.z()));
    };

    add_vert(t.a, t.ua);
    add_vert(t.b, t.ub);
    add_vert(t.c, t.uc);

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

QVector<Tri> build_q1_mesh(const QByteArray& data, const BspHeader& header, bool lightmapped, QString* error) {
  QVector<Tri> tris;
  if (header.lumps.size() < kQ1LumpCount) {
    if (error) {
      *error = "Invalid BSP header.";
    }
    return tris;
  }

  const QVector<Vec3> verts = parse_q1_vertices(data, header.lumps[Q1_VERTICES]);
  const QVector<Edge16> edges = parse_q1_edges(data, header.lumps[Q1_EDGES]);
  const QVector<int> surfedges = parse_surfedges(data, header.lumps[Q1_SURFEDGES]);
  const QVector<Q1TexInfo> texinfo = parse_q1_texinfo(data, header.lumps[Q1_TEXINFO]);
  const QVector<Q1Face> faces = parse_q1_faces(data, header.lumps[Q1_FACES]);
  const QVector<int> miptex_offsets = parse_q1_miptex_offsets(data, header.lumps[Q1_TEXTURES]);
  const QByteArray tex_lump = data.mid(header.lumps[Q1_TEXTURES].offset, header.lumps[Q1_TEXTURES].length);
  const QByteArray lightdata = data.mid(header.lumps[Q1_LIGHTING].offset, header.lumps[Q1_LIGHTING].length);

  if (verts.isEmpty() || faces.isEmpty() || edges.isEmpty() || surfedges.isEmpty()) {
    if (error) {
      *error = "Unable to parse BSP geometry.";
    }
    return tris;
  }

  tris.reserve(faces.size() * 2);

  for (const Q1Face& f : faces) {
    if (f.texinfo < 0 || f.texinfo >= texinfo.size()) {
      continue;
    }
    const QString tex_name = texture_name_from_q1_miptex(tex_lump, miptex_offsets, texinfo[f.texinfo].miptex);
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
      const Edge16& e = edges[edge_index];
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
      light = average_light_q1q2(verts, edges, surfedges, texinfo, f, lightdata);
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

QVector<Tri> build_q2_mesh(const QByteArray& data, const BspHeader& header, bool lightmapped, QString* error) {
  QVector<Tri> tris;
  if (header.lumps.size() < kQ2LumpCount) {
    if (error) {
      *error = "Invalid BSP header.";
    }
    return tris;
  }

  const QVector<Vec3> verts = parse_q1_vertices(data, header.lumps[Q2_VERTICES]);
  const QVector<Edge16> edges = parse_q1_edges(data, header.lumps[Q2_EDGES]);
  const QVector<int> surfedges = parse_surfedges(data, header.lumps[Q2_SURFEDGES]);
  const QVector<Q2TexInfo> texinfo = parse_q2_texinfo(data, header.lumps[Q2_TEXINFO]);
  const QVector<Q2Face> faces = parse_q2_faces(data, header.lumps[Q2_FACES]);
  const QByteArray lightdata = data.mid(header.lumps[Q2_LIGHTING].offset, header.lumps[Q2_LIGHTING].length);

  if (verts.isEmpty() || faces.isEmpty() || edges.isEmpty() || surfedges.isEmpty()) {
    if (error) {
      *error = "Unable to parse BSP geometry.";
    }
    return tris;
  }

  tris.reserve(faces.size() * 2);
  constexpr int SURF_NODRAW = 0x80;

  for (const Q2Face& f : faces) {
    if (f.texinfo < 0 || f.texinfo >= texinfo.size()) {
      continue;
    }
    const Q2TexInfo& tx = texinfo[f.texinfo];
    if (tx.flags & SURF_NODRAW) {
      continue;
    }
    const QString tex_name = QString::fromLatin1(tx.texture).trimmed();
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
    if (lightmapped) {
      light = average_light_q1q2(verts, edges, surfedges, tmp, q1f, lightdata);
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

QVector<Tri> build_q3_mesh(const QByteArray& data, const BspHeader& header, bool lightmapped, QString* error) {
  QVector<Tri> tris;
  if (header.lumps.size() < kQ3LumpCount) {
    if (error) {
      *error = "Invalid BSP header.";
    }
    return tris;
  }

  const QVector<Q3Vertex> verts = parse_q3_vertices(data, header.lumps[Q3_VERTICES]);
  const QVector<int> meshverts = parse_q3_meshverts(data, header.lumps[Q3_MESHVERTS]);
  const QVector<Q3Face> faces = parse_q3_faces(data, header.lumps[Q3_FACES]);
  const QVector<QString> shaders = parse_q3_textures(data, header.lumps[Q3_TEXTURES]);
  const QVector<QColor> lightmap_colors = parse_q3_lightmap_colors(data, header.lumps[Q3_LIGHTMAPS]);

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

  for (const Q3Face& f : faces) {
    if (f.shader >= 0 && f.shader < shaders.size()) {
      if (is_non_visible_texture_name(shaders[f.shader]) || is_sky_texture_name(shaders[f.shader])) {
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

    if (f.type == 1 || f.type == 3) {
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
                   color, shader_name, true);
      }
    } else if (f.type == 2) {
      if (f.firstVert < 0 || f.numVerts <= 0 || f.firstVert + f.numVerts > verts.size()) {
        continue;
      }
      const int w = f.size[0];
      const int h = f.size[1];
      if (w <= 1 || h <= 1) {
        continue;
      }
      for (int y = 0; y + 1 < h; ++y) {
        for (int x = 0; x + 1 < w; ++x) {
          const int idx0 = f.firstVert + y * w + x;
          const int idx1 = idx0 + 1;
          const int idx2 = idx0 + w;
          const int idx3 = idx2 + 1;
          if (idx3 >= verts.size()) {
            continue;
          }
          append_tri(tris,
                     QVector3D(verts[idx0].pos.x, verts[idx0].pos.y, verts[idx0].pos.z),
                     QVector3D(verts[idx1].pos.x, verts[idx1].pos.y, verts[idx1].pos.z),
                     QVector3D(verts[idx2].pos.x, verts[idx2].pos.y, verts[idx2].pos.z),
                     QVector2D(verts[idx0].st[0], verts[idx0].st[1]),
                     QVector2D(verts[idx1].st[0], verts[idx1].st[1]),
                     QVector2D(verts[idx2].st[0], verts[idx2].st[1]),
                     color, shader_name, true);
          append_tri(tris,
                     QVector3D(verts[idx1].pos.x, verts[idx1].pos.y, verts[idx1].pos.z),
                     QVector3D(verts[idx3].pos.x, verts[idx3].pos.y, verts[idx3].pos.z),
                     QVector3D(verts[idx2].pos.x, verts[idx2].pos.y, verts[idx2].pos.z),
                     QVector2D(verts[idx1].st[0], verts[idx1].st[1]),
                     QVector2D(verts[idx3].st[0], verts[idx3].st[1]),
                     QVector2D(verts[idx2].st[0], verts[idx2].st[1]),
                     color, shader_name, true);
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

  if (header.magic != "IBSP") {
    out.error = "Unsupported BSP magic.";
    return out;
  }

  QVector<Tri> tris;
  const bool lightmapped = (style == BspPreviewStyle::Lightmapped);
  if (header.version == kQ1Version) {
    tris = build_q1_mesh(bytes, header, lightmapped, &out.error);
  } else if (header.version == kQ2Version) {
    tris = build_q2_mesh(bytes, header, lightmapped, &out.error);
  } else if (header.version == kQ3Version || header.version == kQLVersion) {
    tris = build_q3_mesh(bytes, header, lightmapped, &out.error);
  } else {
    out.error = QString("Unsupported BSP version: %1").arg(header.version);
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

  if (header.magic != "IBSP") {
    if (error) {
      *error = "Unsupported BSP magic.";
    }
    return false;
  }

  QVector<Tri> tris;
  if (header.version == kQ1Version) {
    tris = build_q1_mesh(bytes, header, use_lightmap, error);
  } else if (header.version == kQ2Version) {
    tris = build_q2_mesh(bytes, header, use_lightmap, error);
  } else if (header.version == kQ3Version || header.version == kQLVersion) {
    tris = build_q3_mesh(bytes, header, use_lightmap, error);
  } else {
    if (error) {
      *error = QString("Unsupported BSP version: %1").arg(header.version);
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

  if (header.magic != "IBSP" || header.version != kQ1Version) {
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

  const QByteArray tex_lump = bytes.mid(tex_lump_info.offset, tex_lump_info.length);
  for (int i = 0; i < miptex_offsets.size(); ++i) {
    const int base = miptex_offsets[i];
    if (base < 0 || base + 40 > tex_lump.size()) {
      continue;
    }

    const QString name = texture_name_from_q1_miptex(tex_lump, miptex_offsets, i).toLower();
    if (name.isEmpty()) {
      continue;
    }

    bool ok = false;
    const quint32 width_u = read_u32_le(tex_lump, base + 16, &ok);
    if (!ok) {
      continue;
    }
    const quint32 height_u = read_u32_le(tex_lump, base + 20, &ok);
    if (!ok || width_u == 0 || height_u == 0) {
      continue;
    }
    const quint32 ofs0 = read_u32_le(tex_lump, base + 24, &ok);
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
    if (slice_len <= 0 || base + slice_len > tex_lump.size()) {
      continue;
    }

    const QByteArray mip_bytes = tex_lump.mid(base, static_cast<int>(slice_len));
    QString mip_err;
    const QImage img = decode_miptex_image(mip_bytes, quake_palette, &mip_err);
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
