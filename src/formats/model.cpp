#include "formats/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QQuaternion>

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
  out.surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(out.mesh.indices.size())}};
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
  out.surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(out.mesh.indices.size())}};
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
  QVector<ModelSurface> surfaces;
  surfaces.reserve(num_surfaces);

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

    QString surf_name;
    if (!cur.read_fixed_string(64, &surf_name)) {  // name
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

    QString shader_name;
    if (surf_num_shaders > 0 && ofs_shaders > 0) {
      const int sh_off = surf_off + ofs_shaders;
      if (cur.seek(sh_off)) {
        (void)cur.read_fixed_string(64, &shader_name);
        qint32 shader_index = 0;
        (void)cur.read_i32(&shader_index);
      }
    }

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
    const int first_index = indices.size();
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

    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface surf;
      surf.name = surf_name;
      surf.shader = shader_name;
      surf.first_index = first_index;
      surf.index_count = index_count;
      surfaces.push_back(std::move(surf));
    }
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

  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "md3";
  out.frame_count = num_frames;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_iqm(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open IQM.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 16 + 4 * 27) {
    if (error) {
      *error = "IQM header is incomplete.";
    }
    return std::nullopt;
  }

  Cursor cur;
  cur.bytes = &bytes;

  QByteArray magic;
  if (!cur.read_bytes(16, &magic) || magic.size() != 16) {
    if (error) {
      *error = "IQM header is incomplete.";
    }
    return std::nullopt;
  }
  const QByteArray expected_magic("INTERQUAKEMODEL\0", 16);
  if (magic != expected_magic) {
    if (error) {
      *error = "Not a supported IQM (missing INTERQUAKEMODEL magic).";
    }
    return std::nullopt;
  }

  quint32 version = 0;
  quint32 filesize = 0;
  quint32 flags = 0;
  quint32 num_text = 0, ofs_text = 0;
  quint32 num_meshes = 0, ofs_meshes = 0;
  quint32 num_vertexarrays = 0, num_vertexes = 0, ofs_vertexarrays = 0;
  quint32 num_triangles = 0, ofs_triangles = 0, ofs_adjacency = 0;
  quint32 num_joints = 0, ofs_joints = 0;
  quint32 num_poses = 0, ofs_poses = 0;
  quint32 num_anims = 0, ofs_anims = 0;
  quint32 num_frames = 0, num_framechannels = 0, ofs_frames = 0, ofs_bounds = 0;
  quint32 num_comment = 0, ofs_comment = 0;
  quint32 num_extensions = 0, ofs_extensions = 0;

  if (!cur.read_u32(&version) || !cur.read_u32(&filesize) || !cur.read_u32(&flags) ||
      !cur.read_u32(&num_text) || !cur.read_u32(&ofs_text) ||
      !cur.read_u32(&num_meshes) || !cur.read_u32(&ofs_meshes) ||
      !cur.read_u32(&num_vertexarrays) || !cur.read_u32(&num_vertexes) || !cur.read_u32(&ofs_vertexarrays) ||
      !cur.read_u32(&num_triangles) || !cur.read_u32(&ofs_triangles) || !cur.read_u32(&ofs_adjacency) ||
      !cur.read_u32(&num_joints) || !cur.read_u32(&ofs_joints) ||
      !cur.read_u32(&num_poses) || !cur.read_u32(&ofs_poses) ||
      !cur.read_u32(&num_anims) || !cur.read_u32(&ofs_anims) ||
      !cur.read_u32(&num_frames) || !cur.read_u32(&num_framechannels) || !cur.read_u32(&ofs_frames) ||
      !cur.read_u32(&ofs_bounds) || !cur.read_u32(&num_comment) || !cur.read_u32(&ofs_comment) ||
      !cur.read_u32(&num_extensions) || !cur.read_u32(&ofs_extensions)) {
    if (error) {
      *error = "IQM header is incomplete.";
    }
    return std::nullopt;
  }

  Q_UNUSED(flags);
  Q_UNUSED(ofs_adjacency);
  Q_UNUSED(num_joints);
  Q_UNUSED(ofs_joints);
  Q_UNUSED(num_poses);
  Q_UNUSED(ofs_poses);
  Q_UNUSED(num_anims);
  Q_UNUSED(ofs_anims);
  Q_UNUSED(num_frames);
  Q_UNUSED(num_framechannels);
  Q_UNUSED(ofs_frames);
  Q_UNUSED(ofs_bounds);
  Q_UNUSED(num_comment);
  Q_UNUSED(ofs_comment);
  Q_UNUSED(num_extensions);
  Q_UNUSED(ofs_extensions);

  constexpr quint32 kIqmVersion = 2;
  if (version != kIqmVersion) {
    if (error) {
      *error = QString("Unsupported IQM version: %1.").arg(version);
    }
    return std::nullopt;
  }

  if (filesize > static_cast<quint32>(bytes.size())) {
    if (error) {
      *error = "IQM file size field is out of bounds.";
    }
    return std::nullopt;
  }

  auto range_ok = [&](quint32 ofs, quint32 count, quint32 elem_bytes) -> bool {
    const quint64 o = ofs;
    const quint64 n = count;
    const quint64 b = elem_bytes;
    const quint64 end = o + n * b;
    return end <= static_cast<quint64>(bytes.size());
  };

  if (!range_ok(ofs_text, num_text, 1) || !range_ok(ofs_meshes, num_meshes, 24) || !range_ok(ofs_vertexarrays, num_vertexarrays, 20) ||
      !range_ok(ofs_triangles, num_triangles, 12)) {
    if (error) {
      *error = "IQM sections are out of bounds.";
    }
    return std::nullopt;
  }

  const QByteArray text = bytes.mid(static_cast<int>(ofs_text), static_cast<int>(num_text));
  auto get_text = [&](quint32 ofs) -> QString {
    if (ofs >= static_cast<quint32>(text.size())) {
      return {};
    }
    const char* base = text.constData() + static_cast<int>(ofs);
    const int max = text.size() - static_cast<int>(ofs);
    const int nul = QByteArray(base, max).indexOf('\0');
    const int len = (nul >= 0) ? nul : max;
    return QString::fromLatin1(base, len);
  };

  struct IqmMesh {
    quint32 name = 0;
    quint32 material = 0;
    quint32 first_vertex = 0;
    quint32 num_vertexes = 0;
    quint32 first_triangle = 0;
    quint32 num_triangles = 0;
  };

  QVector<IqmMesh> meshes;
  meshes.reserve(static_cast<int>(num_meshes));
  for (quint32 i = 0; i < num_meshes; ++i) {
    Cursor mc = cur;
    if (!mc.seek(static_cast<int>(ofs_meshes + i * 24))) {
      break;
    }
    IqmMesh m;
    if (!mc.read_u32(&m.name) || !mc.read_u32(&m.material) || !mc.read_u32(&m.first_vertex) || !mc.read_u32(&m.num_vertexes) ||
        !mc.read_u32(&m.first_triangle) || !mc.read_u32(&m.num_triangles)) {
      break;
    }
    meshes.push_back(m);
  }

  struct IqmVa {
    quint32 type = 0;
    quint32 format = 0;
    quint32 size = 0;
    quint32 offset = 0;
    bool valid = false;
  };

  constexpr quint32 IQM_POSITION = 0;
  constexpr quint32 IQM_TEXCOORD = 1;
  constexpr quint32 IQM_NORMAL = 2;
  constexpr quint32 IQM_FLOAT = 7;

  IqmVa pos_va;
  IqmVa nrm_va;
  IqmVa st_va;

  for (quint32 i = 0; i < num_vertexarrays; ++i) {
    Cursor vc = cur;
    if (!vc.seek(static_cast<int>(ofs_vertexarrays + i * 20))) {
      break;
    }
    quint32 type = 0, flags0 = 0, format = 0, size0 = 0, offset = 0;
    if (!vc.read_u32(&type) || !vc.read_u32(&flags0) || !vc.read_u32(&format) || !vc.read_u32(&size0) || !vc.read_u32(&offset)) {
      break;
    }
    Q_UNUSED(flags0);
    if (format != IQM_FLOAT) {
      continue;
    }
    const quint32 elem_bytes = size0 * 4;
    if (!range_ok(offset, num_vertexes, elem_bytes)) {
      continue;
    }

    if (type == IQM_POSITION && size0 == 3) {
      pos_va = IqmVa{type, format, size0, offset, true};
    } else if (type == IQM_NORMAL && size0 == 3) {
      nrm_va = IqmVa{type, format, size0, offset, true};
    } else if (type == IQM_TEXCOORD && size0 == 2) {
      st_va = IqmVa{type, format, size0, offset, true};
    }
  }

  if (!pos_va.valid) {
    if (error) {
      *error = "IQM is missing required position data.";
    }
    return std::nullopt;
  }

  auto read_f32_le = [&](quint32 byte_ofs) -> float {
    if (byte_ofs + 4 > static_cast<quint32>(bytes.size())) {
      return 0.0f;
    }
    const uchar* p = reinterpret_cast<const uchar*>(bytes.constData() + static_cast<int>(byte_ofs));
    const quint32 u = static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    float out = 0.0f;
    memcpy(&out, &u, sizeof(float));
    return out;
  };

  QVector<ModelVertex> vertices;
  vertices.resize(static_cast<int>(num_vertexes));
  for (quint32 i = 0; i < num_vertexes; ++i) {
    const quint32 pofs = pos_va.offset + i * 12;
    vertices[static_cast<int>(i)].px = read_f32_le(pofs + 0);
    vertices[static_cast<int>(i)].py = read_f32_le(pofs + 4);
    vertices[static_cast<int>(i)].pz = read_f32_le(pofs + 8);

    if (nrm_va.valid) {
      const quint32 nofs = nrm_va.offset + i * 12;
      vertices[static_cast<int>(i)].nx = read_f32_le(nofs + 0);
      vertices[static_cast<int>(i)].ny = read_f32_le(nofs + 4);
      vertices[static_cast<int>(i)].nz = read_f32_le(nofs + 8);
    }
    if (st_va.valid) {
      const quint32 tofs = st_va.offset + i * 8;
      vertices[static_cast<int>(i)].u = read_f32_le(tofs + 0);
      const float tv = read_f32_le(tofs + 4);
      vertices[static_cast<int>(i)].v = 1.0f - tv;
    }
  }

  struct TriU32 {
    quint32 v0 = 0;
    quint32 v1 = 0;
    quint32 v2 = 0;
  };

  auto read_tri = [&](quint32 tri_index, TriU32* out) -> bool {
    if (!out) {
      return false;
    }
    const quint64 ofs = static_cast<quint64>(ofs_triangles) + static_cast<quint64>(tri_index) * 12ULL;
    if (ofs + 12ULL > static_cast<quint64>(bytes.size())) {
      return false;
    }
    const uchar* p = reinterpret_cast<const uchar*>(bytes.constData() + static_cast<int>(ofs));
    auto ru32 = [&](int off) -> quint32 {
      const uchar* b = p + off;
      return static_cast<quint32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    };
    out->v0 = ru32(0);
    out->v1 = ru32(4);
    out->v2 = ru32(8);
    return true;
  };

  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(meshes.isEmpty() ? 1 : meshes.size());

  if (meshes.isEmpty()) {
    // No mesh table: treat the whole triangle list as a single surface.
    const int first_index = indices.size();
    indices.reserve(static_cast<int>(num_triangles * 3));
    for (quint32 t = 0; t < num_triangles; ++t) {
      TriU32 tri;
      if (!read_tri(t, &tri)) {
        break;
      }
      if (tri.v0 >= num_vertexes || tri.v1 >= num_vertexes || tri.v2 >= num_vertexes) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(tri.v0));
      indices.push_back(static_cast<std::uint32_t>(tri.v1));
      indices.push_back(static_cast<std::uint32_t>(tri.v2));
    }
    const int index_count = indices.size() - first_index;
    surfaces = {ModelSurface{QString("model"), QString(), first_index, index_count}};
  } else {
    for (int mi = 0; mi < meshes.size(); ++mi) {
      const IqmMesh& m = meshes[mi];
      if (m.first_triangle >= num_triangles) {
        continue;
      }
      const quint32 tri_end = qMin(num_triangles, m.first_triangle + m.num_triangles);
      const int first_index = indices.size();
      indices.reserve(indices.size() + static_cast<int>((tri_end - m.first_triangle) * 3));
      for (quint32 t = m.first_triangle; t < tri_end; ++t) {
        TriU32 tri;
        if (!read_tri(t, &tri)) {
          break;
        }
        if (tri.v0 >= num_vertexes || tri.v1 >= num_vertexes || tri.v2 >= num_vertexes) {
          continue;
        }
        indices.push_back(static_cast<std::uint32_t>(tri.v0));
        indices.push_back(static_cast<std::uint32_t>(tri.v1));
        indices.push_back(static_cast<std::uint32_t>(tri.v2));
      }
      const int index_count = indices.size() - first_index;
      if (index_count <= 0) {
        continue;
      }

      ModelSurface surf;
      surf.name = get_text(m.name);
      surf.shader = get_text(m.material);
      if (surf.name.isEmpty()) {
        surf.name = QString("mesh%1").arg(mi);
      }
      surf.first_index = first_index;
      surf.index_count = index_count;
      surfaces.push_back(std::move(surf));
    }
    if (surfaces.isEmpty()) {
      surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "IQM contains no drawable geometry.";
    }
    return std::nullopt;
  }

  LoadedModel out;
  out.format = "iqm";
  out.frame_count = 1;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);

  if (!nrm_va.valid) {
    compute_smooth_normals(&out.mesh);
  }
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_md5mesh(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MD5 mesh.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.isEmpty()) {
    if (error) {
      *error = "Empty MD5 mesh.";
    }
    return std::nullopt;
  }

  struct Tok {
    const QByteArray* data = nullptr;
    int pos = 0;

    [[nodiscard]] bool at_end() const { return !data || pos >= data->size(); }

    void skip_ws_and_comments() {
      if (!data) {
        return;
      }
      while (pos < data->size()) {
        const char c = (*data)[pos];
        if (c == '/' && pos + 1 < data->size() && (*data)[pos + 1] == '/') {
          pos += 2;
          while (pos < data->size() && (*data)[pos] != '\n') {
            ++pos;
          }
          continue;
        }
        if (c == '/' && pos + 1 < data->size() && (*data)[pos + 1] == '*') {
          pos += 2;
          while (pos + 1 < data->size()) {
            if ((*data)[pos] == '*' && (*data)[pos + 1] == '/') {
              pos += 2;
              break;
            }
            ++pos;
          }
          continue;
        }
        if (static_cast<unsigned char>(c) <= 0x20) {
          ++pos;
          continue;
        }
        break;
      }
    }

    [[nodiscard]] bool next(QString* out, QString* err) {
      if (err) {
        err->clear();
      }
      if (out) {
        out->clear();
      }
      skip_ws_and_comments();
      if (at_end()) {
        return false;
      }

      const char c = (*data)[pos];
      // Single-character tokens.
      if (c == '{' || c == '}' || c == '(' || c == ')') {
        ++pos;
        if (out) {
          *out = QString(QChar::fromLatin1(c));
        }
        return true;
      }

      // Quoted string.
      if (c == '"') {
        ++pos;
        const int start = pos;
        while (pos < data->size() && (*data)[pos] != '"') {
          ++pos;
        }
        const int end = pos;
        if (pos < data->size() && (*data)[pos] == '"') {
          ++pos;
        }
        if (out) {
          *out = QString::fromLatin1(data->constData() + start, end - start);
        }
        return true;
      }

      const int start = pos;
      while (pos < data->size()) {
        const char cc = (*data)[pos];
        if (static_cast<unsigned char>(cc) <= 0x20) {
          break;
        }
        if (cc == '{' || cc == '}' || cc == '(' || cc == ')' || cc == '"') {
          break;
        }
        ++pos;
      }
      if (out) {
        *out = QString::fromLatin1(data->constData() + start, pos - start);
      }
      return true;
    }
  };

  auto parse_int = [](const QString& s, bool* ok) -> int {
    bool local_ok = false;
    const int v = s.toInt(&local_ok);
    if (ok) {
      *ok = local_ok;
    }
    return v;
  };
  auto parse_float = [](const QString& s, bool* ok) -> float {
    bool local_ok = false;
    const float v = s.toFloat(&local_ok);
    if (ok) {
      *ok = local_ok;
    }
    return v;
  };

  struct Joint {
    QString name;
    int parent = -1;
    QVector3D pos;
    QQuaternion orient;
    QVector3D world_pos;
    QQuaternion world_orient;
  };
  struct Vert {
    float u = 0.0f;
    float v = 0.0f;
    int start_weight = 0;
    int count_weight = 0;
  };
  struct Weight {
    int joint = 0;
    float bias = 0.0f;
    QVector3D pos;
  };
  struct Mesh {
    QString shader;
    QVector<Vert> verts;
    QVector<std::uint32_t> tris;
    QVector<Weight> weights;
  };

  Tok tok;
  tok.data = &bytes;

  QVector<Joint> joints;
  QVector<Mesh> meshes;

  auto expect = [&](const QString& want, QString* err) -> bool {
    QString got;
    if (!tok.next(&got, err)) {
      if (err) {
        *err = QString("MD5 parse error: expected '%1', got end-of-file.").arg(want);
      }
      return false;
    }
    if (got != want) {
      if (err) {
        *err = QString("MD5 parse error: expected '%1', got '%2'.").arg(want, got);
      }
      return false;
    }
    return true;
  };

  auto read_vec3_paren = [&](QVector3D* out, QString* err) -> bool {
    if (!out) {
      return false;
    }
    if (!expect("(", err)) {
      return false;
    }
    bool okx = false, oky = false, okz = false;
    QString sx, sy, sz;
    if (!tok.next(&sx, err) || !tok.next(&sy, err) || !tok.next(&sz, err)) {
      if (err) {
        *err = "MD5 parse error: unexpected end of vec3.";
      }
      return false;
    }
    const float x = parse_float(sx, &okx);
    const float y = parse_float(sy, &oky);
    const float z = parse_float(sz, &okz);
    if (!(okx && oky && okz)) {
      if (err) {
        *err = "MD5 parse error: invalid vec3.";
      }
      return false;
    }
    if (!expect(")", err)) {
      return false;
    }
    *out = QVector3D(x, y, z);
    return true;
  };

  auto quat_from_xyz = [&](float x, float y, float z) -> QQuaternion {
    const float t = 1.0f - (x * x + y * y + z * z);
    const float w = (t > 0.0f) ? -std::sqrt(t) : 0.0f;
    return QQuaternion(w, x, y, z);
  };

  QString err;
  while (true) {
    QString t;
    if (!tok.next(&t, &err)) {
      break;
    }
    const QString tl = t.toLower();

    if (tl == "joints") {
      if (!expect("{", &err)) {
        break;
      }
      while (true) {
        QString name;
        if (!tok.next(&name, &err)) {
          err = "MD5 parse error: unexpected end of joints.";
          break;
        }
        if (name == "}") {
          break;
        }
        if (name.isEmpty()) {
          err = "MD5 parse error: invalid joint name.";
          break;
        }

        bool ok_parent = false;
        QString parent_s;
        if (!tok.next(&parent_s, &err)) {
          err = "MD5 parse error: unexpected end of joints.";
          break;
        }
        const int parent = parse_int(parent_s, &ok_parent);
        if (!ok_parent) {
          err = "MD5 parse error: invalid joint parent.";
          break;
        }

        QVector3D pos;
        if (!read_vec3_paren(&pos, &err)) {
          break;
        }

        QVector3D oxyz;
        if (!read_vec3_paren(&oxyz, &err)) {
          break;
        }

        Joint j;
        j.name = name;
        j.parent = parent;
        j.pos = pos;
        j.orient = quat_from_xyz(oxyz.x(), oxyz.y(), oxyz.z());
        joints.push_back(std::move(j));
      }
      if (!err.isEmpty()) {
        break;
      }
      continue;
    }

    if (tl == "mesh") {
      if (!expect("{", &err)) {
        break;
      }
      Mesh m;
      while (true) {
        QString key;
        if (!tok.next(&key, &err)) {
          err = "MD5 parse error: unexpected end of mesh.";
          break;
        }
        if (key == "}") {
          break;
        }
        const QString kl = key.toLower();

        if (kl == "shader") {
          QString shader;
          if (!tok.next(&shader, &err)) {
            err = "MD5 parse error: unexpected end of shader.";
            break;
          }
          m.shader = shader;
          continue;
        }

        if (kl == "numverts") {
          bool ok = false;
          QString n_s;
          if (!tok.next(&n_s, &err)) {
            err = "MD5 parse error: invalid numverts.";
            break;
          }
          const int n = parse_int(n_s, &ok);
          if (!ok || n < 0 || n > 2'000'000) {
            err = "MD5 parse error: invalid numverts.";
            break;
          }
          m.verts.clear();
          m.verts.resize(n);
          continue;
        }

        if (kl == "vert") {
          bool ok_idx = false;
          QString idx_s;
          if (!tok.next(&idx_s, &err)) {
            err = "MD5 parse error: invalid vert index.";
            break;
          }
          const int idx = parse_int(idx_s, &ok_idx);
          if (!ok_idx || idx < 0 || idx >= m.verts.size()) {
            err = "MD5 parse error: invalid vert index.";
            break;
          }
          if (!expect("(", &err)) {
            break;
          }
          bool oku = false, okv = false;
          QString su, sv;
          if (!tok.next(&su, &err) || !tok.next(&sv, &err)) {
            err = "MD5 parse error: invalid vert uv.";
            break;
          }
          const float u = parse_float(su, &oku);
          const float v = parse_float(sv, &okv);
          if (!(oku && okv)) {
            err = "MD5 parse error: invalid vert uv.";
            break;
          }
          if (!expect(")", &err)) {
            break;
          }
          bool ok_sw = false, ok_cw = false;
          QString sw_s, cw_s;
          if (!tok.next(&sw_s, &err) || !tok.next(&cw_s, &err)) {
            err = "MD5 parse error: invalid vert weights.";
            break;
          }
          const int start_w = parse_int(sw_s, &ok_sw);
          const int count_w = parse_int(cw_s, &ok_cw);
          if (!(ok_sw && ok_cw) || start_w < 0 || count_w < 0) {
            err = "MD5 parse error: invalid vert weights.";
            break;
          }
          m.verts[idx].u = u;
          m.verts[idx].v = v;
          m.verts[idx].start_weight = start_w;
          m.verts[idx].count_weight = count_w;
          continue;
        }

        if (kl == "numtris") {
          bool ok = false;
          QString n_s;
          if (!tok.next(&n_s, &err)) {
            err = "MD5 parse error: invalid numtris.";
            break;
          }
          const int n = parse_int(n_s, &ok);
          if (!ok || n < 0 || n > 4'000'000) {
            err = "MD5 parse error: invalid numtris.";
            break;
          }
          m.tris.clear();
          m.tris.reserve(n * 3);
          continue;
        }

        if (kl == "tri") {
          bool ok0 = false, ok1 = false, ok2 = false, ok3 = false;
          QString tri_idx_s;
          QString i0_s, i1_s, i2_s;
          if (!tok.next(&tri_idx_s, &err) || !tok.next(&i0_s, &err) || !tok.next(&i1_s, &err) || !tok.next(&i2_s, &err)) {
            err = "MD5 parse error: invalid tri.";
            break;
          }
          (void)parse_int(tri_idx_s, &ok0);  // tri index (unused)
          const int i0 = parse_int(i0_s, &ok1);
          const int i1 = parse_int(i1_s, &ok2);
          const int i2 = parse_int(i2_s, &ok3);
          if (!(ok0 && ok1 && ok2 && ok3) || i0 < 0 || i1 < 0 || i2 < 0) {
            err = "MD5 parse error: invalid tri.";
            break;
          }
          m.tris.push_back(static_cast<std::uint32_t>(i0));
          m.tris.push_back(static_cast<std::uint32_t>(i1));
          m.tris.push_back(static_cast<std::uint32_t>(i2));
          continue;
        }

        if (kl == "numweights") {
          bool ok = false;
          QString n_s;
          if (!tok.next(&n_s, &err)) {
            err = "MD5 parse error: invalid numweights.";
            break;
          }
          const int n = parse_int(n_s, &ok);
          if (!ok || n < 0 || n > 8'000'000) {
            err = "MD5 parse error: invalid numweights.";
            break;
          }
          m.weights.clear();
          m.weights.resize(n);
          continue;
        }

        if (kl == "weight") {
          bool ok_idx = false;
          QString idx_s;
          if (!tok.next(&idx_s, &err)) {
            err = "MD5 parse error: invalid weight index.";
            break;
          }
          const int idx = parse_int(idx_s, &ok_idx);
          if (!ok_idx || idx < 0 || idx >= m.weights.size()) {
            err = "MD5 parse error: invalid weight index.";
            break;
          }
          bool okj = false, okb = false;
          QString joint_s;
          QString bias_s;
          if (!tok.next(&joint_s, &err) || !tok.next(&bias_s, &err)) {
            err = "MD5 parse error: invalid weight.";
            break;
          }
          const int joint = parse_int(joint_s, &okj);
          const float bias = parse_float(bias_s, &okb);
          if (!(okj && okb) || joint < 0) {
            err = "MD5 parse error: invalid weight.";
            break;
          }
          QVector3D pos;
          if (!read_vec3_paren(&pos, &err)) {
            break;
          }
          m.weights[idx].joint = joint;
          m.weights[idx].bias = bias;
          m.weights[idx].pos = pos;
          continue;
        }

        // Unknown key; skip one token best-effort.
        QString unused;
        (void)tok.next(&unused, &err);
      }

      if (!err.isEmpty()) {
        break;
      }
      meshes.push_back(std::move(m));
      continue;
    }
  }

  if (!err.isEmpty()) {
    if (error) {
      *error = err;
    }
    return std::nullopt;
  }

  if (joints.isEmpty() || meshes.isEmpty()) {
    if (error) {
      *error = "MD5 mesh is missing joints or meshes.";
    }
    return std::nullopt;
  }

  // Build world transforms.
  for (int i = 0; i < joints.size(); ++i) {
    Joint& j = joints[i];
    if (j.parent < 0) {
      j.world_orient = j.orient;
      j.world_pos = j.pos;
    } else if (j.parent >= 0 && j.parent < joints.size()) {
      const Joint& p = joints[j.parent];
      j.world_orient = p.world_orient * j.orient;
      j.world_pos = p.world_pos + p.world_orient.rotatedVector(j.pos);
    } else {
      j.world_orient = j.orient;
      j.world_pos = j.pos;
    }
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(meshes.size());

  for (int mi = 0; mi < meshes.size(); ++mi) {
    const Mesh& m = meshes[mi];
    const int base_vertex = vertices.size();

    vertices.reserve(vertices.size() + m.verts.size());
    for (const Vert& v : m.verts) {
      QVector3D p(0, 0, 0);
      const int start = v.start_weight;
      const int end = start + v.count_weight;
      for (int wi = start; wi < end && wi < m.weights.size(); ++wi) {
        const Weight& w = m.weights[wi];
        if (w.joint < 0 || w.joint >= joints.size()) {
          continue;
        }
        const Joint& j = joints[w.joint];
        const QVector3D wp = j.world_pos + j.world_orient.rotatedVector(w.pos);
        p += wp * w.bias;
      }

      ModelVertex mv;
      mv.px = p.x();
      mv.py = p.y();
      mv.pz = p.z();
      mv.u = v.u;
      mv.v = 1.0f - v.v;
      vertices.push_back(mv);
    }

    const int first_index = indices.size();
    indices.reserve(indices.size() + m.tris.size());
    const std::uint32_t base_u32 = static_cast<std::uint32_t>(base_vertex);
    const std::uint32_t vcount_u32 = static_cast<std::uint32_t>(m.verts.size());
    for (int ti = 0; ti + 2 < m.tris.size(); ti += 3) {
      const std::uint32_t i0 = m.tris[ti + 0];
      const std::uint32_t i1 = m.tris[ti + 1];
      const std::uint32_t i2 = m.tris[ti + 2];
      if (i0 >= vcount_u32 || i1 >= vcount_u32 || i2 >= vcount_u32) {
        continue;
      }
      indices.push_back(base_u32 + i0);
      indices.push_back(base_u32 + i1);
      indices.push_back(base_u32 + i2);
    }
    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface s;
      s.name = QString("mesh%1").arg(mi);
      s.shader = m.shader;
      s.first_index = first_index;
      s.index_count = index_count;
      surfaces.push_back(std::move(s));
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "MD5 contains no drawable geometry.";
    }
    return std::nullopt;
  }

  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "md5mesh";
  out.frame_count = 1;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
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
  if (ext == "iqm") {
    return load_iqm(info.absoluteFilePath(), error);
  }
  if (ext == "md5mesh") {
    return load_md5mesh(info.absoluteFilePath(), error);
  }

  if (error) {
    *error = "Unsupported model format.";
  }
  return std::nullopt;
}
