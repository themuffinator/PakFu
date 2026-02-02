#include "formats/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace {
struct Cursor {
  const QByteArray* bytes = nullptr;
  int pos = 0;

  int size() const { return bytes ? bytes->size() : 0; }

  bool seek(int p) {
    if (!bytes) {
      return false;
    }
    if (p < 0 || p > bytes->size()) {
      return false;
    }
    pos = p;
    return true;
  }

  bool skip(int n) { return seek(pos + n); }

  bool can_read(int n) const {
    if (!bytes) {
      return false;
    }
    if (n < 0) {
      return false;
    }
    return pos >= 0 && pos + n <= bytes->size();
  }

  bool read_bytes(int n, QByteArray* out) {
    if (!can_read(n)) {
      return false;
    }
    if (out) {
      *out = bytes->mid(pos, n);
    }
    pos += n;
    return true;
  }

  bool read_u8(quint8* out) {
    if (!can_read(1) || !out) {
      return false;
    }
    *out = static_cast<quint8>((*bytes)[pos]);
    ++pos;
    return true;
  }

  bool read_i16(qint16* out) {
    if (!can_read(2) || !out) {
      return false;
    }
    const quint8 b0 = static_cast<quint8>((*bytes)[pos + 0]);
    const quint8 b1 = static_cast<quint8>((*bytes)[pos + 1]);
    const quint16 u = static_cast<quint16>(b0 | (b1 << 8));
    *out = static_cast<qint16>(u);
    pos += 2;
    return true;
  }

  bool read_i32(qint32* out) {
    if (!can_read(4) || !out) {
      return false;
    }
    const quint8 b0 = static_cast<quint8>((*bytes)[pos + 0]);
    const quint8 b1 = static_cast<quint8>((*bytes)[pos + 1]);
    const quint8 b2 = static_cast<quint8>((*bytes)[pos + 2]);
    const quint8 b3 = static_cast<quint8>((*bytes)[pos + 3]);
    const quint32 u = static_cast<quint32>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
    *out = static_cast<qint32>(u);
    pos += 4;
    return true;
  }

  bool read_u32(quint32* out) {
    qint32 v = 0;
    if (!read_i32(&v) || !out) {
      return false;
    }
    *out = static_cast<quint32>(v);
    return true;
  }

  bool read_f32(float* out) {
    if (!can_read(4) || !out) {
      return false;
    }
    quint32 u = 0;
    if (!read_u32(&u)) {
      return false;
    }
    static_assert(sizeof(float) == sizeof(quint32));
    float f = 0.0f;
    memcpy(&f, &u, sizeof(float));
    *out = f;
    return true;
  }

  bool read_fixed_string(int n, QString* out) {
    QByteArray buf;
    if (!read_bytes(n, &buf)) {
      return false;
    }
    int end = buf.indexOf('\0');
    if (end < 0) {
      end = buf.size();
    }
    if (out) {
      *out = QString::fromLatin1(buf.constData(), end);
    }
    return true;
  }
};

QString file_ext_lower(const QString& name) {
  const QString lower = name.toLower();
  const int dot = lower.lastIndexOf('.');
  return dot >= 0 ? lower.mid(dot + 1) : QString();
}

void compute_bounds(ModelMesh* mesh) {
  if (!mesh || mesh->vertices.isEmpty()) {
    if (mesh) {
      mesh->mins = QVector3D();
      mesh->maxs = QVector3D();
    }
    return;
  }

  QVector3D mins(mesh->vertices[0].px, mesh->vertices[0].py, mesh->vertices[0].pz);
  QVector3D maxs = mins;
  for (const ModelVertex& v : mesh->vertices) {
    mins.setX(std::min(mins.x(), v.px));
    mins.setY(std::min(mins.y(), v.py));
    mins.setZ(std::min(mins.z(), v.pz));
    maxs.setX(std::max(maxs.x(), v.px));
    maxs.setY(std::max(maxs.y(), v.py));
    maxs.setZ(std::max(maxs.z(), v.pz));
  }
  mesh->mins = mins;
  mesh->maxs = maxs;
}

void compute_smooth_normals(ModelMesh* mesh) {
  if (!mesh) {
    return;
  }
  const int vcount = mesh->vertices.size();
  if (vcount <= 0) {
    return;
  }
  if ((mesh->indices.size() % 3) != 0) {
    return;
  }

  QVector<QVector3D> acc;
  acc.resize(vcount);
  for (int i = 0; i < vcount; ++i) {
    acc[i] = QVector3D(0, 0, 0);
  }

  const int tricount = mesh->indices.size() / 3;
  for (int t = 0; t < tricount; ++t) {
    const std::uint32_t i0 = mesh->indices[t * 3 + 0];
    const std::uint32_t i1 = mesh->indices[t * 3 + 1];
    const std::uint32_t i2 = mesh->indices[t * 3 + 2];
    if (i0 >= static_cast<std::uint32_t>(vcount) || i1 >= static_cast<std::uint32_t>(vcount) ||
        i2 >= static_cast<std::uint32_t>(vcount)) {
      continue;
    }
    const ModelVertex& v0 = mesh->vertices[static_cast<int>(i0)];
    const ModelVertex& v1 = mesh->vertices[static_cast<int>(i1)];
    const ModelVertex& v2 = mesh->vertices[static_cast<int>(i2)];
    const QVector3D p0(v0.px, v0.py, v0.pz);
    const QVector3D p1(v1.px, v1.py, v1.pz);
    const QVector3D p2(v2.px, v2.py, v2.pz);
    const QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0);
    if (n.lengthSquared() < 1e-12f) {
      continue;
    }
    acc[static_cast<int>(i0)] += n;
    acc[static_cast<int>(i1)] += n;
    acc[static_cast<int>(i2)] += n;
  }

  for (int i = 0; i < vcount; ++i) {
    QVector3D n = acc[i];
    if (n.lengthSquared() < 1e-12f) {
      n = QVector3D(0, 0, 1);
    } else {
      n.normalize();
    }
    mesh->vertices[i].nx = n.x();
    mesh->vertices[i].ny = n.y();
    mesh->vertices[i].nz = n.z();
  }
}

std::optional<LoadedModel> load_mdl(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MDL.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kIdent = 0x4F504449u;  // "IDPO"
  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }
  if (ident != kIdent || version != 6) {
    if (error) {
      *error = "Not a supported Quake MDL (expected IDPO v6).";
    }
    return std::nullopt;
  }

  float scale[3]{};
  float translate[3]{};
  float bounding_radius = 0.0f;
  float eye[3]{};
  qint32 numskins = 0;
  qint32 skinwidth = 0;
  qint32 skinheight = 0;
  qint32 numverts = 0;
  qint32 numtris = 0;
  qint32 numframes = 0;
  qint32 synctype = 0;
  qint32 flags = 0;
  float size = 0.0f;

  for (int i = 0; i < 3; ++i) {
    if (!cur.read_f32(&scale[i])) {
      if (error) {
        *error = "MDL header is incomplete.";
      }
      return std::nullopt;
    }
  }
  for (int i = 0; i < 3; ++i) {
    if (!cur.read_f32(&translate[i])) {
      if (error) {
        *error = "MDL header is incomplete.";
      }
      return std::nullopt;
    }
  }
  if (!cur.read_f32(&bounding_radius)) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }
  (void)bounding_radius;
  for (int i = 0; i < 3; ++i) {
    if (!cur.read_f32(&eye[i])) {
      if (error) {
        *error = "MDL header is incomplete.";
      }
      return std::nullopt;
    }
  }
  (void)eye;

  if (!cur.read_i32(&numskins) || !cur.read_i32(&skinwidth) || !cur.read_i32(&skinheight) || !cur.read_i32(&numverts) ||
      !cur.read_i32(&numtris) || !cur.read_i32(&numframes) || !cur.read_i32(&synctype) || !cur.read_i32(&flags) ||
      !cur.read_f32(&size)) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }
  (void)synctype;
  (void)flags;
  (void)size;

  if (skinwidth <= 0 || skinheight <= 0 || skinwidth > 4096 || skinheight > 4096) {
    if (error) {
      *error = "MDL has invalid skin dimensions.";
    }
    return std::nullopt;
  }
  if (numverts <= 0 || numverts > 65535 || numtris < 0 || numtris > 200000 || numframes <= 0 || numframes > 10000 ||
      numskins < 0 || numskins > 1000) {
    if (error) {
      *error = "MDL header values are invalid.";
    }
    return std::nullopt;
  }

  const int skin_bytes = skinwidth * skinheight;
  for (int s = 0; s < numskins; ++s) {
    qint32 type = 0;
    if (!cur.read_i32(&type)) {
      if (error) {
        *error = "MDL skins are incomplete.";
      }
      return std::nullopt;
    }
    if (type == 0) {
      if (!cur.skip(skin_bytes)) {
        if (error) {
          *error = "MDL skins are incomplete.";
        }
        return std::nullopt;
      }
    } else if (type == 1) {
      qint32 group = 0;
      if (!cur.read_i32(&group) || group <= 0 || group > 10000) {
        if (error) {
          *error = "MDL skin group is invalid.";
        }
        return std::nullopt;
      }
      if (!cur.skip(group * 4)) {  // intervals (float)
        if (error) {
          *error = "MDL skin group is incomplete.";
        }
        return std::nullopt;
      }
      if (!cur.skip(group * skin_bytes)) {
        if (error) {
          *error = "MDL skin group is incomplete.";
        }
        return std::nullopt;
      }
    } else {
      if (error) {
        *error = QString("MDL has unknown skin type: %1").arg(type);
      }
      return std::nullopt;
    }
  }

  // Skip ST verts (onseam,s,t) int32 * 3 each.
  if (!cur.skip(numverts * 12)) {
    if (error) {
      *error = "MDL texture coordinates are incomplete.";
    }
    return std::nullopt;
  }

  QVector<std::uint32_t> indices;
  indices.reserve(static_cast<int>(numtris) * 3);

  for (int t = 0; t < numtris; ++t) {
    qint32 facesfront = 0;
    qint32 v0 = 0, v1 = 0, v2 = 0;
    if (!cur.read_i32(&facesfront) || !cur.read_i32(&v0) || !cur.read_i32(&v1) || !cur.read_i32(&v2)) {
      if (error) {
        *error = "MDL triangles are incomplete.";
      }
      return std::nullopt;
    }
    (void)facesfront;
    if (v0 < 0 || v1 < 0 || v2 < 0 || v0 >= numverts || v1 >= numverts || v2 >= numverts) {
      continue;
    }
    indices.push_back(static_cast<std::uint32_t>(v0));
    indices.push_back(static_cast<std::uint32_t>(v1));
    indices.push_back(static_cast<std::uint32_t>(v2));
  }

  // Read the first frame's vertex positions.
  qint32 frame_type = 0;
  if (!cur.read_i32(&frame_type)) {
    if (error) {
      *error = "MDL frames are incomplete.";
    }
    return std::nullopt;
  }

  auto read_trivert = [&](quint8 v[3]) -> bool {
    quint8 x = 0, y = 0, z = 0, ni = 0;
    if (!cur.read_u8(&x) || !cur.read_u8(&y) || !cur.read_u8(&z) || !cur.read_u8(&ni)) {
      return false;
    }
    v[0] = x;
    v[1] = y;
    v[2] = z;
    (void)ni;
    return true;
  };

  auto read_frame_vertices = [&](QVector<ModelVertex>* out_verts) -> bool {
    quint8 bbox[3]{};
    if (!read_trivert(bbox) || !read_trivert(bbox)) {
      return false;
    }
    (void)bbox;
    if (!cur.skip(16)) {
      return false;
    }
    out_verts->resize(numverts);
    for (int i = 0; i < numverts; ++i) {
      quint8 v[3]{};
      if (!read_trivert(v)) {
        return false;
      }
      const float px = static_cast<float>(v[0]) * scale[0] + translate[0];
      const float py = static_cast<float>(v[1]) * scale[1] + translate[1];
      const float pz = static_cast<float>(v[2]) * scale[2] + translate[2];
      (*out_verts)[i].px = px;
      (*out_verts)[i].py = py;
      (*out_verts)[i].pz = pz;
    }
    return true;
  };

  QVector<ModelVertex> verts;
  if (frame_type == 0) {
    if (!read_frame_vertices(&verts)) {
      if (error) {
        *error = "MDL frame is incomplete.";
      }
      return std::nullopt;
    }
  } else if (frame_type == 1) {
    qint32 group = 0;
    quint8 bboxmin[3]{};
    quint8 bboxmax[3]{};
    if (!cur.read_i32(&group) || group <= 0) {
      if (error) {
        *error = "MDL frame group is invalid.";
      }
      return std::nullopt;
    }
    if (!read_trivert(bboxmin) || !read_trivert(bboxmax)) {
      if (error) {
        *error = "MDL frame group is incomplete.";
      }
      return std::nullopt;
    }
    (void)bboxmin;
    (void)bboxmax;
    if (!cur.skip(group * 4)) {
      if (error) {
        *error = "MDL frame group is incomplete.";
      }
      return std::nullopt;
    }
    // Read only the first subframe.
    if (!read_frame_vertices(&verts)) {
      if (error) {
        *error = "MDL frame group is incomplete.";
      }
      return std::nullopt;
    }
  } else {
    if (error) {
      *error = QString("MDL has unknown frame type: %1").arg(frame_type);
    }
    return std::nullopt;
  }

  LoadedModel out;
  out.format = "mdl";
  out.frame_count = numframes;
  out.surface_count = 1;
  out.mesh.vertices = std::move(verts);
  out.mesh.indices = std::move(indices);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_md2(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MD2.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kIdent = 0x32504449u;  // "IDP2"
  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "MD2 header is incomplete.";
    }
    return std::nullopt;
  }
  if (ident != kIdent || version != 8) {
    if (error) {
      *error = "Not a supported Quake II MD2 (expected IDP2 v8).";
    }
    return std::nullopt;
  }

  qint32 skinwidth = 0;
  qint32 skinheight = 0;
  qint32 framesize = 0;
  qint32 num_skins = 0;
  qint32 num_xyz = 0;
  qint32 num_st = 0;
  qint32 num_tris = 0;
  qint32 num_glcmds = 0;
  qint32 num_frames = 0;
  qint32 ofs_skins = 0;
  qint32 ofs_st = 0;
  qint32 ofs_tris = 0;
  qint32 ofs_frames = 0;
  qint32 ofs_glcmds = 0;
  qint32 ofs_end = 0;

  if (!cur.read_i32(&skinwidth) || !cur.read_i32(&skinheight) || !cur.read_i32(&framesize) || !cur.read_i32(&num_skins) ||
      !cur.read_i32(&num_xyz) || !cur.read_i32(&num_st) || !cur.read_i32(&num_tris) || !cur.read_i32(&num_glcmds) ||
      !cur.read_i32(&num_frames) || !cur.read_i32(&ofs_skins) || !cur.read_i32(&ofs_st) || !cur.read_i32(&ofs_tris) ||
      !cur.read_i32(&ofs_frames) || !cur.read_i32(&ofs_glcmds) || !cur.read_i32(&ofs_end)) {
    if (error) {
      *error = "MD2 header is incomplete.";
    }
    return std::nullopt;
  }
  (void)num_skins;
  (void)num_glcmds;
  (void)ofs_skins;
  (void)ofs_glcmds;

  const int file_size = bytes.size();
  if (ofs_end <= 0 || ofs_end > file_size || ofs_tris < 0 || ofs_st < 0 || ofs_frames < 0 || ofs_tris >= file_size || ofs_st >= file_size ||
      ofs_frames >= file_size) {
    if (error) {
      *error = "MD2 header offsets are invalid.";
    }
    return std::nullopt;
  }
  if (skinwidth <= 0 || skinheight <= 0 || skinwidth > 8192 || skinheight > 8192 || num_st <= 0 || num_st > 200000 || num_xyz <= 0 ||
      num_xyz > 100000 || num_tris < 0 || num_tris > 200000 || num_frames <= 0 || num_frames > 10000 || framesize <= 0 ||
      framesize > 16 * 1024 * 1024) {
    if (error) {
      *error = "MD2 header values are invalid.";
    }
    return std::nullopt;
  }

  struct Md2St {
    qint16 s = 0;
    qint16 t = 0;
  };
  QVector<Md2St> st;
  st.resize(num_st);
  if (!cur.seek(ofs_st)) {
    if (error) {
      *error = "MD2 texture coordinate offset is invalid.";
    }
    return std::nullopt;
  }
  for (int i = 0; i < num_st; ++i) {
    if (!cur.read_i16(&st[i].s) || !cur.read_i16(&st[i].t)) {
      if (error) {
        *error = "MD2 texture coordinates are incomplete.";
      }
      return std::nullopt;
    }
  }

  struct Md2Tri {
    qint16 vi[3]{};
    qint16 ti[3]{};
  };
  QVector<Md2Tri> tris;
  tris.reserve(num_tris);
  if (!cur.seek(ofs_tris)) {
    if (error) {
      *error = "MD2 triangles offset is invalid.";
    }
    return std::nullopt;
  }
  for (int t = 0; t < num_tris; ++t) {
    Md2Tri tri;
    for (int i = 0; i < 3; ++i) {
      if (!cur.read_i16(&tri.vi[i])) {
        if (error) {
          *error = "MD2 triangles are incomplete.";
        }
        return std::nullopt;
      }
    }
    for (int i = 0; i < 3; ++i) {
      if (!cur.read_i16(&tri.ti[i])) {
        if (error) {
          *error = "MD2 triangles are incomplete.";
        }
        return std::nullopt;
      }
    }
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
      if (tri.vi[i] < 0 || tri.vi[i] >= num_xyz || tri.ti[i] < 0 || tri.ti[i] >= num_st) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      continue;
    }
    tris.push_back(tri);
  }

  if (!cur.seek(ofs_frames)) {
    if (error) {
      *error = "MD2 frames offset is invalid.";
    }
    return std::nullopt;
  }

  float scale[3]{};
  float translate[3]{};
  for (int i = 0; i < 3; ++i) {
    if (!cur.read_f32(&scale[i])) {
      if (error) {
        *error = "MD2 frame is incomplete.";
      }
      return std::nullopt;
    }
  }
  for (int i = 0; i < 3; ++i) {
    if (!cur.read_f32(&translate[i])) {
      if (error) {
        *error = "MD2 frame is incomplete.";
      }
      return std::nullopt;
    }
  }
  if (!cur.skip(16)) {
    if (error) {
      *error = "MD2 frame is incomplete.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> base_verts;
  base_verts.resize(num_xyz);
  for (int i = 0; i < num_xyz; ++i) {
    quint8 vx = 0, vy = 0, vz = 0, ni = 0;
    if (!cur.read_u8(&vx) || !cur.read_u8(&vy) || !cur.read_u8(&vz) || !cur.read_u8(&ni)) {
      if (error) {
        *error = "MD2 vertices are incomplete.";
      }
      return std::nullopt;
    }
    (void)ni;
    base_verts[i].px = static_cast<float>(vx) * scale[0] + translate[0];
    base_verts[i].py = static_cast<float>(vy) * scale[1] + translate[1];
    base_verts[i].pz = static_cast<float>(vz) * scale[2] + translate[2];
  }

  if (tris.isEmpty()) {
    if (error) {
      *error = "MD2 contains no drawable geometry.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> verts;
  QVector<std::uint32_t> indices;
  verts.reserve(tris.size() * 3);
  indices.reserve(tris.size() * 3);

  QHash<quint64, std::uint32_t> remap;
  remap.reserve(tris.size() * 3);

  const float inv_skinw = 1.0f / static_cast<float>(skinwidth);
  const float inv_skinh = 1.0f / static_cast<float>(skinheight);

  auto make_key = [](quint32 vi, quint32 ti) -> quint64 {
    return (static_cast<quint64>(vi) << 32) | static_cast<quint64>(ti);
  };

  for (const Md2Tri& tri : tris) {
    for (int i = 0; i < 3; ++i) {
      const quint32 vi = static_cast<quint32>(tri.vi[i]);
      const quint32 ti = static_cast<quint32>(tri.ti[i]);
      const quint64 key = make_key(vi, ti);

      auto it = remap.constFind(key);
      if (it == remap.constEnd()) {
        const std::uint32_t new_index = static_cast<std::uint32_t>(verts.size());
        remap.insert(key, new_index);

        ModelVertex v = base_verts[static_cast<int>(vi)];
        v.u = static_cast<float>(st[static_cast<int>(ti)].s) * inv_skinw;
        v.v = 1.0f - (static_cast<float>(st[static_cast<int>(ti)].t) * inv_skinh);
        verts.push_back(v);
        indices.push_back(new_index);
      } else {
        indices.push_back(*it);
      }
    }
  }

  LoadedModel out;
  out.format = "md2";
  out.frame_count = num_frames;
  out.surface_count = 1;
  out.mesh.vertices = std::move(verts);
  out.mesh.indices = std::move(indices);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_md3(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MD3.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kIdent = 0x33504449u;  // "IDP3"
  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "MD3 header is incomplete.";
    }
    return std::nullopt;
  }
  if (ident != kIdent || version != 15) {
    if (error) {
      *error = "Not a supported Quake III MD3 (expected IDP3 v15).";
    }
    return std::nullopt;
  }

  if (!cur.skip(64)) {  // name
    if (error) {
      *error = "MD3 header is incomplete.";
    }
    return std::nullopt;
  }

  qint32 flags = 0;
  qint32 num_frames = 0;
  qint32 num_tags = 0;
  qint32 num_surfaces = 0;
  qint32 num_skins = 0;
  qint32 ofs_frames = 0;
  qint32 ofs_tags = 0;
  qint32 ofs_surfaces = 0;
  qint32 ofs_end = 0;

  if (!cur.read_i32(&flags) || !cur.read_i32(&num_frames) || !cur.read_i32(&num_tags) || !cur.read_i32(&num_surfaces) ||
      !cur.read_i32(&num_skins) || !cur.read_i32(&ofs_frames) || !cur.read_i32(&ofs_tags) || !cur.read_i32(&ofs_surfaces) ||
      !cur.read_i32(&ofs_end)) {
    if (error) {
      *error = "MD3 header is incomplete.";
    }
    return std::nullopt;
  }
  (void)flags;
  (void)num_tags;
  (void)num_skins;
  (void)ofs_frames;
  (void)ofs_tags;

  const int file_size = bytes.size();
  if (ofs_end <= 0 || ofs_end > file_size || ofs_surfaces < 0 || ofs_surfaces >= file_size) {
    if (error) {
      *error = "MD3 header offsets are invalid.";
    }
    return std::nullopt;
  }
  if (num_frames <= 0 || num_frames > 10000 || num_surfaces < 0 || num_surfaces > 10000) {
    if (error) {
      *error = "MD3 header values are invalid.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  int surface_count = 0;

  int surf_off = ofs_surfaces;
  for (int s = 0; s < num_surfaces; ++s) {
    if (surf_off < 0 || surf_off + 4 > file_size) {
      break;
    }
    if (!cur.seek(surf_off)) {
      break;
    }

    quint32 surf_ident = 0;
    if (!cur.read_u32(&surf_ident) || surf_ident != kIdent) {
      if (error) {
        *error = "MD3 surface header is invalid.";
      }
      return std::nullopt;
    }

    if (!cur.skip(64)) {  // name
      if (error) {
        *error = "MD3 surface header is incomplete.";
      }
      return std::nullopt;
    }

    qint32 surf_flags = 0;
    qint32 surf_num_frames = 0;
    qint32 surf_num_shaders = 0;
    qint32 surf_num_verts = 0;
    qint32 surf_num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 ofs_shaders = 0;
    qint32 ofs_st = 0;
    qint32 ofs_xyz = 0;
    qint32 ofs_surf_end = 0;

    if (!cur.read_i32(&surf_flags) || !cur.read_i32(&surf_num_frames) || !cur.read_i32(&surf_num_shaders) ||
        !cur.read_i32(&surf_num_verts) || !cur.read_i32(&surf_num_tris) || !cur.read_i32(&ofs_tris) ||
        !cur.read_i32(&ofs_shaders) || !cur.read_i32(&ofs_st) || !cur.read_i32(&ofs_xyz) || !cur.read_i32(&ofs_surf_end)) {
      if (error) {
        *error = "MD3 surface header is incomplete.";
      }
      return std::nullopt;
    }
    (void)surf_flags;
    (void)surf_num_shaders;
    (void)ofs_shaders;

    if (surf_num_verts <= 0 || surf_num_verts > 200000 || surf_num_tris < 0 || surf_num_tris > 200000 || ofs_surf_end <= 0) {
      if (error) {
        *error = "MD3 surface values are invalid.";
      }
      return std::nullopt;
    }

    const int base_vertex = vertices.size();
    vertices.resize(base_vertex + surf_num_verts);

    // Frame 0 vertex array (xyz/normal).
    const int xyz_off = surf_off + ofs_xyz;
    if (!cur.seek(xyz_off)) {
      if (error) {
        *error = "MD3 surface vertex offset is invalid.";
      }
      return std::nullopt;
    }
    for (int v = 0; v < surf_num_verts; ++v) {
      qint16 x = 0, y = 0, z = 0;
      qint16 n = 0;
      if (!cur.read_i16(&x) || !cur.read_i16(&y) || !cur.read_i16(&z) || !cur.read_i16(&n)) {
        if (error) {
          *error = "MD3 vertices are incomplete.";
        }
        return std::nullopt;
      }
      (void)n;
      ModelVertex mv;
      mv.px = static_cast<float>(x) / 64.0f;
      mv.py = static_cast<float>(y) / 64.0f;
      mv.pz = static_cast<float>(z) / 64.0f;
      vertices[base_vertex + v] = mv;
    }

    // Texture coordinates.
    const int st_off = surf_off + ofs_st;
    if (!cur.seek(st_off)) {
      if (error) {
        *error = "MD3 surface texture coordinate offset is invalid.";
      }
      return std::nullopt;
    }
    for (int v = 0; v < surf_num_verts; ++v) {
      float s0 = 0.0f;
      float t0 = 0.0f;
      if (!cur.read_f32(&s0) || !cur.read_f32(&t0)) {
        if (error) {
          *error = "MD3 texture coordinates are incomplete.";
        }
        return std::nullopt;
      }
      vertices[base_vertex + v].u = s0;
      vertices[base_vertex + v].v = 1.0f - t0;
    }

    // Triangles.
    const int tri_off = surf_off + ofs_tris;
    if (!cur.seek(tri_off)) {
      if (error) {
        *error = "MD3 surface triangle offset is invalid.";
      }
      return std::nullopt;
    }
    indices.reserve(indices.size() + surf_num_tris * 3);
    for (int t = 0; t < surf_num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      if (!cur.read_i32(&i0) || !cur.read_i32(&i1) || !cur.read_i32(&i2)) {
        if (error) {
          *error = "MD3 triangles are incomplete.";
        }
        return std::nullopt;
      }
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= surf_num_verts || i1 >= surf_num_verts || i2 >= surf_num_verts) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i0));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i1));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i2));
    }

    ++surface_count;
    surf_off += ofs_surf_end;
    if (surf_off <= 0 || surf_off > ofs_end) {
      break;
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "MD3 contains no drawable geometry.";
    }
    return std::nullopt;
  }

  LoadedModel out;
  out.format = "md3";
  out.frame_count = num_frames;
  out.surface_count = std::max(1, surface_count);
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}
}  // namespace

std::optional<LoadedModel> load_model_file(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  const QFileInfo info(file_path);
  const QString ext = file_ext_lower(info.fileName());

  if (ext == "mdl") {
    return load_mdl(info.absoluteFilePath(), error);
  }
  if (ext == "md2") {
    return load_md2(info.absoluteFilePath(), error);
  }
  if (ext == "md3") {
    return load_md3(info.absoluteFilePath(), error);
  }

  if (error) {
    *error = "Unsupported model format.";
  }
  return std::nullopt;
}
