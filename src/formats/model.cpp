#include "formats/model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QQuaternion>
#include <QVector2D>

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

std::optional<LoadedModel> load_quake_mdl(const QString& file_path, QString* error) {
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
  QByteArray embedded_skin_indices;
  for (int s = 0; s < numskins; ++s) {
    qint32 type = 0;
    if (!cur.read_i32(&type)) {
      if (error) {
        *error = "MDL skins are incomplete.";
      }
      return std::nullopt;
    }
    if (type == 0) {
      QByteArray skin;
      if (!cur.read_bytes(skin_bytes, (s == 0) ? &skin : nullptr)) {
        if (error) {
          *error = "MDL skins are incomplete.";
        }
        return std::nullopt;
      }
      if (s == 0) {
        embedded_skin_indices = std::move(skin);
      }
    } else if (type == 1) {
      qint32 group = 0;
      if (!cur.read_i32(&group) || group <= 0 || group > 10000) {
        if (error) {
          *error = "MDL skin group is invalid.";
        }
        return std::nullopt;
      }
      const qint64 interval_bytes = static_cast<qint64>(group) * 4;
      if (interval_bytes < 0 || interval_bytes > std::numeric_limits<int>::max() || !cur.skip(static_cast<int>(interval_bytes))) {
        if (error) {
          *error = "MDL skin group is incomplete.";
        }
        return std::nullopt;
      }
      if (s == 0) {
        if (!cur.read_bytes(skin_bytes, &embedded_skin_indices)) {
          if (error) {
            *error = "MDL skin group is incomplete.";
          }
          return std::nullopt;
        }
        const qint64 remaining_groups = static_cast<qint64>(group - 1);
        const qint64 remaining_bytes = remaining_groups * static_cast<qint64>(skin_bytes);
        if (remaining_bytes < 0 || remaining_bytes > std::numeric_limits<int>::max() ||
            !cur.skip(static_cast<int>(remaining_bytes))) {
          if (error) {
            *error = "MDL skin group is incomplete.";
          }
          return std::nullopt;
        }
      } else {
        const qint64 group_bytes = static_cast<qint64>(group) * static_cast<qint64>(skin_bytes);
        if (group_bytes < 0 || group_bytes > std::numeric_limits<int>::max() || !cur.skip(static_cast<int>(group_bytes))) {
          if (error) {
            *error = "MDL skin group is incomplete.";
          }
          return std::nullopt;
        }
      }
    } else {
      if (error) {
        *error = QString("MDL has unknown skin type: %1").arg(type);
      }
      return std::nullopt;
    }
  }

  struct MdlSt {
    qint32 onseam = 0;
    qint32 s = 0;
    qint32 t = 0;
  };
  QVector<MdlSt> st;
  st.resize(numverts);
  for (int i = 0; i < numverts; ++i) {
    if (!cur.read_i32(&st[i].onseam) || !cur.read_i32(&st[i].s) || !cur.read_i32(&st[i].t)) {
      if (error) {
        *error = "MDL texture coordinates are incomplete.";
      }
      return std::nullopt;
    }
  }

  struct MdlTri {
    qint32 facesfront = 0;
    qint32 vi[3]{};
  };
  QVector<MdlTri> tris;
  tris.reserve(numtris);
  for (int t = 0; t < numtris; ++t) {
    MdlTri tri;
    if (!cur.read_i32(&tri.facesfront) || !cur.read_i32(&tri.vi[0]) || !cur.read_i32(&tri.vi[1]) || !cur.read_i32(&tri.vi[2])) {
      if (error) {
        *error = "MDL triangles are incomplete.";
      }
      return std::nullopt;
    }
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
      if (tri.vi[i] < 0 || tri.vi[i] >= numverts) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      continue;
    }
    tris.push_back(tri);
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

  QVector<ModelVertex> base_verts;
  if (frame_type == 0) {
    if (!read_frame_vertices(&base_verts)) {
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
    if (!read_frame_vertices(&base_verts)) {
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

  if (tris.isEmpty()) {
    if (error) {
      *error = "MDL contains no drawable geometry.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> verts;
  QVector<std::uint32_t> indices;
  verts.reserve(tris.size() * 3);
  indices.reserve(tris.size() * 3);

  const float inv_skinw = 1.0f / static_cast<float>(skinwidth);
  const float inv_skinh = 1.0f / static_cast<float>(skinheight);

  // Mirror Quake's original BuildTris logic so seam handling and UV generation
  // match engine behavior exactly.
  QVector<int> used(tris.size(), 0);
  QVector<int> strip_verts(tris.size() + 2, 0);
  QVector<int> strip_tris(tris.size(), 0);
  int strip_count = 0;

  const auto clear_temp_used = [&](int start_tri) {
    for (int j = start_tri + 1; j < tris.size(); ++j) {
      if (used[j] == 2) {
        used[j] = 0;
      }
    }
  };

  const auto strip_length = [&](int start_tri, int start_v) -> int {
    used[start_tri] = 2;
    const MdlTri& last = tris[start_tri];

    strip_verts[0] = last.vi[(start_v + 0) % 3];
    strip_verts[1] = last.vi[(start_v + 1) % 3];
    strip_verts[2] = last.vi[(start_v + 2) % 3];
    strip_tris[0] = start_tri;
    strip_count = 1;

    int m1 = last.vi[(start_v + 2) % 3];
    int m2 = last.vi[(start_v + 1) % 3];

    for (;;) {
      bool found = false;
      for (int j = start_tri + 1; j < tris.size(); ++j) {
        const MdlTri& check = tris[j];
        if (check.facesfront != last.facesfront) {
          continue;
        }
        for (int k = 0; k < 3; ++k) {
          if (check.vi[k] != m1 || check.vi[(k + 1) % 3] != m2) {
            continue;
          }
          if (used[j] != 0) {
            clear_temp_used(start_tri);
            return strip_count;
          }

          const int new_edge = check.vi[(k + 2) % 3];
          if ((strip_count & 1) != 0) {
            m2 = new_edge;
          } else {
            m1 = new_edge;
          }

          strip_verts[strip_count + 2] = new_edge;
          strip_tris[strip_count] = j;
          ++strip_count;
          used[j] = 2;
          found = true;
          break;
        }
        if (found) {
          break;
        }
      }
      if (!found) {
        break;
      }
    }

    clear_temp_used(start_tri);
    return strip_count;
  };

  const auto fan_length = [&](int start_tri, int start_v) -> int {
    used[start_tri] = 2;
    const MdlTri& last = tris[start_tri];

    strip_verts[0] = last.vi[(start_v + 0) % 3];
    strip_verts[1] = last.vi[(start_v + 1) % 3];
    strip_verts[2] = last.vi[(start_v + 2) % 3];
    strip_tris[0] = start_tri;
    strip_count = 1;

    int m1 = last.vi[(start_v + 0) % 3];
    int m2 = last.vi[(start_v + 2) % 3];

    for (;;) {
      bool found = false;
      for (int j = start_tri + 1; j < tris.size(); ++j) {
        const MdlTri& check = tris[j];
        if (check.facesfront != last.facesfront) {
          continue;
        }
        for (int k = 0; k < 3; ++k) {
          if (check.vi[k] != m1 || check.vi[(k + 1) % 3] != m2) {
            continue;
          }
          if (used[j] != 0) {
            clear_temp_used(start_tri);
            return strip_count;
          }

          const int new_edge = check.vi[(k + 2) % 3];
          m2 = new_edge;
          strip_verts[strip_count + 2] = new_edge;
          strip_tris[strip_count] = j;
          ++strip_count;
          used[j] = 2;
          found = true;
          break;
        }
        if (found) {
          break;
        }
      }
      if (!found) {
        break;
      }
    }

    clear_temp_used(start_tri);
    return strip_count;
  };

  QVector<int> best_verts(tris.size() + 2, 0);
  QVector<int> best_tris(tris.size(), 0);

  for (int i = 0; i < tris.size(); ++i) {
    if (used[i] != 0) {
      continue;
    }

    int best_len = 0;
    int best_type = 0;  // 0 = fan, 1 = strip
    for (int type = 0; type < 2; ++type) {
      for (int start_v = 0; start_v < 3; ++start_v) {
        const int len = (type == 1) ? strip_length(i, start_v) : fan_length(i, start_v);
        if (len <= best_len) {
          continue;
        }
        best_len = len;
        best_type = type;
        for (int j = 0; j < best_len + 2; ++j) {
          best_verts[j] = strip_verts[j];
        }
        for (int j = 0; j < best_len; ++j) {
          best_tris[j] = strip_tris[j];
        }
      }
    }

    if (best_len <= 0) {
      continue;
    }
    for (int j = 0; j < best_len; ++j) {
      used[best_tris[j]] = 1;
    }

    const bool faces_front = (tris[best_tris[0]].facesfront != 0);
    QVector<std::uint32_t> cmd_indices;
    cmd_indices.reserve(best_len + 2);
    for (int j = 0; j < best_len + 2; ++j) {
      const int vi = best_verts[j];
      if (vi < 0 || vi >= base_verts.size()) {
        continue;
      }

      ModelVertex v = base_verts[vi];
      qint32 s = st[vi].s;
      if (!faces_front && st[vi].onseam != 0) {
        s += (skinwidth / 2);
      }
      v.u = (static_cast<float>(s) + 0.5f) * inv_skinw;
      // Model textures are vertically flipped before GPU upload; keep MDL's top-origin
      // texture coordinates by inverting V here.
      v.v = 1.0f - ((static_cast<float>(st[vi].t) + 0.5f) * inv_skinh);

      cmd_indices.push_back(static_cast<std::uint32_t>(verts.size()));
      verts.push_back(v);
    }

    const int vertex_count = cmd_indices.size();
    if (vertex_count < 3) {
      continue;
    }

    if (best_type == 1) {  // strip
      for (int j = 0; j + 2 < vertex_count; ++j) {
        if ((j & 1) == 0) {
          indices.push_back(cmd_indices[j + 0]);
          indices.push_back(cmd_indices[j + 1]);
          indices.push_back(cmd_indices[j + 2]);
        } else {
          indices.push_back(cmd_indices[j + 1]);
          indices.push_back(cmd_indices[j + 0]);
          indices.push_back(cmd_indices[j + 2]);
        }
      }
    } else {  // fan
      const std::uint32_t anchor = cmd_indices[0];
      for (int j = 1; j + 1 < vertex_count; ++j) {
        indices.push_back(anchor);
        indices.push_back(cmd_indices[j]);
        indices.push_back(cmd_indices[j + 1]);
      }
    }
  }

  LoadedModel out;
  out.format = "mdl";
  out.frame_count = numframes;
  out.surface_count = 1;
  out.mesh.vertices = std::move(verts);
  out.mesh.indices = std::move(indices);
  out.surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(out.mesh.indices.size())}};
  if (embedded_skin_indices.size() == skin_bytes) {
    out.embedded_skin_indices = std::move(embedded_skin_indices);
    out.embedded_skin_width = skinwidth;
    out.embedded_skin_height = skinheight;
  }
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

struct GoldsrcBone {
  int parent = -1;
  float pos[3]{};
  float rot[3]{};
};

struct GoldsrcMat34 {
  float m[3][4]{};
};

struct GoldsrcTexture {
  QString name;
  int flags = 0;
  int width = 0;
  int height = 0;
  QByteArray rgba;
};

void goldsrc_angle_quaternion(const float angles[3], float q[4]) {
  const float half_z = angles[2] * 0.5f;
  const float sy = std::sin(half_z);
  const float cy = std::cos(half_z);
  const float half_y = angles[1] * 0.5f;
  const float sp = std::sin(half_y);
  const float cp = std::cos(half_y);
  const float half_x = angles[0] * 0.5f;
  const float sr = std::sin(half_x);
  const float cr = std::cos(half_x);

  q[0] = sr * cp * cy - cr * sp * sy;
  q[1] = cr * sp * cy + sr * cp * sy;
  q[2] = cr * cp * sy - sr * sp * cy;
  q[3] = cr * cp * cy + sr * sp * sy;
}

void goldsrc_quaternion_matrix(const float q[4], const float pos[3], GoldsrcMat34* out) {
  if (!out) {
    return;
  }
  out->m[0][0] = 1.0f - 2.0f * q[1] * q[1] - 2.0f * q[2] * q[2];
  out->m[1][0] = 2.0f * q[0] * q[1] + 2.0f * q[3] * q[2];
  out->m[2][0] = 2.0f * q[0] * q[2] - 2.0f * q[3] * q[1];

  out->m[0][1] = 2.0f * q[0] * q[1] - 2.0f * q[3] * q[2];
  out->m[1][1] = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[2] * q[2];
  out->m[2][1] = 2.0f * q[1] * q[2] + 2.0f * q[3] * q[0];

  out->m[0][2] = 2.0f * q[0] * q[2] + 2.0f * q[3] * q[1];
  out->m[1][2] = 2.0f * q[1] * q[2] - 2.0f * q[3] * q[0];
  out->m[2][2] = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[1] * q[1];

  out->m[0][3] = pos[0];
  out->m[1][3] = pos[1];
  out->m[2][3] = pos[2];
}

GoldsrcMat34 goldsrc_concat_transform(const GoldsrcMat34& a, const GoldsrcMat34& b) {
  GoldsrcMat34 out;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      out.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    }
    out.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
  }
  return out;
}

QVector3D goldsrc_transform_point(const GoldsrcMat34& m, const QVector3D& p) {
  return QVector3D(m.m[0][0] * p.x() + m.m[0][1] * p.y() + m.m[0][2] * p.z() + m.m[0][3],
                   m.m[1][0] * p.x() + m.m[1][1] * p.y() + m.m[1][2] * p.z() + m.m[1][3],
                   m.m[2][0] * p.x() + m.m[2][1] * p.y() + m.m[2][2] * p.z() + m.m[2][3]);
}

bool can_span_bytes(const QByteArray& bytes, qint64 offset, qint64 span) {
  if (offset < 0 || span < 0) {
    return false;
  }
  if (offset > bytes.size()) {
    return false;
  }
  return span <= static_cast<qint64>(bytes.size()) - offset;
}

bool load_file_bytes(const QString& file_path, QByteArray* out) {
  if (!out) {
    return false;
  }
  out->clear();
  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    return false;
  }
  *out = f.readAll();
  return true;
}

bool parse_goldsrc_textures_from_bytes(const QByteArray& bytes, QVector<GoldsrcTexture>* out_textures) {
  if (!out_textures) {
    return false;
  }
  out_textures->clear();

  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kGoldsrcIdent = 0x54534449u;  // "IDST"
  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    return false;
  }
  if (ident != kGoldsrcIdent || version != 10) {
    return false;
  }
  if (!cur.seek(140)) {
    return false;
  }

  qint32 header_i32[26]{};
  for (int i = 0; i < 26; ++i) {
    if (!cur.read_i32(&header_i32[i])) {
      return false;
    }
  }
  const qint32 numtextures = header_i32[10];
  const qint32 textureindex = header_i32[11];
  const qint32 texturedataindex = header_i32[12];
  if (numtextures <= 0 || textureindex <= 0 || texturedataindex < 0) {
    return true;
  }
  if (numtextures > 4096) {
    return false;
  }
  if (!can_span_bytes(bytes, textureindex, static_cast<qint64>(numtextures) * 80)) {
    return false;
  }

  // GoldSrc model texture payload offsets are not entirely consistent across tools:
  // some files store `index` as absolute file offsets, others as texturedata-relative.
  // Probe the table up front and pick the mode that validates the most textures.
  int absolute_offset_hits = 0;
  int relative_offset_hits = 0;
  for (int t = 0; t < numtextures; ++t) {
    const qint64 tex_off = static_cast<qint64>(textureindex) + static_cast<qint64>(t) * 80;
    if (!can_span_bytes(bytes, tex_off + 64, 16)) {
      continue;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + tex_off + 64);
    const qint32 width = static_cast<qint32>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    const qint32 height = static_cast<qint32>(p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24));
    const qint32 index = static_cast<qint32>(p[12] | (p[13] << 8) | (p[14] << 16) | (p[15] << 24));
    const qint64 pixel_count = static_cast<qint64>(width) * static_cast<qint64>(height);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || pixel_count <= 0) {
      continue;
    }
    if (can_span_bytes(bytes, static_cast<qint64>(index), pixel_count)) {
      ++absolute_offset_hits;
    }
    if (can_span_bytes(bytes, static_cast<qint64>(texturedataindex) + static_cast<qint64>(index), pixel_count)) {
      ++relative_offset_hits;
    }
  }
  const bool prefer_absolute_offsets = (absolute_offset_hits >= relative_offset_hits);

  constexpr int kStudioMasked = 0x40;
  out_textures->reserve(numtextures);
  for (int t = 0; t < numtextures; ++t) {
    const qint64 tex_off = static_cast<qint64>(textureindex) + static_cast<qint64>(t) * 80;
    if (!can_span_bytes(bytes, tex_off, 80) || !cur.seek(static_cast<int>(tex_off))) {
      return false;
    }

    GoldsrcTexture tex;
    qint32 index = 0;
    if (!cur.read_fixed_string(64, &tex.name) || !cur.read_i32(&tex.flags) || !cur.read_i32(&tex.width) ||
        !cur.read_i32(&tex.height) || !cur.read_i32(&index)) {
      return false;
    }
    tex.name.replace('\\', '/');

    const qint64 pixel_count = static_cast<qint64>(tex.width) * static_cast<qint64>(tex.height);
    if (tex.width <= 0 || tex.height <= 0 || tex.width > 8192 || tex.height > 8192 || pixel_count <= 0) {
      out_textures->push_back(std::move(tex));
      continue;
    }

    const qint64 pixel_off_abs = static_cast<qint64>(index);
    const qint64 pixel_off_rel = static_cast<qint64>(texturedataindex) + static_cast<qint64>(index);
    qint64 pixel_off = -1;
    if (prefer_absolute_offsets) {
      if (can_span_bytes(bytes, pixel_off_abs, pixel_count)) {
        pixel_off = pixel_off_abs;
      } else if (can_span_bytes(bytes, pixel_off_rel, pixel_count)) {
        pixel_off = pixel_off_rel;
      }
    } else {
      if (can_span_bytes(bytes, pixel_off_rel, pixel_count)) {
        pixel_off = pixel_off_rel;
      } else if (can_span_bytes(bytes, pixel_off_abs, pixel_count)) {
        pixel_off = pixel_off_abs;
      }
    }
    if (pixel_off < 0) {
      out_textures->push_back(std::move(tex));
      continue;
    }

    const auto* pixel = reinterpret_cast<const unsigned char*>(bytes.constData() + pixel_off);
    const qint64 level0_tail = pixel_off + pixel_count;
    const qint64 mip_tail =
        pixel_off + pixel_count + (pixel_count / 4) + (pixel_count / 16) + (pixel_count / 64);

    int palette_colors = 0;
    qint64 palette_rgb_off = level0_tail;
    int palette_score = std::numeric_limits<int>::min();

    const auto try_palette_at = [&](qint64 probe_off, int base_score) {
      if (probe_off < 0 || probe_off >= bytes.size()) {
        return;
      }

      if (can_span_bytes(bytes, probe_off, 2)) {
        const auto* pal_count_ptr = reinterpret_cast<const unsigned char*>(bytes.constData() + probe_off);
        const quint16 declared = static_cast<quint16>(pal_count_ptr[0] | (pal_count_ptr[1] << 8));
        if (declared > 0 && declared <= 256 &&
            can_span_bytes(bytes, probe_off + 2, static_cast<qint64>(declared) * 3)) {
          int score = base_score + 100;
          if (declared == 256) {
            score += 25;
          }
          if (score > palette_score) {
            palette_score = score;
            palette_colors = static_cast<int>(declared);
            palette_rgb_off = probe_off + 2;
          }
        }
      }

      if (can_span_bytes(bytes, probe_off, 256 * 3)) {
        const int score = base_score + 50;
        if (score > palette_score) {
          palette_score = score;
          palette_colors = 256;
          palette_rgb_off = probe_off;
        }
      }
    };

    // GoldSrc model textures are commonly either:
    // - level0 indexed pixels + palette
    // - Quake-style mip chain + palette
    // Probe both tails and keep the strongest match.
    try_palette_at(level0_tail, 100);
    if (mip_tail != level0_tail) {
      try_palette_at(mip_tail, 90);
    }

    const qint64 rgba_bytes = pixel_count * 4;
    if (rgba_bytes <= 0 || rgba_bytes > std::numeric_limits<int>::max()) {
      out_textures->push_back(std::move(tex));
      continue;
    }
    tex.rgba.resize(static_cast<int>(rgba_bytes));
    auto* dst = reinterpret_cast<unsigned char*>(tex.rgba.data());
    const auto* palette =
        (palette_colors > 0) ? reinterpret_cast<const unsigned char*>(bytes.constData() + palette_rgb_off) : nullptr;
    const bool masked = (tex.flags & kStudioMasked) != 0;

    for (qint64 i = 0; i < pixel_count; ++i) {
      const unsigned char idx = pixel[i];
      const qint64 o = i * 4;
      if (palette) {
        const int pi = std::min(static_cast<int>(idx), palette_colors - 1);
        const int po = pi * 3;
        dst[o + 0] = palette[po + 0];
        dst[o + 1] = palette[po + 1];
        dst[o + 2] = palette[po + 2];
      } else {
        dst[o + 0] = idx;
        dst[o + 1] = idx;
        dst[o + 2] = idx;
      }
      dst[o + 3] = (masked && idx == 255) ? 0 : 255;
    }

    out_textures->push_back(std::move(tex));
  }

  return true;
}

QString find_goldsrc_texture_companion(const QString& model_file_path) {
  const QFileInfo model_info(model_file_path);
  const QString model_base = model_info.completeBaseName();
  if (model_base.isEmpty()) {
    return {};
  }
  QDir dir(model_info.absolutePath());
  if (!dir.exists()) {
    return {};
  }

  const QString upper = dir.filePath(QString("%1T.mdl").arg(model_base));
  if (QFileInfo::exists(upper)) {
    return upper;
  }
  const QString lower = dir.filePath(QString("%1t.mdl").arg(model_base));
  if (QFileInfo::exists(lower)) {
    return lower;
  }

  const QFileInfoList mdls = dir.entryInfoList(QStringList() << "*.mdl", QDir::Files | QDir::Readable, QDir::Name);
  for (const QFileInfo& fi : mdls) {
    if (fi.completeBaseName().compare(model_base + "t", Qt::CaseInsensitive) == 0) {
      return fi.absoluteFilePath();
    }
  }
  return {};
}

std::optional<LoadedModel> load_goldsrc_mdl(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QByteArray bytes;
  if (!load_file_bytes(file_path, &bytes)) {
    if (error) {
      *error = "Unable to open MDL.";
    }
    return std::nullopt;
  }

  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kGoldsrcIdent = 0x54534449u;  // "IDST"
  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }
  if (ident != kGoldsrcIdent || version != 10) {
    if (error) {
      *error = "Not a supported GoldSrc MDL (expected IDST v10).";
    }
    return std::nullopt;
  }

  if (!cur.seek(140)) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }

  qint32 header_i32[26]{};
  for (int i = 0; i < 26; ++i) {
    if (!cur.read_i32(&header_i32[i])) {
      if (error) {
        *error = "MDL header is incomplete.";
      }
      return std::nullopt;
    }
  }

  const qint32 numbones = header_i32[0];
  const qint32 boneindex = header_i32[1];
  const qint32 numseq = header_i32[6];
  const qint32 numskinref = header_i32[13];
  const qint32 numskinfamilies = header_i32[14];
  const qint32 skinindex = header_i32[15];
  const qint32 numbodyparts = header_i32[16];
  const qint32 bodypartindex = header_i32[17];

  if (numbones < 0 || numbones > 4096 || numbodyparts < 0 || numbodyparts > 4096) {
    if (error) {
      *error = "GoldSrc MDL header values are invalid.";
    }
    return std::nullopt;
  }
  if (numbones > 0 && !can_span_bytes(bytes, boneindex, static_cast<qint64>(numbones) * 112)) {
    if (error) {
      *error = "GoldSrc MDL bone table is invalid.";
    }
    return std::nullopt;
  }
  if (numbodyparts > 0 && !can_span_bytes(bytes, bodypartindex, static_cast<qint64>(numbodyparts) * 76)) {
    if (error) {
      *error = "GoldSrc MDL bodypart table is invalid.";
    }
    return std::nullopt;
  }

  QVector<GoldsrcBone> bones;
  bones.resize(std::max(0, static_cast<int>(numbones)));
  for (int b = 0; b < bones.size(); ++b) {
    const qint64 off = static_cast<qint64>(boneindex) + static_cast<qint64>(b) * 112;
    if (!cur.seek(static_cast<int>(off))) {
      if (error) {
        *error = "GoldSrc MDL bone table is incomplete.";
      }
      return std::nullopt;
    }

    if (!cur.skip(32)) {
      if (error) {
        *error = "GoldSrc MDL bone table is incomplete.";
      }
      return std::nullopt;
    }
    qint32 parent = -1;
    if (!cur.read_i32(&parent) || !cur.skip(4 + (6 * 4))) {
      if (error) {
        *error = "GoldSrc MDL bone table is incomplete.";
      }
      return std::nullopt;
    }

    float values[6]{};
    for (float& v : values) {
      if (!cur.read_f32(&v)) {
        if (error) {
          *error = "GoldSrc MDL bone table is incomplete.";
        }
        return std::nullopt;
      }
    }
    if (!cur.skip(6 * 4)) {
      if (error) {
        *error = "GoldSrc MDL bone table is incomplete.";
      }
      return std::nullopt;
    }

    bones[b].parent = (parent >= 0 && parent < bones.size()) ? parent : -1;
    bones[b].pos[0] = values[0];
    bones[b].pos[1] = values[1];
    bones[b].pos[2] = values[2];
    bones[b].rot[0] = values[3];
    bones[b].rot[1] = values[4];
    bones[b].rot[2] = values[5];
  }

  QVector<GoldsrcMat34> bone_local;
  QVector<GoldsrcMat34> bone_world;
  QVector<int> bone_state;
  bone_local.resize(bones.size());
  bone_world.resize(bones.size());
  bone_state.resize(bones.size());
  for (int i = 0; i < bone_state.size(); ++i) {
    bone_state[i] = 0;
  }
  for (int i = 0; i < bones.size(); ++i) {
    float q[4]{};
    goldsrc_angle_quaternion(bones[i].rot, q);
    goldsrc_quaternion_matrix(q, bones[i].pos, &bone_local[i]);
  }

  std::function<bool(int)> build_bone_world = [&](int i) -> bool {
    if (i < 0 || i >= bone_world.size()) {
      return false;
    }
    if (bone_state[i] == 2) {
      return true;
    }
    if (bone_state[i] == 1) {
      return false;
    }
    bone_state[i] = 1;
    const int parent = bones[i].parent;
    if (parent >= 0) {
      if (!build_bone_world(parent)) {
        return false;
      }
      bone_world[i] = goldsrc_concat_transform(bone_world[parent], bone_local[i]);
    } else {
      bone_world[i] = bone_local[i];
    }
    bone_state[i] = 2;
    return true;
  };
  for (int i = 0; i < bones.size(); ++i) {
    if (!build_bone_world(i)) {
      if (error) {
        *error = "GoldSrc MDL bone hierarchy is invalid.";
      }
      return std::nullopt;
    }
  }

  QVector<int> skinref_to_texture;
  if (numskinref > 0 && numskinfamilies > 0 && numskinref <= 4096 && numskinfamilies <= 1024) {
    const qint64 entries = static_cast<qint64>(numskinref) * static_cast<qint64>(numskinfamilies);
    if (entries > 0 && entries <= 1'000'000 && can_span_bytes(bytes, skinindex, entries * 2) && cur.seek(skinindex)) {
      skinref_to_texture.resize(numskinref);
      bool ok = true;
      for (int family = 0; family < numskinfamilies && ok; ++family) {
        for (int r = 0; r < numskinref; ++r) {
          qint16 v = 0;
          if (!cur.read_i16(&v)) {
            ok = false;
            break;
          }
          if (family == 0) {
            skinref_to_texture[r] = static_cast<int>(v);
          }
        }
      }
      if (!ok) {
        skinref_to_texture.clear();
      }
    }
  }

  QVector<GoldsrcTexture> textures;
  (void)parse_goldsrc_textures_from_bytes(bytes, &textures);
  if (textures.isEmpty()) {
    const QString tex_file = find_goldsrc_texture_companion(file_path);
    if (!tex_file.isEmpty()) {
      QByteArray tex_bytes;
      if (load_file_bytes(tex_file, &tex_bytes)) {
        QVector<GoldsrcTexture> tex_from_companion;
        if (parse_goldsrc_textures_from_bytes(tex_bytes, &tex_from_companion) && !tex_from_companion.isEmpty()) {
          textures = std::move(tex_from_companion);
        }
      }
    }
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;

  for (int bp = 0; bp < numbodyparts; ++bp) {
    const qint64 bp_off = static_cast<qint64>(bodypartindex) + static_cast<qint64>(bp) * 76;
    if (!can_span_bytes(bytes, bp_off, 76) || !cur.seek(static_cast<int>(bp_off))) {
      continue;
    }

    QString body_name;
    qint32 nummodels = 0;
    qint32 base = 0;
    qint32 modelindex = 0;
    if (!cur.read_fixed_string(64, &body_name) || !cur.read_i32(&nummodels) || !cur.read_i32(&base) ||
        !cur.read_i32(&modelindex)) {
      continue;
    }
    (void)base;
    if (nummodels <= 0 || nummodels > 4096 ||
        !can_span_bytes(bytes, modelindex, static_cast<qint64>(nummodels) * 112)) {
      continue;
    }

    for (int model_slot = 0; model_slot < nummodels; ++model_slot) {
      const qint64 model_off = static_cast<qint64>(modelindex) + static_cast<qint64>(model_slot) * 112;
      if (!can_span_bytes(bytes, model_off, 112) || !cur.seek(static_cast<int>(model_off))) {
        continue;
      }

      QString model_name;
      qint32 model_type = 0;
      float bounding_radius = 0.0f;
      qint32 nummesh = 0;
      qint32 meshindex = 0;
      qint32 numverts = 0;
      qint32 vertinfoindex = 0;
      qint32 vertindex = 0;
      qint32 numnorms = 0;
      qint32 norminfoindex = 0;
      qint32 normindex = 0;
      qint32 numgroups = 0;
      qint32 groupindex = 0;
      if (!cur.read_fixed_string(64, &model_name) || !cur.read_i32(&model_type) || !cur.read_f32(&bounding_radius) ||
          !cur.read_i32(&nummesh) || !cur.read_i32(&meshindex) || !cur.read_i32(&numverts) ||
          !cur.read_i32(&vertinfoindex) || !cur.read_i32(&vertindex) || !cur.read_i32(&numnorms) ||
          !cur.read_i32(&norminfoindex) || !cur.read_i32(&normindex) || !cur.read_i32(&numgroups) ||
          !cur.read_i32(&groupindex)) {
        continue;
      }
      (void)model_type;
      (void)bounding_radius;
      (void)numnorms;
      (void)norminfoindex;
      (void)normindex;
      (void)numgroups;
      (void)groupindex;

      if (nummesh <= 0 || nummesh > 100000 || numverts <= 0 || numverts > 1000000) {
        continue;
      }
      if (!can_span_bytes(bytes, meshindex, static_cast<qint64>(nummesh) * 20) ||
          !can_span_bytes(bytes, vertinfoindex, numverts) ||
          !can_span_bytes(bytes, vertindex, static_cast<qint64>(numverts) * 12)) {
        continue;
      }

      QByteArray vert_bone;
      if (!cur.seek(vertinfoindex) || !cur.read_bytes(numverts, &vert_bone)) {
        continue;
      }

      QVector<ModelVertex> model_vertices;
      model_vertices.resize(numverts);
      if (!cur.seek(vertindex)) {
        continue;
      }
      bool verts_ok = true;
      for (int v = 0; v < numverts; ++v) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!cur.read_f32(&x) || !cur.read_f32(&y) || !cur.read_f32(&z)) {
          verts_ok = false;
          break;
        }
        QVector3D p(x, y, z);
        const int bone = static_cast<unsigned char>(vert_bone[v]);
        if (bone >= 0 && bone < bone_world.size()) {
          p = goldsrc_transform_point(bone_world[bone], p);
        }
        ModelVertex mv;
        mv.px = p.x();
        mv.py = p.y();
        mv.pz = p.z();
        model_vertices[v] = mv;
      }
      if (!verts_ok) {
        continue;
      }

      for (int m = 0; m < nummesh; ++m) {
        const qint64 mesh_off = static_cast<qint64>(meshindex) + static_cast<qint64>(m) * 20;
        if (!can_span_bytes(bytes, mesh_off, 20) || !cur.seek(static_cast<int>(mesh_off))) {
          continue;
        }

        qint32 numtris = 0;
        qint32 triindex = 0;
        qint32 skinref = 0;
        qint32 mesh_numnorms = 0;
        qint32 mesh_normindex = 0;
        if (!cur.read_i32(&numtris) || !cur.read_i32(&triindex) || !cur.read_i32(&skinref) ||
            !cur.read_i32(&mesh_numnorms) || !cur.read_i32(&mesh_normindex)) {
          continue;
        }
        (void)numtris;
        (void)mesh_numnorms;
        (void)mesh_normindex;
        if (triindex < 0 || triindex >= bytes.size()) {
          continue;
        }

        int texture_index = skinref;
        if (!skinref_to_texture.isEmpty() && skinref >= 0 && skinref < skinref_to_texture.size()) {
          texture_index = skinref_to_texture[skinref];
        }

        const GoldsrcTexture* tex = nullptr;
        if (texture_index >= 0 && texture_index < textures.size()) {
          tex = &textures[texture_index];
        }
        const int tex_w = tex ? tex->width : 0;
        const int tex_h = tex ? tex->height : 0;
        QString shader = tex ? tex->name.trimmed() : QString();
        if (shader.isEmpty() && texture_index >= 0) {
          shader = QString("texture_%1").arg(texture_index);
        }

        QString surface_base = model_name.trimmed();
        if (surface_base.isEmpty()) {
          surface_base = body_name.trimmed();
        }
        if (surface_base.isEmpty()) {
          surface_base = "model";
        }

        ModelSurface surface;
        if (nummodels > 1) {
          surface.name = QString("%1_model%2_mesh%3").arg(surface_base).arg(model_slot).arg(m);
        } else {
          surface.name = QString("%1_mesh%2").arg(surface_base).arg(m);
        }
        surface.shader = shader;
        surface.first_index = static_cast<int>(indices.size());

        if (!cur.seek(triindex)) {
          continue;
        }

        bool stream_ok = true;
        int cmd_blocks = 0;
        while (cur.can_read(2)) {
          qint16 cmd = 0;
          if (!cur.read_i16(&cmd)) {
            stream_ok = false;
            break;
          }
          if (cmd == 0) {
            break;
          }

          const int count = std::abs(static_cast<int>(cmd));
          if (count <= 0 || count > 32767) {
            stream_ok = false;
            break;
          }

          QVector<std::uint32_t> cmd_indices;
          cmd_indices.reserve(count);
          for (int i = 0; i < count; ++i) {
            qint16 vi = 0;
            qint16 ni = 0;
            qint16 s = 0;
            qint16 t = 0;
            if (!cur.read_i16(&vi) || !cur.read_i16(&ni) || !cur.read_i16(&s) || !cur.read_i16(&t)) {
              stream_ok = false;
              break;
            }
            (void)ni;
            if (vi < 0 || vi >= model_vertices.size()) {
              continue;
            }

            ModelVertex v = model_vertices[vi];
            if (tex_w > 0) {
              v.u = (static_cast<float>(s) + 0.5f) / static_cast<float>(tex_w);
            }
            if (tex_h > 0) {
              // GoldSrc UV origin is top-left; preview upload flips source vertically.
              v.v = 1.0f - ((static_cast<float>(t) + 0.5f) / static_cast<float>(tex_h));
            }

            cmd_indices.push_back(static_cast<std::uint32_t>(vertices.size()));
            vertices.push_back(v);
          }
          if (!stream_ok) {
            break;
          }

          const int vertex_count = cmd_indices.size();
          if (vertex_count < 3) {
            continue;
          }

          if (cmd > 0) {  // strip
            for (int i = 0; i + 2 < vertex_count; ++i) {
              if ((i & 1) == 0) {
                indices.push_back(cmd_indices[i + 0]);
                indices.push_back(cmd_indices[i + 1]);
                indices.push_back(cmd_indices[i + 2]);
              } else {
                indices.push_back(cmd_indices[i + 1]);
                indices.push_back(cmd_indices[i + 0]);
                indices.push_back(cmd_indices[i + 2]);
              }
            }
          } else {  // fan
            const std::uint32_t anchor = cmd_indices[0];
            for (int i = 1; i + 1 < vertex_count; ++i) {
              indices.push_back(anchor);
              indices.push_back(cmd_indices[i]);
              indices.push_back(cmd_indices[i + 1]);
            }
          }

          ++cmd_blocks;
          if (cmd_blocks > 2000000) {
            stream_ok = false;
            break;
          }
        }
        if (!stream_ok) {
          continue;
        }

        surface.index_count = static_cast<int>(indices.size()) - surface.first_index;
        if (surface.index_count > 0) {
          surfaces.push_back(std::move(surface));
        }
      }
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "GoldSrc MDL contains no drawable geometry.";
    }
    return std::nullopt;
  }

  LoadedModel out;
  out.format = "mdl";
  out.frame_count = std::max(1, static_cast<int>(numseq));
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  if (surfaces.isEmpty()) {
    out.surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(out.mesh.indices.size())}};
  } else {
    out.surfaces = std::move(surfaces);
  }

  out.embedded_textures.clear();
  out.embedded_textures.resize(textures.size());
  int first_valid_tex_slot = -1;
  for (int ti = 0; ti < textures.size(); ++ti) {
    GoldsrcTexture& tex = textures[ti];
    EmbeddedTexture et;
    et.name = tex.name.trimmed();
    if (et.name.isEmpty()) {
      et.name = QString("texture_%1").arg(ti);
    }

    const qint64 pixel_count = static_cast<qint64>(tex.width) * static_cast<qint64>(tex.height);
    if (pixel_count > 0 && tex.width > 0 && tex.height > 0 && tex.rgba.size() == pixel_count * 4) {
      et.rgba = std::move(tex.rgba);
      et.width = tex.width;
      et.height = tex.height;
      if (first_valid_tex_slot < 0) {
        first_valid_tex_slot = ti;
      }
    }

    out.embedded_textures[ti] = std::move(et);
  }

  if (first_valid_tex_slot >= 0 && first_valid_tex_slot < out.embedded_textures.size()) {
    const EmbeddedTexture& first_tex = out.embedded_textures[first_valid_tex_slot];
    out.embedded_skin_rgba = first_tex.rgba;
    out.embedded_skin_width = first_tex.width;
    out.embedded_skin_height = first_tex.height;
  }

  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
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

  const QByteArray header = f.read(8);
  if (header.size() < 8) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }

  Cursor cur;
  cur.bytes = &header;

  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "MDL header is incomplete.";
    }
    return std::nullopt;
  }

  constexpr quint32 kQuakeIdent = 0x4F504449u;    // "IDPO"
  constexpr quint32 kGoldsrcIdent = 0x54534449u;  // "IDST"
  constexpr quint32 kGoldsrcSeqIdent = 0x51534449u;  // "IDSQ"
  if (ident == kQuakeIdent && version == 6) {
    return load_quake_mdl(file_path, error);
  }
  if (ident == kGoldsrcIdent && version == 10) {
    return load_goldsrc_mdl(file_path, error);
  }
  if (ident == kGoldsrcSeqIdent && version == 10) {
    if (error) {
      *error = "GoldSrc IDSQ sequence-group MDL has no standalone renderable mesh.";
    }
    return std::nullopt;
  }

  if (error) {
    *error = "Unsupported MDL variant (expected Quake IDPO v6 or GoldSrc IDST v10).";
  }
  return std::nullopt;
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

std::optional<LoadedModel> load_tan(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open TAN.";
    }
    return std::nullopt;
  }

  const QByteArray bytes = f.readAll();
  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kIdent = static_cast<quint32>('T') | (static_cast<quint32>('A') << 8) |
                              (static_cast<quint32>('N') << 16) | (static_cast<quint32>(' ') << 24);
  constexpr qint32 kVersion = 2;
  constexpr int kSurfaceHeaderSize = 104;
  constexpr int kMaxFrames = 10000;
  constexpr int kMaxSurfaces = 1024;
  constexpr int kMaxVerts = 1000000;
  constexpr int kMaxTriangles = 2000000;
  constexpr int kHeaderTagOffsetCount = 16;

  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "TAN header is incomplete.";
    }
    return std::nullopt;
  }
  if (ident != kIdent || version != kVersion) {
    if (error) {
      *error = "Not a supported TAN (expected TAN version 2).";
    }
    return std::nullopt;
  }

  if (!cur.skip(64)) {  // name
    if (error) {
      *error = "TAN header is incomplete.";
    }
    return std::nullopt;
  }

  qint32 num_frames = 0;
  qint32 num_tags = 0;
  qint32 num_surfaces = 0;
  float total_time = 0.0f;
  float total_delta[3]{};
  qint32 ofs_frames = 0;
  qint32 ofs_surfaces = 0;
  qint32 tag_offsets[kHeaderTagOffsetCount]{};
  qint32 ofs_end = 0;

  if (!cur.read_i32(&num_frames) ||
      !cur.read_i32(&num_tags) ||
      !cur.read_i32(&num_surfaces) ||
      !cur.read_f32(&total_time) ||
      !cur.read_f32(&total_delta[0]) ||
      !cur.read_f32(&total_delta[1]) ||
      !cur.read_f32(&total_delta[2]) ||
      !cur.read_i32(&ofs_frames) ||
      !cur.read_i32(&ofs_surfaces)) {
    if (error) {
      *error = "TAN header is incomplete.";
    }
    return std::nullopt;
  }
  for (int i = 0; i < kHeaderTagOffsetCount; ++i) {
    if (!cur.read_i32(&tag_offsets[i])) {
      if (error) {
        *error = "TAN header is incomplete.";
      }
      return std::nullopt;
    }
  }
  if (!cur.read_i32(&ofs_end)) {
    if (error) {
      *error = "TAN header is incomplete.";
    }
    return std::nullopt;
  }

  Q_UNUSED(total_time);
  Q_UNUSED(total_delta);
  Q_UNUSED(num_tags);
  Q_UNUSED(tag_offsets);

  const int file_size = bytes.size();
  if (num_frames <= 0 || num_frames > kMaxFrames ||
      num_surfaces < 0 || num_surfaces > kMaxSurfaces) {
    if (error) {
      *error = "TAN header values are invalid.";
    }
    return std::nullopt;
  }
  if (ofs_frames < 0 || ofs_frames + 48 > file_size ||
      ofs_surfaces < 0 || ofs_surfaces > file_size ||
      ofs_end <= 0 || ofs_end > file_size) {
    if (error) {
      *error = "TAN header offsets are invalid.";
    }
    return std::nullopt;
  }

  float frame_scale[3]{};
  float frame_offset[3]{};
  if (!cur.seek(ofs_frames + 24) ||
      !cur.read_f32(&frame_scale[0]) || !cur.read_f32(&frame_scale[1]) || !cur.read_f32(&frame_scale[2]) ||
      !cur.read_f32(&frame_offset[0]) || !cur.read_f32(&frame_offset[1]) || !cur.read_f32(&frame_offset[2])) {
    if (error) {
      *error = "Unable to parse TAN frame data.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(num_surfaces);

  int surf_off = ofs_surfaces;
  for (int s = 0; s < num_surfaces; ++s) {
    if (surf_off < 0 || surf_off + kSurfaceHeaderSize > file_size) {
      if (error) {
        *error = "TAN surface header is invalid.";
      }
      return std::nullopt;
    }
    if (!cur.seek(surf_off)) {
      if (error) {
        *error = "TAN surface header is invalid.";
      }
      return std::nullopt;
    }

    quint32 surf_ident = 0;
    QString surf_name;
    if (!cur.read_u32(&surf_ident) || surf_ident != kIdent || !cur.read_fixed_string(64, &surf_name)) {
      if (error) {
        *error = "TAN surface header is invalid.";
      }
      return std::nullopt;
    }

    qint32 surf_num_frames = 0;
    qint32 surf_num_verts = 0;
    qint32 surf_min_lod = 0;
    qint32 surf_num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 ofs_collapse = 0;
    qint32 ofs_st = 0;
    qint32 ofs_xyznormal = 0;
    qint32 ofs_surf_end = 0;
    if (!cur.read_i32(&surf_num_frames) || !cur.read_i32(&surf_num_verts) || !cur.read_i32(&surf_min_lod) ||
        !cur.read_i32(&surf_num_tris) || !cur.read_i32(&ofs_tris) || !cur.read_i32(&ofs_collapse) ||
        !cur.read_i32(&ofs_st) || !cur.read_i32(&ofs_xyznormal) || !cur.read_i32(&ofs_surf_end)) {
      if (error) {
        *error = "TAN surface header is incomplete.";
      }
      return std::nullopt;
    }
    Q_UNUSED(surf_min_lod);

    if (surf_num_frames <= 0 || surf_num_verts <= 0 || surf_num_verts > kMaxVerts ||
        surf_num_tris < 0 || surf_num_tris > kMaxTriangles || ofs_surf_end <= 0) {
      if (error) {
        *error = "TAN surface values are invalid.";
      }
      return std::nullopt;
    }

    const qint64 surf_end = static_cast<qint64>(surf_off) + static_cast<qint64>(ofs_surf_end);
    if (surf_end <= surf_off || surf_end > file_size || surf_end > ofs_end) {
      if (error) {
        *error = "TAN surface exceeds file bounds.";
      }
      return std::nullopt;
    }

    const qint64 tri_bytes = static_cast<qint64>(surf_num_tris) * 3LL * 4LL;
    const qint64 collapse_bytes = static_cast<qint64>(surf_num_verts) * 4LL;
    const qint64 st_bytes = static_cast<qint64>(surf_num_verts) * 8LL;
    const qint64 frame_vertex_stride = static_cast<qint64>(surf_num_verts) * 8LL;
    const qint64 xyz_bytes = frame_vertex_stride * static_cast<qint64>(surf_num_frames);

    const auto span_in_surface = [&](qint32 rel_ofs, qint64 len) -> bool {
      if (rel_ofs < 0 || len < 0) {
        return false;
      }
      const qint64 begin = static_cast<qint64>(surf_off) + static_cast<qint64>(rel_ofs);
      const qint64 end = begin + len;
      return begin >= surf_off && end >= begin && end <= surf_end;
    };

    if (!span_in_surface(ofs_tris, tri_bytes) ||
        !span_in_surface(ofs_collapse, collapse_bytes) ||
        !span_in_surface(ofs_st, st_bytes) ||
        !span_in_surface(ofs_xyznormal, xyz_bytes)) {
      if (error) {
        *error = "TAN surface offsets are invalid.";
      }
      return std::nullopt;
    }

    const int base_vertex = vertices.size();
    if (base_vertex > (std::numeric_limits<int>::max() - surf_num_verts)) {
      if (error) {
        *error = "TAN vertex count is too large.";
      }
      return std::nullopt;
    }
    vertices.resize(base_vertex + surf_num_verts);

    if (!cur.seek(surf_off + ofs_xyznormal)) {
      if (error) {
        *error = "TAN vertex offset is invalid.";
      }
      return std::nullopt;
    }
    for (int v = 0; v < surf_num_verts; ++v) {
      qint16 x = 0;
      qint16 y = 0;
      qint16 z = 0;
      qint16 n = 0;
      if (!cur.read_i16(&x) || !cur.read_i16(&y) || !cur.read_i16(&z) || !cur.read_i16(&n)) {
        if (error) {
          *error = "TAN vertices are incomplete.";
        }
        return std::nullopt;
      }
      Q_UNUSED(n);

      ModelVertex mv;
      mv.px = static_cast<float>(static_cast<quint16>(x)) * frame_scale[0] + frame_offset[0];
      mv.py = static_cast<float>(static_cast<quint16>(y)) * frame_scale[1] + frame_offset[1];
      mv.pz = static_cast<float>(static_cast<quint16>(z)) * frame_scale[2] + frame_offset[2];
      vertices[base_vertex + v] = mv;
    }

    if (!cur.seek(surf_off + ofs_st)) {
      if (error) {
        *error = "TAN texture-coordinate offset is invalid.";
      }
      return std::nullopt;
    }
    for (int v = 0; v < surf_num_verts; ++v) {
      float s0 = 0.0f;
      float t0 = 0.0f;
      if (!cur.read_f32(&s0) || !cur.read_f32(&t0)) {
        if (error) {
          *error = "TAN texture coordinates are incomplete.";
        }
        return std::nullopt;
      }
      vertices[base_vertex + v].u = s0;
      vertices[base_vertex + v].v = 1.0f - t0;
    }

    if (!cur.seek(surf_off + ofs_tris)) {
      if (error) {
        *error = "TAN triangle offset is invalid.";
      }
      return std::nullopt;
    }
    const qint64 tri_index_add = static_cast<qint64>(surf_num_tris) * 3LL;
    if (tri_index_add > (std::numeric_limits<int>::max() - static_cast<qint64>(indices.size()))) {
      if (error) {
        *error = "TAN triangle count is too large.";
      }
      return std::nullopt;
    }
    const int first_index = indices.size();
    indices.reserve(indices.size() + static_cast<int>(tri_index_add));
    for (int t = 0; t < surf_num_tris; ++t) {
      qint32 i0 = 0;
      qint32 i1 = 0;
      qint32 i2 = 0;
      if (!cur.read_i32(&i0) || !cur.read_i32(&i1) || !cur.read_i32(&i2)) {
        if (error) {
          *error = "TAN triangles are incomplete.";
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
      surf.name = surf_name.isEmpty() ? QString("surface_%1").arg(s) : surf_name;
      surf.shader = surf.name;
      surf.first_index = first_index;
      surf.index_count = index_count;
      surfaces.push_back(std::move(surf));
    }

    const qint64 next_surf_off = static_cast<qint64>(surf_off) + static_cast<qint64>(ofs_surf_end);
    if (next_surf_off <= 0 || next_surf_off > ofs_end || next_surf_off > std::numeric_limits<int>::max()) {
      break;
    }
    surf_off = static_cast<int>(next_surf_off);
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "TAN contains no drawable geometry.";
    }
    return std::nullopt;
  }

  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "tan";
  out.frame_count = num_frames;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_mdc(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MDC.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  Cursor cur;
  cur.bytes = &bytes;

  constexpr quint32 kIdent = 0x43504449u;  // "IDPC"
  constexpr qint32 kVersion = 2;

  quint32 ident = 0;
  qint32 version = 0;
  if (!cur.read_u32(&ident) || !cur.read_i32(&version)) {
    if (error) {
      *error = "MDC header is incomplete.";
    }
    return std::nullopt;
  }
  if (ident != kIdent || version != kVersion) {
    if (error) {
      *error = "Not a supported RtCW/ET MDC (expected IDPC v2).";
    }
    return std::nullopt;
  }

  if (!cur.skip(64)) {  // name
    if (error) {
      *error = "MDC header is incomplete.";
    }
    return std::nullopt;
  }

  qint32 flags = 0;
  qint32 num_frames = 0;
  qint32 num_tags = 0;
  qint32 num_surfaces = 0;
  qint32 num_skins = 0;
  qint32 ofs_frames = 0;
  qint32 ofs_tag_names = 0;
  qint32 ofs_tags = 0;
  qint32 ofs_surfaces = 0;
  qint32 ofs_end = 0;
  if (!cur.read_i32(&flags) || !cur.read_i32(&num_frames) || !cur.read_i32(&num_tags) || !cur.read_i32(&num_surfaces) ||
      !cur.read_i32(&num_skins) || !cur.read_i32(&ofs_frames) || !cur.read_i32(&ofs_tag_names) || !cur.read_i32(&ofs_tags) ||
      !cur.read_i32(&ofs_surfaces) || !cur.read_i32(&ofs_end)) {
    if (error) {
      *error = "MDC header is incomplete.";
    }
    return std::nullopt;
  }
  (void)flags;
  (void)num_tags;
  (void)num_skins;
  (void)ofs_frames;
  (void)ofs_tag_names;
  (void)ofs_tags;

  const int file_size = bytes.size();
  if (ofs_end <= 0 || ofs_end > file_size || ofs_surfaces < 0 || ofs_surfaces >= file_size) {
    if (error) {
      *error = "MDC header offsets are invalid.";
    }
    return std::nullopt;
  }
  if (num_frames <= 0 || num_frames > 10000 || num_surfaces < 0 || num_surfaces > 10000) {
    if (error) {
      *error = "MDC header values are invalid.";
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
        *error = "MDC surface header is invalid.";
      }
      return std::nullopt;
    }

    QString surf_name;
    if (!cur.read_fixed_string(64, &surf_name)) {
      if (error) {
        *error = "MDC surface header is incomplete.";
      }
      return std::nullopt;
    }

    qint32 surf_flags = 0;
    qint32 surf_num_comp_frames = 0;
    qint32 surf_num_base_frames = 0;
    qint32 surf_num_shaders = 0;
    qint32 surf_num_verts = 0;
    qint32 surf_num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 ofs_shaders = 0;
    qint32 ofs_st = 0;
    qint32 ofs_xyz = 0;
    qint32 ofs_xyz_compressed = 0;
    qint32 ofs_frame_base = 0;
    qint32 ofs_frame_comp = 0;
    qint32 ofs_surf_end = 0;
    if (!cur.read_i32(&surf_flags) || !cur.read_i32(&surf_num_comp_frames) || !cur.read_i32(&surf_num_base_frames) ||
        !cur.read_i32(&surf_num_shaders) || !cur.read_i32(&surf_num_verts) || !cur.read_i32(&surf_num_tris) ||
        !cur.read_i32(&ofs_tris) || !cur.read_i32(&ofs_shaders) || !cur.read_i32(&ofs_st) || !cur.read_i32(&ofs_xyz) ||
        !cur.read_i32(&ofs_xyz_compressed) || !cur.read_i32(&ofs_frame_base) || !cur.read_i32(&ofs_frame_comp) ||
        !cur.read_i32(&ofs_surf_end)) {
      if (error) {
        *error = "MDC surface header is incomplete.";
      }
      return std::nullopt;
    }
    (void)surf_flags;
    (void)surf_num_comp_frames;
    (void)ofs_xyz_compressed;
    (void)ofs_frame_comp;

    QString shader_name;
    if (surf_num_shaders > 0 && ofs_shaders > 0) {
      const int sh_off = surf_off + ofs_shaders;
      if (cur.seek(sh_off)) {
        (void)cur.read_fixed_string(64, &shader_name);
        qint32 shader_index = 0;
        (void)cur.read_i32(&shader_index);
      }
    }

    if (surf_num_base_frames <= 0 || surf_num_base_frames > 10000 || surf_num_verts <= 0 || surf_num_verts > 200000 ||
        surf_num_tris < 0 || surf_num_tris > 200000 || ofs_surf_end <= 0) {
      if (error) {
        *error = "MDC surface values are invalid.";
      }
      return std::nullopt;
    }

    int frame_base_index = 0;
    if (num_frames > 0 && ofs_frame_base > 0) {
      const int map_off = surf_off + ofs_frame_base;
      if (cur.seek(map_off)) {
        qint16 mapped_base = 0;
        if (cur.read_i16(&mapped_base)) {
          frame_base_index = std::clamp(static_cast<int>(mapped_base), 0, surf_num_base_frames - 1);
        }
      }
    }

    const int base_vertex = vertices.size();
    vertices.resize(base_vertex + surf_num_verts);

    const int xyz_off = surf_off + ofs_xyz + frame_base_index * surf_num_verts * 8;
    if (!cur.seek(xyz_off)) {
      if (error) {
        *error = "MDC surface vertex offset is invalid.";
      }
      return std::nullopt;
    }
    for (int v = 0; v < surf_num_verts; ++v) {
      qint16 x = 0, y = 0, z = 0;
      qint16 n = 0;
      if (!cur.read_i16(&x) || !cur.read_i16(&y) || !cur.read_i16(&z) || !cur.read_i16(&n)) {
        if (error) {
          *error = "MDC vertices are incomplete.";
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

    const int st_off = surf_off + ofs_st;
    if (!cur.seek(st_off)) {
      if (error) {
        *error = "MDC surface texture coordinate offset is invalid.";
      }
      return std::nullopt;
    }
    for (int v = 0; v < surf_num_verts; ++v) {
      float s0 = 0.0f;
      float t0 = 0.0f;
      if (!cur.read_f32(&s0) || !cur.read_f32(&t0)) {
        if (error) {
          *error = "MDC texture coordinates are incomplete.";
        }
        return std::nullopt;
      }
      vertices[base_vertex + v].u = s0;
      vertices[base_vertex + v].v = 1.0f - t0;
    }

    const int tri_off = surf_off + ofs_tris;
    if (!cur.seek(tri_off)) {
      if (error) {
        *error = "MDC surface triangle offset is invalid.";
      }
      return std::nullopt;
    }
    const int first_index = indices.size();
    indices.reserve(indices.size() + surf_num_tris * 3);
    for (int t = 0; t < surf_num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      if (!cur.read_i32(&i0) || !cur.read_i32(&i1) || !cur.read_i32(&i2)) {
        if (error) {
          *error = "MDC triangles are incomplete.";
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
      *error = "MDC contains no drawable geometry.";
    }
    return std::nullopt;
  }

  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "mdc";
  out.frame_count = num_frames;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

[[nodiscard]] bool bytes_range_ok(const QByteArray& bytes, qint64 offset, qint64 length) {
  if (offset < 0 || length < 0) {
    return false;
  }
  const qint64 size = bytes.size();
  if (offset > size) {
    return false;
  }
  return length <= (size - offset);
}

[[nodiscard]] bool read_i16_at(const QByteArray& bytes, int offset, qint16* out) {
  if (!out || !bytes_range_ok(bytes, offset, 2)) {
    return false;
  }
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
  const quint16 u = static_cast<quint16>(p[0] | (p[1] << 8));
  *out = static_cast<qint16>(u);
  return true;
}

[[nodiscard]] bool read_i32_at(const QByteArray& bytes, int offset, qint32* out) {
  if (!out || !bytes_range_ok(bytes, offset, 4)) {
    return false;
  }
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
  const quint32 u = static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
  *out = static_cast<qint32>(u);
  return true;
}

[[nodiscard]] bool read_u32_at(const QByteArray& bytes, int offset, quint32* out) {
  qint32 v = 0;
  if (!out || !read_i32_at(bytes, offset, &v)) {
    return false;
  }
  *out = static_cast<quint32>(v);
  return true;
}

[[nodiscard]] bool read_f32_at(const QByteArray& bytes, int offset, float* out) {
  if (!out) {
    return false;
  }
  quint32 u = 0;
  if (!read_u32_at(bytes, offset, &u)) {
    return false;
  }
  float f = 0.0f;
  static_assert(sizeof(float) == sizeof(quint32));
  memcpy(&f, &u, sizeof(float));
  *out = f;
  return true;
}

[[nodiscard]] QString read_fixed_latin1_string_at(const QByteArray& bytes, int offset, int n) {
  if (n <= 0 || !bytes_range_ok(bytes, offset, n)) {
    return {};
  }
  const QByteArray raw = bytes.mid(offset, n);
  int end = raw.indexOf('\0');
  if (end < 0) {
    end = raw.size();
  }
  return QString::fromLatin1(raw.constData(), end);
}

[[nodiscard]] QString find_case_insensitive_sibling(const QString& candidate_path) {
  if (candidate_path.isEmpty()) {
    return {};
  }
  const QFileInfo want_info(candidate_path);
  const QDir dir(want_info.absolutePath());
  if (!dir.exists()) {
    return {};
  }
  const QString want_name = want_info.fileName().toLower();
  const QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& f : files) {
    if (f.toLower() == want_name) {
      return dir.filePath(f);
    }
  }
  return {};
}

[[nodiscard]] QString companion_path_with_ext(const QString& model_path, const QString& ext) {
  const QFileInfo info(model_path);
  if (!info.exists()) {
    return {};
  }
  const QString candidate = QDir(info.absolutePath()).filePath(info.completeBaseName() + ext);
  if (QFileInfo::exists(candidate)) {
    return candidate;
  }
  return find_case_insensitive_sibling(candidate);
}

[[nodiscard]] QString resolve_glm_gla_path(const QString& glm_path, const QString& anim_name_hint) {
  const QFileInfo model_info(glm_path);
  const QDir model_dir(model_info.absolutePath());
  if (!model_dir.exists()) {
    return {};
  }

  QVector<QString> candidates;
  candidates.reserve(8);

  const auto add_candidate = [&](const QString& p) {
    if (!p.isEmpty()) {
      candidates.push_back(QDir::cleanPath(p));
    }
  };

  add_candidate(model_dir.filePath(model_info.completeBaseName() + ".gla"));

  QString anim = anim_name_hint.trimmed();
  anim.replace('\\', '/');
  while (anim.startsWith('/')) {
    anim.remove(0, 1);
  }
  if (!anim.isEmpty()) {
    if (!anim.endsWith(".gla", Qt::CaseInsensitive)) {
      anim += ".gla";
    }
    add_candidate(model_dir.filePath(anim));
    add_candidate(model_dir.filePath(QFileInfo(anim).fileName()));
  }

  for (const QString& p : candidates) {
    if (QFileInfo::exists(p)) {
      return p;
    }
    const QString ci = find_case_insensitive_sibling(p);
    if (!ci.isEmpty()) {
      return ci;
    }
  }
  return {};
}

constexpr float kShortToAngle = 360.0f / 65536.0f;

[[nodiscard]] float short_to_angle(qint16 value) {
  return static_cast<float>(value) * kShortToAngle;
}

[[nodiscard]] QVector3D local_angle_vector(float pitch_deg, float yaw_deg) {
  const float yaw = yaw_deg * static_cast<float>(M_PI * 2.0 / 360.0);
  const float pitch = pitch_deg * static_cast<float>(M_PI * 2.0 / 360.0);
  const float sy = std::sin(yaw);
  const float cy = std::cos(yaw);
  const float sp = std::sin(pitch);
  const float cp = std::cos(pitch);
  return QVector3D(cp * cy, cp * sy, -sp);
}

struct BoneTransform {
  QVector3D row0 = QVector3D(1.0f, 0.0f, 0.0f);
  QVector3D row1 = QVector3D(0.0f, 1.0f, 0.0f);
  QVector3D row2 = QVector3D(0.0f, 0.0f, 1.0f);
  QVector3D translation = QVector3D(0.0f, 0.0f, 0.0f);
  bool valid = false;
};

void angles_to_axis(float pitch_deg, float yaw_deg, float roll_deg, QVector3D* axis0, QVector3D* axis1, QVector3D* axis2) {
  if (!axis0 || !axis1 || !axis2) {
    return;
  }

  const float yaw = yaw_deg * static_cast<float>(M_PI * 2.0 / 360.0);
  const float pitch = pitch_deg * static_cast<float>(M_PI * 2.0 / 360.0);
  const float roll = roll_deg * static_cast<float>(M_PI * 2.0 / 360.0);

  const float sy = std::sin(yaw);
  const float cy = std::cos(yaw);
  const float sp = std::sin(pitch);
  const float cp = std::cos(pitch);
  const float sr = std::sin(roll);
  const float cr = std::cos(roll);

  QVector3D forward(cp * cy, cp * sy, -sp);
  QVector3D right((-sr * sp * cy) + (-cr * -sy), (-sr * sp * sy) + (-cr * cy), -sr * cp);
  QVector3D up((cr * sp * cy) + (-sr * -sy), (cr * sp * sy) + (-sr * cy), cr * cp);

  *axis0 = forward;
  *axis1 = -right;
  *axis2 = up;
}

[[nodiscard]] QVector3D transform_point(const BoneTransform& bone, const QVector3D& p) {
  return QVector3D(QVector3D::dotProduct(bone.row0, p) + bone.translation.x(),
                   QVector3D::dotProduct(bone.row1, p) + bone.translation.y(),
                   QVector3D::dotProduct(bone.row2, p) + bone.translation.z());
}

[[nodiscard]] QVector3D transform_direction(const BoneTransform& bone, const QVector3D& n) {
  return QVector3D(QVector3D::dotProduct(bone.row0, n), QVector3D::dotProduct(bone.row1, n), QVector3D::dotProduct(bone.row2, n));
}

[[nodiscard]] bool load_mdx_bind_transforms(const QString& mdx_path, QVector<BoneTransform>* out_bones, int* out_num_frames, QString* error) {
  if (error) {
    error->clear();
  }
  if (out_bones) {
    out_bones->clear();
  }
  if (out_num_frames) {
    *out_num_frames = 1;
  }

  QFile f(mdx_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MDX.";
    }
    return false;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 96) {
    if (error) {
      *error = "MDX header is incomplete.";
    }
    return false;
  }

  constexpr quint32 kMdxIdent = 0x5758444Du;  // "MDXW"
  constexpr qint32 kMdxVersion = 2;

  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_frames = 0;
  qint32 num_bones = 0;
  qint32 ofs_frames = 0;
  qint32 ofs_bones = 0;
  qint32 torso_parent = 0;
  qint32 ofs_end = 0;

  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 72, &num_frames) ||
      !read_i32_at(bytes, 76, &num_bones) || !read_i32_at(bytes, 80, &ofs_frames) || !read_i32_at(bytes, 84, &ofs_bones) ||
      !read_i32_at(bytes, 88, &torso_parent) || !read_i32_at(bytes, 92, &ofs_end)) {
    if (error) {
      *error = "MDX header is incomplete.";
    }
    return false;
  }
  (void)torso_parent;

  if (ident != kMdxIdent || version != kMdxVersion) {
    if (error) {
      *error = "Not a supported MDX (expected MDXW v2).";
    }
    return false;
  }

  if (num_frames <= 0 || num_frames > 10000 || num_bones <= 0 || num_bones > 2048) {
    if (error) {
      *error = "MDX header values are invalid.";
    }
    return false;
  }
  if (ofs_end <= 0 || ofs_end > bytes.size()) {
    if (error) {
      *error = "MDX EOF offset is invalid.";
    }
    return false;
  }

  constexpr int kMdxBoneInfoBytes = 80;
  constexpr int kMdxFrameBytes = 52;
  constexpr int kMdxBoneFrameCompressedBytes = 12;

  const qint64 bone_info_bytes = static_cast<qint64>(num_bones) * kMdxBoneInfoBytes;
  if (!bytes_range_ok(bytes, ofs_bones, bone_info_bytes)) {
    if (error) {
      *error = "MDX bone info table is out of bounds.";
    }
    return false;
  }

  const qint64 frame_stride = static_cast<qint64>(kMdxFrameBytes) + static_cast<qint64>(num_bones) * kMdxBoneFrameCompressedBytes;
  if (frame_stride <= 0 || !bytes_range_ok(bytes, ofs_frames, frame_stride)) {
    if (error) {
      *error = "MDX frame data is out of bounds.";
    }
    return false;
  }

  struct BoneInfo {
    int parent = -1;
    float parent_dist = 0.0f;
  };

  QVector<BoneInfo> info;
  info.resize(num_bones);
  for (int i = 0; i < num_bones; ++i) {
    const int bo = ofs_bones + i * kMdxBoneInfoBytes;
    qint32 parent = -1;
    float parent_dist = 0.0f;
    if (!read_i32_at(bytes, bo + 64, &parent) || !read_f32_at(bytes, bo + 72, &parent_dist)) {
      if (error) {
        *error = "MDX bone info is incomplete.";
      }
      return false;
    }
    info[i].parent = parent;
    info[i].parent_dist = parent_dist;
  }

  QVector3D parent_offset(0.0f, 0.0f, 0.0f);
  if (!read_f32_at(bytes, ofs_frames + 40, &parent_offset[0]) || !read_f32_at(bytes, ofs_frames + 44, &parent_offset[1]) ||
      !read_f32_at(bytes, ofs_frames + 48, &parent_offset[2])) {
    if (error) {
      *error = "MDX frame parent offset is incomplete.";
    }
    return false;
  }

  const int bones_off = ofs_frames + kMdxFrameBytes;
  QVector<BoneTransform> bones;
  bones.resize(num_bones);
  QVector<int> state(num_bones, 0);

  std::function<bool(int)> build_bone = [&](int bone_index) -> bool {
    if (bone_index < 0 || bone_index >= num_bones) {
      return false;
    }
    if (state[bone_index] == 2) {
      return bones[bone_index].valid;
    }
    if (state[bone_index] == 1) {
      return false;
    }
    state[bone_index] = 1;

    const int co = bones_off + bone_index * kMdxBoneFrameCompressedBytes;
    qint16 a0 = 0, a1 = 0, a2 = 0;
    qint16 ofs_pitch = 0, ofs_yaw = 0;
    if (!read_i16_at(bytes, co + 0, &a0) || !read_i16_at(bytes, co + 2, &a1) || !read_i16_at(bytes, co + 4, &a2) ||
        !read_i16_at(bytes, co + 8, &ofs_pitch) || !read_i16_at(bytes, co + 10, &ofs_yaw)) {
      state[bone_index] = 2;
      bones[bone_index].valid = false;
      return false;
    }

    BoneTransform b;
    angles_to_axis(short_to_angle(a0), short_to_angle(a1), short_to_angle(a2), &b.row0, &b.row1, &b.row2);

    const int parent = info[bone_index].parent;
    if (parent >= 0 && parent < num_bones && build_bone(parent) && bones[parent].valid) {
      const QVector3D dir = local_angle_vector(short_to_angle(ofs_pitch), short_to_angle(ofs_yaw));
      b.translation = bones[parent].translation + dir * info[bone_index].parent_dist;
    } else {
      b.translation = parent_offset;
    }

    b.valid = true;
    bones[bone_index] = b;
    state[bone_index] = 2;
    return true;
  };

  for (int i = 0; i < num_bones; ++i) {
    (void)build_bone(i);
  }

  if (out_bones) {
    *out_bones = std::move(bones);
  }
  if (out_num_frames) {
    *out_num_frames = num_frames;
  }
  return true;
}

[[nodiscard]] bool load_mdxa_base_transforms(const QString& mdxa_path, QVector<BoneTransform>* out_bones, QString* error) {
  if (error) {
    error->clear();
  }
  if (out_bones) {
    out_bones->clear();
  }

  QFile f(mdxa_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open GLA.";
    }
    return false;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 100) {
    if (error) {
      *error = "GLA header is incomplete.";
    }
    return false;
  }

  constexpr quint32 kMdxaIdent = 0x41474C32u;  // "2LGA"
  constexpr qint32 kMdxaVersion = 6;

  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_bones = 0;
  qint32 ofs_skel = 0;
  qint32 ofs_end = 0;

  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 84, &num_bones) ||
      !read_i32_at(bytes, 92, &ofs_skel) || !read_i32_at(bytes, 96, &ofs_end)) {
    if (error) {
      *error = "GLA header is incomplete.";
    }
    return false;
  }

  if (ident != kMdxaIdent || version != kMdxaVersion) {
    if (error) {
      *error = "Not a supported GLA (expected 2LGA v6).";
    }
    return false;
  }
  if (num_bones <= 0 || num_bones > 2048) {
    if (error) {
      *error = "GLA bone count is invalid.";
    }
    return false;
  }
  if (ofs_end <= 0 || ofs_end > bytes.size()) {
    if (error) {
      *error = "GLA EOF offset is invalid.";
    }
    return false;
  }

  int offsets_base = ofs_skel;
  if (!bytes_range_ok(bytes, offsets_base, static_cast<qint64>(num_bones) * 4)) {
    offsets_base = 100;  // OpenJK readers historically locate the skeleton offset table right after header.
  }
  if (!bytes_range_ok(bytes, offsets_base, static_cast<qint64>(num_bones) * 4)) {
    if (error) {
      *error = "GLA skeleton table is out of bounds.";
    }
    return false;
  }

  QVector<BoneTransform> bones;
  bones.resize(num_bones);

  constexpr int kSkelFixedBytes = 172;  // mdxaSkel_t up to numChildren
  for (int i = 0; i < num_bones; ++i) {
    qint32 rel = 0;
    if (!read_i32_at(bytes, offsets_base + i * 4, &rel)) {
      continue;
    }
    const int bone_off = offsets_base + rel;
    if (!bytes_range_ok(bytes, bone_off, kSkelFixedBytes)) {
      continue;
    }

    BoneTransform b;
    float m00 = 0.0f, m01 = 0.0f, m02 = 0.0f, tx = 0.0f;
    float m10 = 0.0f, m11 = 0.0f, m12 = 0.0f, ty = 0.0f;
    float m20 = 0.0f, m21 = 0.0f, m22 = 0.0f, tz = 0.0f;
    if (!read_f32_at(bytes, bone_off + 72, &m00) || !read_f32_at(bytes, bone_off + 76, &m01) || !read_f32_at(bytes, bone_off + 80, &m02) ||
        !read_f32_at(bytes, bone_off + 84, &tx) || !read_f32_at(bytes, bone_off + 88, &m10) || !read_f32_at(bytes, bone_off + 92, &m11) ||
        !read_f32_at(bytes, bone_off + 96, &m12) || !read_f32_at(bytes, bone_off + 100, &ty) || !read_f32_at(bytes, bone_off + 104, &m20) ||
        !read_f32_at(bytes, bone_off + 108, &m21) || !read_f32_at(bytes, bone_off + 112, &m22) || !read_f32_at(bytes, bone_off + 116, &tz)) {
      continue;
    }

    b.row0 = QVector3D(m00, m01, m02);
    b.row1 = QVector3D(m10, m11, m12);
    b.row2 = QVector3D(m20, m21, m22);
    b.translation = QVector3D(tx, ty, tz);
    b.valid = true;
    bones[i] = b;
  }

  if (out_bones) {
    *out_bones = std::move(bones);
  }
  return true;
}

std::optional<LoadedModel> load_md4(const QString& file_path, QString* error) {
  if (error) {
    *error = {};
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MD4.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 96) {
    if (error) {
      *error = "MD4 header is incomplete.";
    }
    return std::nullopt;
  }

  constexpr quint32 kMd4Ident = 0x34504449u;  // "IDP4"
  constexpr qint32 kMd4Version = 1;
  constexpr int kMd4FrameFixedBytes = 56;      // md4Frame_t before bones[]
  constexpr int kMd4BoneMatrixBytes = 48;      // float matrix[3][4]
  constexpr int kMd4SurfaceHeaderBytes = 168;  // md4Surface_t fixed fields

  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_frames = 0;
  qint32 num_bones = 0;
  qint32 ofs_frames = 0;
  qint32 num_lods = 0;
  qint32 ofs_lods = 0;
  qint32 ofs_end = 0;
  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 72, &num_frames) ||
      !read_i32_at(bytes, 76, &num_bones) || !read_i32_at(bytes, 80, &ofs_frames) || !read_i32_at(bytes, 84, &num_lods) ||
      !read_i32_at(bytes, 88, &ofs_lods) || !read_i32_at(bytes, 92, &ofs_end)) {
    if (error) {
      *error = "MD4 header is incomplete.";
    }
    return std::nullopt;
  }

  if (ident != kMd4Ident || version != kMd4Version) {
    if (error) {
      *error = "Not a supported MD4 (expected IDP4 v1).";
    }
    return std::nullopt;
  }
  if (num_frames <= 0 || num_frames > 100000 || num_bones <= 0 || num_bones > 8192 || num_lods <= 0 || num_lods > 256) {
    if (error) {
      *error = "MD4 header values are invalid.";
    }
    return std::nullopt;
  }
  if (ofs_frames < 0 || ofs_lods < 0 || ofs_end <= 0 || ofs_end > bytes.size()) {
    if (error) {
      *error = "MD4 offsets are invalid.";
    }
    return std::nullopt;
  }

  const qint64 frame0_bytes = static_cast<qint64>(kMd4FrameFixedBytes) + static_cast<qint64>(num_bones) * kMd4BoneMatrixBytes;
  if (!bytes_range_ok(bytes, ofs_frames, frame0_bytes)) {
    if (error) {
      *error = "MD4 frame table is out of bounds.";
    }
    return std::nullopt;
  }

  struct Md4BoneMatrix {
    float m[3][4]{};
    bool valid = false;
  };

  auto transform_point_md4 = [](const Md4BoneMatrix& b, const QVector3D& p) -> QVector3D {
    return QVector3D(b.m[0][0] * p.x() + b.m[0][1] * p.y() + b.m[0][2] * p.z() + b.m[0][3],
                     b.m[1][0] * p.x() + b.m[1][1] * p.y() + b.m[1][2] * p.z() + b.m[1][3],
                     b.m[2][0] * p.x() + b.m[2][1] * p.y() + b.m[2][2] * p.z() + b.m[2][3]);
  };
  auto transform_dir_md4 = [](const Md4BoneMatrix& b, const QVector3D& d) -> QVector3D {
    return QVector3D(b.m[0][0] * d.x() + b.m[0][1] * d.y() + b.m[0][2] * d.z(),
                     b.m[1][0] * d.x() + b.m[1][1] * d.y() + b.m[1][2] * d.z(),
                     b.m[2][0] * d.x() + b.m[2][1] * d.y() + b.m[2][2] * d.z());
  };

  QVector<Md4BoneMatrix> bones;
  bones.resize(num_bones);
  const int frame0_bones_off = ofs_frames + kMd4FrameFixedBytes;
  for (int i = 0; i < num_bones; ++i) {
    const int bo = frame0_bones_off + i * kMd4BoneMatrixBytes;
    if (!bytes_range_ok(bytes, bo, kMd4BoneMatrixBytes)) {
      continue;
    }
    Md4BoneMatrix bone;
    bool ok = true;
    for (int r = 0; r < 3 && ok; ++r) {
      for (int c = 0; c < 4; ++c) {
        float v = 0.0f;
        if (!read_f32_at(bytes, bo + (r * 16) + (c * 4), &v)) {
          ok = false;
          break;
        }
        bone.m[r][c] = v;
      }
    }
    bone.valid = ok;
    bones[i] = bone;
  }

  qint32 lod_num_surfaces = 0;
  qint32 lod_ofs_surfaces = 0;
  qint32 lod_ofs_end = 0;
  if (!read_i32_at(bytes, ofs_lods + 0, &lod_num_surfaces) || !read_i32_at(bytes, ofs_lods + 4, &lod_ofs_surfaces) ||
      !read_i32_at(bytes, ofs_lods + 8, &lod_ofs_end)) {
    if (error) {
      *error = "MD4 LOD header is incomplete.";
    }
    return std::nullopt;
  }
  if (lod_num_surfaces <= 0 || lod_num_surfaces > 32768 || lod_ofs_surfaces <= 0 || lod_ofs_end <= 0) {
    if (error) {
      *error = "MD4 LOD values are invalid.";
    }
    return std::nullopt;
  }

  const int lod_base = ofs_lods;
  int lod_end = lod_base + lod_ofs_end;
  int surf_off = lod_base + lod_ofs_surfaces;
  bool lod_layout_ok = bytes_range_ok(bytes, lod_base, lod_ofs_end) && lod_end > lod_base && lod_end <= ofs_end && surf_off >= lod_base &&
                       surf_off < lod_end;

  // Some tools emit absolute offsets in md4LOD_t; accept that variant as well.
  if (!lod_layout_ok) {
    const int abs_lod_end = lod_ofs_end;
    const int abs_surf_off = lod_ofs_surfaces;
    if (abs_lod_end > lod_base && abs_lod_end <= ofs_end && abs_lod_end <= bytes.size() && abs_surf_off >= lod_base &&
        abs_surf_off < abs_lod_end) {
      lod_end = abs_lod_end;
      surf_off = abs_surf_off;
      lod_layout_ok = true;
    }
  }

  if (!lod_layout_ok) {
    if (error) {
      *error = "MD4 LOD offsets are out of bounds.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(lod_num_surfaces);

  for (int s = 0; s < lod_num_surfaces; ++s) {
    if (!bytes_range_ok(bytes, surf_off, kMd4SurfaceHeaderBytes)) {
      if (error) {
        *error = "MD4 surface header is out of bounds.";
      }
      return std::nullopt;
    }

    quint32 surf_ident = 0;
    qint32 num_verts = 0;
    qint32 ofs_verts = 0;
    qint32 num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 num_bone_refs = 0;
    qint32 ofs_bone_refs = 0;
    qint32 ofs_surf_end = 0;
    if (!read_u32_at(bytes, surf_off + 0, &surf_ident) || !read_i32_at(bytes, surf_off + 140, &num_verts) ||
        !read_i32_at(bytes, surf_off + 144, &ofs_verts) || !read_i32_at(bytes, surf_off + 148, &num_tris) ||
        !read_i32_at(bytes, surf_off + 152, &ofs_tris) || !read_i32_at(bytes, surf_off + 156, &num_bone_refs) ||
        !read_i32_at(bytes, surf_off + 160, &ofs_bone_refs) || !read_i32_at(bytes, surf_off + 164, &ofs_surf_end)) {
      if (error) {
        *error = "MD4 surface header is incomplete.";
      }
      return std::nullopt;
    }
    if (surf_ident != kMd4Ident) {
      if (error) {
        *error = "MD4 surface identifier is invalid.";
      }
      return std::nullopt;
    }
    if (num_verts <= 0 || num_verts > 2'000'000 || num_tris < 0 || num_tris > 4'000'000 || num_bone_refs < 0 ||
        num_bone_refs > 8192 || ofs_surf_end <= kMd4SurfaceHeaderBytes) {
      if (error) {
        *error = "MD4 surface values are invalid.";
      }
      return std::nullopt;
    }

    const int surf_end = surf_off + ofs_surf_end;
    if (surf_end <= surf_off || surf_end > lod_end || surf_end > ofs_end || surf_end > bytes.size()) {
      if (error) {
        *error = "MD4 surface exceeds file bounds.";
      }
      return std::nullopt;
    }

    QVector<int> bone_refs;
    bone_refs.resize(num_bone_refs);
    const int bone_ref_off = surf_off + ofs_bone_refs;
    if (!bytes_range_ok(bytes, bone_ref_off, static_cast<qint64>(num_bone_refs) * 4) || bone_ref_off < surf_off ||
        bone_ref_off > surf_end) {
      if (error) {
        *error = "MD4 surface bone references are out of bounds.";
      }
      return std::nullopt;
    }
    for (int i = 0; i < num_bone_refs; ++i) {
      qint32 ref = -1;
      (void)read_i32_at(bytes, bone_ref_off + i * 4, &ref);
      bone_refs[i] = ref;
    }

    const int base_vertex = vertices.size();
    qint64 vptr = static_cast<qint64>(surf_off) + ofs_verts;
    if (vptr < surf_off || vptr >= surf_end) {
      if (error) {
        *error = "MD4 surface vertex offset is invalid.";
      }
      return std::nullopt;
    }

    for (int v = 0; v < num_verts; ++v) {
      if (vptr < surf_off || vptr + 24 > surf_end) {
        if (error) {
          *error = "MD4 vertex data is incomplete.";
        }
        return std::nullopt;
      }

      float nx = 0.0f, ny = 0.0f, nz = 1.0f;
      float tu = 0.0f, tv = 0.0f;
      qint32 num_weights = 0;
      (void)read_f32_at(bytes, static_cast<int>(vptr + 0), &nx);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 4), &ny);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 8), &nz);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 12), &tu);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 16), &tv);
      (void)read_i32_at(bytes, static_cast<int>(vptr + 20), &num_weights);

      if (num_weights < 0 || num_weights > 128) {
        if (error) {
          *error = "MD4 vertex has invalid weight count.";
        }
        return std::nullopt;
      }

      const qint64 weight_bytes = static_cast<qint64>(num_weights) * 20;
      if (vptr + 24 + weight_bytes > surf_end) {
        if (error) {
          *error = "MD4 vertex weights are incomplete.";
        }
        return std::nullopt;
      }

      const QVector3D src_normal(nx, ny, nz);
      QVector3D p(0.0f, 0.0f, 0.0f);
      QVector3D n(0.0f, 0.0f, 0.0f);
      float total_w = 0.0f;

      for (int w = 0; w < num_weights; ++w) {
        const int wo = static_cast<int>(vptr + 24 + static_cast<qint64>(w) * 20);
        qint32 local_bone = -1;
        float bw = 0.0f;
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        (void)read_i32_at(bytes, wo + 0, &local_bone);
        (void)read_f32_at(bytes, wo + 4, &bw);
        (void)read_f32_at(bytes, wo + 8, &ox);
        (void)read_f32_at(bytes, wo + 12, &oy);
        (void)read_f32_at(bytes, wo + 16, &oz);
        if (bw <= 0.0f || local_bone < 0 || local_bone >= bone_refs.size()) {
          continue;
        }

        const int global_bone = bone_refs[local_bone];
        const QVector3D offset(ox, oy, oz);
        if (global_bone >= 0 && global_bone < bones.size() && bones[global_bone].valid) {
          p += transform_point_md4(bones[global_bone], offset) * bw;
          n += transform_dir_md4(bones[global_bone], src_normal) * bw;
        } else {
          p += offset * bw;
          n += src_normal * bw;
        }
        total_w += bw;
      }

      if (total_w <= 0.0f) {
        if (num_weights > 0) {
          float ox = 0.0f, oy = 0.0f, oz = 0.0f;
          const int wo0 = static_cast<int>(vptr + 24);
          (void)read_f32_at(bytes, wo0 + 8, &ox);
          (void)read_f32_at(bytes, wo0 + 12, &oy);
          (void)read_f32_at(bytes, wo0 + 16, &oz);
          p = QVector3D(ox, oy, oz);
        }
        n = src_normal;
      }

      if (n.lengthSquared() < 1e-12f) {
        n = QVector3D(0.0f, 0.0f, 1.0f);
      } else {
        n.normalize();
      }

      ModelVertex mv;
      mv.px = p.x();
      mv.py = p.y();
      mv.pz = p.z();
      mv.nx = n.x();
      mv.ny = n.y();
      mv.nz = n.z();
      mv.u = tu;
      mv.v = 1.0f - tv;
      vertices.push_back(mv);

      vptr += 24 + weight_bytes;
    }

    const int tri_off = surf_off + ofs_tris;
    if (!bytes_range_ok(bytes, tri_off, static_cast<qint64>(num_tris) * 12) || tri_off < surf_off || tri_off > surf_end) {
      if (error) {
        *error = "MD4 triangles are out of bounds.";
      }
      return std::nullopt;
    }

    const int first_index = indices.size();
    for (int t = 0; t < num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      const int to = tri_off + t * 12;
      (void)read_i32_at(bytes, to + 0, &i0);
      (void)read_i32_at(bytes, to + 4, &i1);
      (void)read_i32_at(bytes, to + 8, &i2);
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i0));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i1));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i2));
    }

    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface ms;
      ms.name = read_fixed_latin1_string_at(bytes, surf_off + 4, 64);
      ms.shader = read_fixed_latin1_string_at(bytes, surf_off + 68, 64);
      ms.first_index = first_index;
      ms.index_count = index_count;
      surfaces.push_back(std::move(ms));
    }

    surf_off = surf_end;
    if (surf_off >= lod_end) {
      break;
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "MD4 contains no drawable geometry.";
    }
    return std::nullopt;
  }
  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "md4";
  out.frame_count = num_frames;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_mdr(const QString& file_path, QString* error) {
  if (error) {
    *error = {};
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MDR.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 104) {
    if (error) {
      *error = "MDR header is incomplete.";
    }
    return std::nullopt;
  }

  constexpr quint32 kMdrIdent = 0x354D4452u;  // "RDM5"
  constexpr qint32 kMdrVersion = 2;
  constexpr int kMdrHeaderSize = 104;
  constexpr int kMdrSurfaceHeaderSize = 168;
  constexpr int kMdrFrameFixedBytes = 56;
  constexpr int kMdrBoneMatrixBytes = 48;

  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_frames = 0;
  qint32 num_bones = 0;
  qint32 ofs_frames = 0;
  qint32 num_lods = 0;
  qint32 ofs_lods = 0;
  qint32 num_tags = 0;
  qint32 ofs_tags = 0;
  qint32 ofs_end = 0;
  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 72, &num_frames) ||
      !read_i32_at(bytes, 76, &num_bones) || !read_i32_at(bytes, 80, &ofs_frames) || !read_i32_at(bytes, 84, &num_lods) ||
      !read_i32_at(bytes, 88, &ofs_lods) || !read_i32_at(bytes, 92, &num_tags) || !read_i32_at(bytes, 96, &ofs_tags) ||
      !read_i32_at(bytes, 100, &ofs_end)) {
    if (error) {
      *error = "MDR header is incomplete.";
    }
    return std::nullopt;
  }
  (void)num_tags;
  (void)ofs_tags;

  if (ident != kMdrIdent || version != kMdrVersion) {
    if (error) {
      *error = "Not a supported MDR (expected RDM5 v2).";
    }
    return std::nullopt;
  }
  if (num_frames <= 0 || num_frames > 100000 || num_bones <= 0 || num_bones > 8192 || num_lods <= 0 || num_lods > 256) {
    if (error) {
      *error = "MDR header values are invalid.";
    }
    return std::nullopt;
  }
  if (ofs_lods < kMdrHeaderSize || ofs_lods >= bytes.size() || ofs_end <= 0 || ofs_end > bytes.size()) {
    if (error) {
      *error = "MDR offsets are invalid.";
    }
    return std::nullopt;
  }

  struct BoneMatrix {
    float m[3][4]{};
    bool valid = false;
  };
  auto transform_point = [](const BoneMatrix& b, const QVector3D& p) -> QVector3D {
    return QVector3D(b.m[0][0] * p.x() + b.m[0][1] * p.y() + b.m[0][2] * p.z() + b.m[0][3],
                     b.m[1][0] * p.x() + b.m[1][1] * p.y() + b.m[1][2] * p.z() + b.m[1][3],
                     b.m[2][0] * p.x() + b.m[2][1] * p.y() + b.m[2][2] * p.z() + b.m[2][3]);
  };
  auto transform_dir = [](const BoneMatrix& b, const QVector3D& d) -> QVector3D {
    return QVector3D(b.m[0][0] * d.x() + b.m[0][1] * d.y() + b.m[0][2] * d.z(),
                     b.m[1][0] * d.x() + b.m[1][1] * d.y() + b.m[1][2] * d.z(),
                     b.m[2][0] * d.x() + b.m[2][1] * d.y() + b.m[2][2] * d.z());
  };

  QVector<BoneMatrix> bones;
  bones.resize(num_bones);
  bool has_frame_matrices = false;
  if (ofs_frames > 0) {
    const qint64 frame0_bytes = static_cast<qint64>(kMdrFrameFixedBytes) + static_cast<qint64>(num_bones) * kMdrBoneMatrixBytes;
    if (bytes_range_ok(bytes, ofs_frames, frame0_bytes)) {
      const int frame0_bones_off = ofs_frames + kMdrFrameFixedBytes;
      for (int i = 0; i < num_bones; ++i) {
        const int bo = frame0_bones_off + i * kMdrBoneMatrixBytes;
        if (!bytes_range_ok(bytes, bo, kMdrBoneMatrixBytes)) {
          continue;
        }
        BoneMatrix bone;
        bool ok = true;
        for (int r = 0; r < 3 && ok; ++r) {
          for (int c = 0; c < 4; ++c) {
            float v = 0.0f;
            if (!read_f32_at(bytes, bo + (r * 16) + (c * 4), &v)) {
              ok = false;
              break;
            }
            bone.m[r][c] = v;
          }
        }
        bone.valid = ok;
        has_frame_matrices = has_frame_matrices || ok;
        bones[i] = bone;
      }
    }
  }

  qint32 lod_num_surfaces = 0;
  qint32 lod_ofs_surfaces = 0;
  qint32 lod_ofs_end = 0;
  if (!read_i32_at(bytes, ofs_lods + 0, &lod_num_surfaces) || !read_i32_at(bytes, ofs_lods + 4, &lod_ofs_surfaces) ||
      !read_i32_at(bytes, ofs_lods + 8, &lod_ofs_end)) {
    if (error) {
      *error = "MDR LOD header is incomplete.";
    }
    return std::nullopt;
  }
  if (lod_num_surfaces <= 0 || lod_num_surfaces > 32768 || lod_ofs_surfaces <= 0 || lod_ofs_end <= 0) {
    if (error) {
      *error = "MDR LOD values are invalid.";
    }
    return std::nullopt;
  }

  const int lod_base = ofs_lods;
  int lod_end = lod_base + lod_ofs_end;
  int surf_off = lod_base + lod_ofs_surfaces;
  bool lod_layout_ok = bytes_range_ok(bytes, lod_base, lod_ofs_end) && lod_end > lod_base && lod_end <= ofs_end && surf_off >= lod_base &&
                       surf_off < lod_end;

  // Accept absolute offsets as a best-effort fallback for non-standard writers.
  if (!lod_layout_ok) {
    const int abs_lod_end = lod_ofs_end;
    const int abs_surf_off = lod_ofs_surfaces;
    if (abs_lod_end > lod_base && abs_lod_end <= ofs_end && abs_lod_end <= bytes.size() && abs_surf_off >= lod_base &&
        abs_surf_off < abs_lod_end) {
      lod_end = abs_lod_end;
      surf_off = abs_surf_off;
      lod_layout_ok = true;
    }
  }

  if (!lod_layout_ok) {
    if (error) {
      *error = "MDR LOD offsets are out of bounds.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(lod_num_surfaces);

  for (int s = 0; s < lod_num_surfaces; ++s) {
    if (!bytes_range_ok(bytes, surf_off, kMdrSurfaceHeaderSize)) {
      if (error) {
        *error = "MDR surface header is out of bounds.";
      }
      return std::nullopt;
    }

    qint32 num_verts = 0;
    qint32 ofs_verts = 0;
    qint32 num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 num_bone_refs = 0;
    qint32 ofs_bone_refs = 0;
    qint32 ofs_surf_end = 0;
    if (!read_i32_at(bytes, surf_off + 140, &num_verts) || !read_i32_at(bytes, surf_off + 144, &ofs_verts) ||
        !read_i32_at(bytes, surf_off + 148, &num_tris) || !read_i32_at(bytes, surf_off + 152, &ofs_tris) ||
        !read_i32_at(bytes, surf_off + 156, &num_bone_refs) || !read_i32_at(bytes, surf_off + 160, &ofs_bone_refs) ||
        !read_i32_at(bytes, surf_off + 164, &ofs_surf_end)) {
      if (error) {
        *error = "MDR surface header is incomplete.";
      }
      return std::nullopt;
    }
    if (num_verts <= 0 || num_verts > 2'000'000 || num_tris < 0 || num_tris > 4'000'000 || num_bone_refs < 0 ||
        num_bone_refs > 8192 || ofs_surf_end <= kMdrSurfaceHeaderSize) {
      if (error) {
        *error = "MDR surface values are invalid.";
      }
      return std::nullopt;
    }

    const int surf_end = surf_off + ofs_surf_end;
    if (surf_end <= surf_off || surf_end > lod_end || surf_end > ofs_end || surf_end > bytes.size()) {
      if (error) {
        *error = "MDR surface exceeds file bounds.";
      }
      return std::nullopt;
    }

    QVector<int> bone_refs;
    bone_refs.resize(num_bone_refs);
    const int bone_ref_off = surf_off + ofs_bone_refs;
    if (!bytes_range_ok(bytes, bone_ref_off, static_cast<qint64>(num_bone_refs) * 4) || bone_ref_off < surf_off ||
        bone_ref_off > surf_end) {
      if (error) {
        *error = "MDR surface bone references are out of bounds.";
      }
      return std::nullopt;
    }
    for (int i = 0; i < num_bone_refs; ++i) {
      qint32 ref = -1;
      (void)read_i32_at(bytes, bone_ref_off + i * 4, &ref);
      bone_refs[i] = ref;
    }

    const int base_vertex = vertices.size();
    qint64 vptr = static_cast<qint64>(surf_off) + ofs_verts;
    if (vptr < surf_off || vptr >= surf_end) {
      if (error) {
        *error = "MDR surface vertex offset is invalid.";
      }
      return std::nullopt;
    }

    for (int v = 0; v < num_verts; ++v) {
      if (vptr < surf_off || vptr + 24 > surf_end) {
        if (error) {
          *error = "MDR vertex data is incomplete.";
        }
        return std::nullopt;
      }

      float nx = 0.0f, ny = 0.0f, nz = 1.0f;
      float tu = 0.0f, tv = 0.0f;
      qint32 num_weights = 0;
      (void)read_f32_at(bytes, static_cast<int>(vptr + 0), &nx);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 4), &ny);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 8), &nz);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 12), &tu);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 16), &tv);
      (void)read_i32_at(bytes, static_cast<int>(vptr + 20), &num_weights);

      if (num_weights < 0 || num_weights > 128) {
        if (error) {
          *error = "MDR vertex has invalid weight count.";
        }
        return std::nullopt;
      }

      const qint64 weight_bytes = static_cast<qint64>(num_weights) * 20;
      if (vptr + 24 + weight_bytes > surf_end) {
        if (error) {
          *error = "MDR vertex weights are incomplete.";
        }
        return std::nullopt;
      }

      const QVector3D src_normal(nx, ny, nz);
      QVector3D p(0.0f, 0.0f, 0.0f);
      QVector3D n(0.0f, 0.0f, 0.0f);
      float total_w = 0.0f;

      for (int w = 0; w < num_weights; ++w) {
        const int wo = static_cast<int>(vptr + 24 + static_cast<qint64>(w) * 20);
        qint32 local_bone = -1;
        float bw = 0.0f;
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        (void)read_i32_at(bytes, wo + 0, &local_bone);
        (void)read_f32_at(bytes, wo + 4, &bw);
        (void)read_f32_at(bytes, wo + 8, &ox);
        (void)read_f32_at(bytes, wo + 12, &oy);
        (void)read_f32_at(bytes, wo + 16, &oz);
        if (bw <= 0.0f || local_bone < 0) {
          continue;
        }

        int global_bone = -1;
        if (local_bone < bone_refs.size()) {
          global_bone = bone_refs[local_bone];
        } else {
          global_bone = local_bone;
        }

        const QVector3D offset(ox, oy, oz);
        if (has_frame_matrices && global_bone >= 0 && global_bone < bones.size() && bones[global_bone].valid) {
          p += transform_point(bones[global_bone], offset) * bw;
          n += transform_dir(bones[global_bone], src_normal) * bw;
        } else {
          p += offset * bw;
          n += src_normal * bw;
        }
        total_w += bw;
      }

      if (total_w <= 0.0f) {
        if (num_weights > 0) {
          float ox = 0.0f, oy = 0.0f, oz = 0.0f;
          const int wo0 = static_cast<int>(vptr + 24);
          (void)read_f32_at(bytes, wo0 + 8, &ox);
          (void)read_f32_at(bytes, wo0 + 12, &oy);
          (void)read_f32_at(bytes, wo0 + 16, &oz);
          p = QVector3D(ox, oy, oz);
        }
        n = src_normal;
      }

      if (n.lengthSquared() < 1e-12f) {
        n = QVector3D(0.0f, 0.0f, 1.0f);
      } else {
        n.normalize();
      }

      ModelVertex mv;
      mv.px = p.x();
      mv.py = p.y();
      mv.pz = p.z();
      mv.nx = n.x();
      mv.ny = n.y();
      mv.nz = n.z();
      mv.u = tu;
      mv.v = 1.0f - tv;
      vertices.push_back(mv);

      vptr += 24 + weight_bytes;
    }

    const int tri_off = surf_off + ofs_tris;
    if (!bytes_range_ok(bytes, tri_off, static_cast<qint64>(num_tris) * 12) || tri_off < surf_off || tri_off > surf_end) {
      if (error) {
        *error = "MDR triangles are out of bounds.";
      }
      return std::nullopt;
    }

    const int first_index = indices.size();
    for (int t = 0; t < num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      const int to = tri_off + t * 12;
      (void)read_i32_at(bytes, to + 0, &i0);
      (void)read_i32_at(bytes, to + 4, &i1);
      (void)read_i32_at(bytes, to + 8, &i2);
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i0));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i1));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i2));
    }

    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface ms;
      ms.name = read_fixed_latin1_string_at(bytes, surf_off + 4, 64);
      ms.shader = read_fixed_latin1_string_at(bytes, surf_off + 68, 64);
      ms.first_index = first_index;
      ms.index_count = index_count;
      surfaces.push_back(std::move(ms));
    }

    surf_off = surf_end;
    if (surf_off >= lod_end) {
      break;
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "MDR contains no drawable geometry.";
    }
    return std::nullopt;
  }
  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "mdr";
  out.frame_count = num_frames;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_skel_mesh_file(const QString& file_path,
                                               quint32 expected_ident,
                                               qint32 expected_version_a,
                                               qint32 expected_version_b,
                                               const QString& format_name,
                                               QString* error) {
  if (error) {
    *error = {};
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QString("Unable to open %1.").arg(format_name);
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 148) {
    if (error) {
      *error = QString("%1 header is incomplete.").arg(format_name);
    }
    return std::nullopt;
  }

  constexpr int kSkelSurfaceHeaderSize = 100;
  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_surfaces = 0;
  qint32 num_bones = 0;
  qint32 ofs_surfaces = 0;
  qint32 ofs_end = 0;
  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 72, &num_surfaces) ||
      !read_i32_at(bytes, 76, &num_bones) || !read_i32_at(bytes, 84, &ofs_surfaces) || !read_i32_at(bytes, 88, &ofs_end)) {
    if (error) {
      *error = QString("%1 header is incomplete.").arg(format_name);
    }
    return std::nullopt;
  }
  if (ident != expected_ident || (version != expected_version_a && version != expected_version_b)) {
    if (error) {
      *error = QString("Not a supported %1 file.").arg(format_name);
    }
    return std::nullopt;
  }
  if (num_surfaces <= 0 || num_surfaces > 32768 || num_bones < 0 || num_bones > 262144) {
    if (error) {
      *error = QString("%1 header values are invalid.").arg(format_name);
    }
    return std::nullopt;
  }
  if (ofs_surfaces <= 0 || ofs_surfaces >= bytes.size() || ofs_end <= 0 || ofs_end > bytes.size()) {
    if (error) {
      *error = QString("%1 offsets are invalid.").arg(format_name);
    }
    return std::nullopt;
  }

  float mesh_scale = 0.52f;
  if (version >= 6) {
    float scale = 1.0f;
    if (read_f32_at(bytes, 148, &scale) && std::isfinite(scale) && scale > 0.0f) {
      mesh_scale = scale * 0.52f;
    }
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(num_surfaces);

  int surf_off = ofs_surfaces;
  for (int s = 0; s < num_surfaces; ++s) {
    if (!bytes_range_ok(bytes, surf_off, kSkelSurfaceHeaderSize)) {
      if (error) {
        *error = QString("%1 surface header is out of bounds.").arg(format_name);
      }
      return std::nullopt;
    }

    qint32 num_tris = 0;
    qint32 num_verts = 0;
    qint32 ofs_tris = 0;
    qint32 ofs_verts = 0;
    qint32 ofs_surf_end = 0;
    if (!read_i32_at(bytes, surf_off + 68, &num_tris) || !read_i32_at(bytes, surf_off + 72, &num_verts) ||
        !read_i32_at(bytes, surf_off + 80, &ofs_tris) || !read_i32_at(bytes, surf_off + 84, &ofs_verts) ||
        !read_i32_at(bytes, surf_off + 92, &ofs_surf_end)) {
      if (error) {
        *error = QString("%1 surface header is incomplete.").arg(format_name);
      }
      return std::nullopt;
    }
    if (num_verts <= 0 || num_verts > 10000000 || num_tris < 0 || num_tris > 10000000 || ofs_surf_end <= kSkelSurfaceHeaderSize) {
      if (error) {
        *error = QString("%1 surface values are invalid.").arg(format_name);
      }
      return std::nullopt;
    }

    const int surf_end = surf_off + ofs_surf_end;
    if (surf_end <= surf_off || surf_end > ofs_end || surf_end > bytes.size()) {
      if (error) {
        *error = QString("%1 surface exceeds file bounds.").arg(format_name);
      }
      return std::nullopt;
    }

    const int base_vertex = vertices.size();
    qint64 vptr = static_cast<qint64>(surf_off) + ofs_verts;
    if (vptr < surf_off || vptr >= surf_end) {
      if (error) {
        *error = QString("%1 surface vertex offset is invalid.").arg(format_name);
      }
      return std::nullopt;
    }

    const bool has_morphs = (version >= 5);
    for (int v = 0; v < num_verts; ++v) {
      const int vertex_base_bytes = has_morphs ? 28 : 24;
      if (vptr < surf_off || vptr + vertex_base_bytes > surf_end) {
        if (error) {
          *error = QString("%1 vertex data is incomplete.").arg(format_name);
        }
        return std::nullopt;
      }

      float nx = 0.0f, ny = 0.0f, nz = 1.0f;
      float tu = 0.0f, tv = 0.0f;
      qint32 num_weights = 0;
      qint32 num_morphs = 0;
      (void)read_f32_at(bytes, static_cast<int>(vptr + 0), &nx);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 4), &ny);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 8), &nz);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 12), &tu);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 16), &tv);
      (void)read_i32_at(bytes, static_cast<int>(vptr + 20), &num_weights);
      if (has_morphs) {
        (void)read_i32_at(bytes, static_cast<int>(vptr + 24), &num_morphs);
      }

      if (num_weights < 0 || num_weights > 128 || num_morphs < 0 || num_morphs > 1000000) {
        if (error) {
          *error = QString("%1 vertex weight/morph counts are invalid.").arg(format_name);
        }
        return std::nullopt;
      }

      const qint64 morph_bytes = static_cast<qint64>(num_morphs) * 16;
      const qint64 weight_bytes = static_cast<qint64>(num_weights) * 20;
      const qint64 weights_off = vptr + vertex_base_bytes + morph_bytes;
      if (weights_off < surf_off || weights_off + weight_bytes > surf_end) {
        if (error) {
          *error = QString("%1 vertex payload is out of bounds.").arg(format_name);
        }
        return std::nullopt;
      }

      const QVector3D src_normal(nx, ny, nz);
      QVector3D p(0.0f, 0.0f, 0.0f);
      QVector3D n(0.0f, 0.0f, 0.0f);
      float total_w = 0.0f;

      for (int w = 0; w < num_weights; ++w) {
        const int wo = static_cast<int>(weights_off + static_cast<qint64>(w) * 20);
        float bw = 0.0f;
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        (void)read_f32_at(bytes, wo + 4, &bw);
        (void)read_f32_at(bytes, wo + 8, &ox);
        (void)read_f32_at(bytes, wo + 12, &oy);
        (void)read_f32_at(bytes, wo + 16, &oz);
        if (bw <= 0.0f) {
          continue;
        }
        p += QVector3D(ox, oy, oz) * bw;
        n += src_normal * bw;
        total_w += bw;
      }

      if (total_w <= 0.0f) {
        if (num_weights > 0) {
          float ox = 0.0f, oy = 0.0f, oz = 0.0f;
          const int wo0 = static_cast<int>(weights_off);
          (void)read_f32_at(bytes, wo0 + 8, &ox);
          (void)read_f32_at(bytes, wo0 + 12, &oy);
          (void)read_f32_at(bytes, wo0 + 16, &oz);
          p = QVector3D(ox, oy, oz);
        }
        n = src_normal;
      }

      if (n.lengthSquared() < 1e-12f) {
        n = QVector3D(0.0f, 0.0f, 1.0f);
      } else {
        n.normalize();
      }
      p *= mesh_scale;

      ModelVertex mv;
      mv.px = p.x();
      mv.py = p.y();
      mv.pz = p.z();
      mv.nx = n.x();
      mv.ny = n.y();
      mv.nz = n.z();
      mv.u = tu;
      mv.v = 1.0f - tv;
      vertices.push_back(mv);

      vptr = weights_off + weight_bytes;
    }

    const int tri_off = surf_off + ofs_tris;
    if (!bytes_range_ok(bytes, tri_off, static_cast<qint64>(num_tris) * 12) || tri_off < surf_off || tri_off > surf_end) {
      if (error) {
        *error = QString("%1 triangles are out of bounds.").arg(format_name);
      }
      return std::nullopt;
    }

    const int first_index = indices.size();
    for (int t = 0; t < num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      const int to = tri_off + t * 12;
      (void)read_i32_at(bytes, to + 0, &i0);
      (void)read_i32_at(bytes, to + 4, &i1);
      (void)read_i32_at(bytes, to + 8, &i2);
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i0));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i1));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i2));
    }

    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface ms;
      ms.name = read_fixed_latin1_string_at(bytes, surf_off + 4, 64);
      ms.first_index = first_index;
      ms.index_count = index_count;
      surfaces.push_back(std::move(ms));
    }

    surf_off = surf_end;
    if (surf_off >= ofs_end) {
      break;
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = QString("%1 contains no drawable geometry.").arg(format_name);
    }
    return std::nullopt;
  }
  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = format_name.toLower();
  out.frame_count = 1;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_skb(const QString& file_path, QString* error) {
  constexpr quint32 kSkbIdent = static_cast<quint32>('S') | (static_cast<quint32>('K') << 8) |
                                (static_cast<quint32>('L') << 16) | (static_cast<quint32>(' ') << 24);
  return load_skel_mesh_file(file_path, kSkbIdent, 3, 4, "skb", error);
}

std::optional<LoadedModel> load_skd(const QString& file_path, QString* error) {
  constexpr quint32 kSkdIdent = static_cast<quint32>('S') | (static_cast<quint32>('K') << 8) |
                                (static_cast<quint32>('M') << 16) | (static_cast<quint32>('D') << 24);
  return load_skel_mesh_file(file_path, kSkdIdent, 5, 6, "skd", error);
}

std::optional<LoadedModel> load_mdm(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open MDM.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 100) {
    if (error) {
      *error = "MDM header is incomplete.";
    }
    return std::nullopt;
  }

  constexpr quint32 kMdmIdent = 0x574D444Du;  // "MDMW"
  constexpr qint32 kMdmVersion = 3;

  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_surfaces = 0;
  qint32 ofs_surfaces = 0;
  qint32 num_tags = 0;
  qint32 ofs_tags = 0;
  qint32 ofs_end = 0;
  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 80, &num_surfaces) ||
      !read_i32_at(bytes, 84, &ofs_surfaces) || !read_i32_at(bytes, 88, &num_tags) || !read_i32_at(bytes, 92, &ofs_tags) ||
      !read_i32_at(bytes, 96, &ofs_end)) {
    if (error) {
      *error = "MDM header is incomplete.";
    }
    return std::nullopt;
  }
  (void)num_tags;
  (void)ofs_tags;

  if (ident != kMdmIdent || version != kMdmVersion) {
    if (error) {
      *error = "Not a supported MDM (expected MDMW v3).";
    }
    return std::nullopt;
  }
  if (num_surfaces <= 0 || num_surfaces > 4096 || ofs_surfaces < 0 || ofs_surfaces >= bytes.size() || ofs_end <= 0 || ofs_end > bytes.size()) {
    if (error) {
      *error = "MDM header values are invalid.";
    }
    return std::nullopt;
  }

  QVector<BoneTransform> bones;
  int frame_count = 1;
  QString mdx_err;
  const QString mdx_path = companion_path_with_ext(file_path, ".mdx");
  if (!mdx_path.isEmpty()) {
    (void)load_mdx_bind_transforms(mdx_path, &bones, &frame_count, &mdx_err);
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(num_surfaces);

  int surf_off = ofs_surfaces;
  for (int s = 0; s < num_surfaces; ++s) {
    if (!bytes_range_ok(bytes, surf_off, 176)) {
      break;
    }

    qint32 surf_ident = 0;
    qint32 shader_index = 0;
    qint32 min_lod = 0;
    qint32 surf_ofs_header = 0;
    qint32 num_verts = 0;
    qint32 ofs_verts = 0;
    qint32 num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 ofs_collapse = 0;
    qint32 num_bone_refs = 0;
    qint32 ofs_bone_refs = 0;
    qint32 ofs_surf_end = 0;

    if (!read_i32_at(bytes, surf_off + 0, &surf_ident) || !read_i32_at(bytes, surf_off + 132, &shader_index) || !read_i32_at(bytes, surf_off + 136, &min_lod) ||
        !read_i32_at(bytes, surf_off + 140, &surf_ofs_header) || !read_i32_at(bytes, surf_off + 144, &num_verts) ||
        !read_i32_at(bytes, surf_off + 148, &ofs_verts) || !read_i32_at(bytes, surf_off + 152, &num_tris) ||
        !read_i32_at(bytes, surf_off + 156, &ofs_tris) || !read_i32_at(bytes, surf_off + 160, &ofs_collapse) ||
        !read_i32_at(bytes, surf_off + 164, &num_bone_refs) || !read_i32_at(bytes, surf_off + 168, &ofs_bone_refs) ||
        !read_i32_at(bytes, surf_off + 172, &ofs_surf_end)) {
      break;
    }
    (void)surf_ident;
    (void)shader_index;
    (void)min_lod;
    (void)surf_ofs_header;
    (void)ofs_collapse;

    if (num_verts <= 0 || num_verts > 2'000'000 || num_tris < 0 || num_tris > 4'000'000 || num_bone_refs < 0 || num_bone_refs > 4096 || ofs_surf_end <= 0) {
      if (error) {
        *error = "MDM surface values are invalid.";
      }
      return std::nullopt;
    }

    const QString surf_name = read_fixed_latin1_string_at(bytes, surf_off + 4, 64);
    const QString shader_name = read_fixed_latin1_string_at(bytes, surf_off + 68, 64);

    QVector<int> bone_refs;
    bone_refs.resize(num_bone_refs);
    if (!bytes_range_ok(bytes, static_cast<qint64>(surf_off) + ofs_bone_refs, static_cast<qint64>(num_bone_refs) * 4)) {
      if (error) {
        *error = "MDM surface bone references are out of bounds.";
      }
      return std::nullopt;
    }
    for (int i = 0; i < num_bone_refs; ++i) {
      qint32 ref = 0;
      (void)read_i32_at(bytes, surf_off + ofs_bone_refs + i * 4, &ref);
      bone_refs[i] = ref;
    }

    const int base_vertex = vertices.size();
    qint64 vptr = static_cast<qint64>(surf_off) + ofs_verts;
    for (int v = 0; v < num_verts; ++v) {
      if (!bytes_range_ok(bytes, vptr, 24)) {
        if (error) {
          *error = "MDM vertex data is incomplete.";
        }
        return std::nullopt;
      }

      float nx = 0.0f, ny = 0.0f, nz = 1.0f;
      float tu = 0.0f, tv = 0.0f;
      qint32 num_weights = 0;
      (void)read_f32_at(bytes, static_cast<int>(vptr + 0), &nx);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 4), &ny);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 8), &nz);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 12), &tu);
      (void)read_f32_at(bytes, static_cast<int>(vptr + 16), &tv);
      (void)read_i32_at(bytes, static_cast<int>(vptr + 20), &num_weights);

      if (num_weights < 0 || num_weights > 128) {
        if (error) {
          *error = "MDM vertex has invalid weight count.";
        }
        return std::nullopt;
      }

      const qint64 weight_bytes = static_cast<qint64>(num_weights) * 20;
      if (!bytes_range_ok(bytes, vptr + 24, weight_bytes)) {
        if (error) {
          *error = "MDM vertex weights are incomplete.";
        }
        return std::nullopt;
      }

      const QVector3D src_normal(nx, ny, nz);
      QVector3D p(0.0f, 0.0f, 0.0f);
      QVector3D n(0.0f, 0.0f, 0.0f);
      float total_w = 0.0f;

      for (int w = 0; w < num_weights; ++w) {
        const int wo = static_cast<int>(vptr + 24 + static_cast<qint64>(w) * 20);
        qint32 local_bone = -1;
        float bw = 0.0f;
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        (void)read_i32_at(bytes, wo + 0, &local_bone);
        (void)read_f32_at(bytes, wo + 4, &bw);
        (void)read_f32_at(bytes, wo + 8, &ox);
        (void)read_f32_at(bytes, wo + 12, &oy);
        (void)read_f32_at(bytes, wo + 16, &oz);

        if (bw <= 0.0f || local_bone < 0 || local_bone >= bone_refs.size()) {
          continue;
        }

        const int global_bone = bone_refs[local_bone];
        const bool bone_ok = (global_bone >= 0 && global_bone < bones.size() && bones[global_bone].valid);
        const QVector3D offset(ox, oy, oz);
        if (bone_ok) {
          p += transform_point(bones[global_bone], offset) * bw;
          n += transform_direction(bones[global_bone], src_normal) * bw;
        } else {
          p += offset * bw;
          n += src_normal * bw;
        }
        total_w += bw;
      }

      if (total_w <= 0.0f) {
        // Fallback: retain deterministic geometry when companion skeleton data is missing.
        if (num_weights > 0) {
          float ox = 0.0f, oy = 0.0f, oz = 0.0f;
          const int wo0 = static_cast<int>(vptr + 24);
          (void)read_f32_at(bytes, wo0 + 8, &ox);
          (void)read_f32_at(bytes, wo0 + 12, &oy);
          (void)read_f32_at(bytes, wo0 + 16, &oz);
          p = QVector3D(ox, oy, oz);
        }
        n = src_normal;
      }

      if (n.lengthSquared() < 1e-12f) {
        n = QVector3D(0.0f, 0.0f, 1.0f);
      } else {
        n.normalize();
      }

      ModelVertex mv;
      mv.px = p.x();
      mv.py = p.y();
      mv.pz = p.z();
      mv.nx = n.x();
      mv.ny = n.y();
      mv.nz = n.z();
      mv.u = tu;
      mv.v = 1.0f - tv;
      vertices.push_back(mv);

      vptr += 24 + weight_bytes;
    }

    const int tri_off = surf_off + ofs_tris;
    if (!bytes_range_ok(bytes, tri_off, static_cast<qint64>(num_tris) * 12)) {
      if (error) {
        *error = "MDM triangles are out of bounds.";
      }
      return std::nullopt;
    }

    const int first_index = indices.size();
    for (int t = 0; t < num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      const int to = tri_off + t * 12;
      (void)read_i32_at(bytes, to + 0, &i0);
      (void)read_i32_at(bytes, to + 4, &i1);
      (void)read_i32_at(bytes, to + 8, &i2);
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i0));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i1));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i2));
    }

    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface ms;
      ms.name = surf_name;
      ms.shader = shader_name;
      ms.first_index = first_index;
      ms.index_count = index_count;
      surfaces.push_back(std::move(ms));
    }

    surf_off += ofs_surf_end;
    if (surf_off <= 0 || surf_off > ofs_end) {
      break;
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "MDM contains no drawable geometry.";
    }
    return std::nullopt;
  }
  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "mdm";
  out.frame_count = frame_count;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_bounds(&out.mesh);
  return out;
}

std::optional<LoadedModel> load_glm(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open GLM.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.size() < 164) {
    if (error) {
      *error = "GLM header is incomplete.";
    }
    return std::nullopt;
  }

  constexpr quint32 kMdxmIdent = 0x4D474C32u;  // "2LGM"
  constexpr qint32 kMdxmVersion = 6;

  quint32 ident = 0;
  qint32 version = 0;
  qint32 num_bones = 0;
  qint32 num_lods = 0;
  qint32 ofs_lods = 0;
  qint32 num_surfaces = 0;
  qint32 ofs_surf_hierarchy = 0;
  qint32 ofs_end = 0;

  if (!read_u32_at(bytes, 0, &ident) || !read_i32_at(bytes, 4, &version) || !read_i32_at(bytes, 136, &num_bones) ||
      !read_i32_at(bytes, 140, &num_lods) || !read_i32_at(bytes, 144, &ofs_lods) || !read_i32_at(bytes, 148, &num_surfaces) ||
      !read_i32_at(bytes, 152, &ofs_surf_hierarchy) || !read_i32_at(bytes, 156, &ofs_end)) {
    if (error) {
      *error = "GLM header is incomplete.";
    }
    return std::nullopt;
  }
  (void)num_bones;

  if (ident != kMdxmIdent || version != kMdxmVersion) {
    if (error) {
      *error = "Not a supported GLM (expected 2LGM v6).";
    }
    return std::nullopt;
  }
  if (num_lods <= 0 || num_lods > 64 || num_surfaces <= 0 || num_surfaces > 4096 || ofs_lods < 0 || ofs_lods >= bytes.size() || ofs_end <= 0 ||
      ofs_end > bytes.size()) {
    if (error) {
      *error = "GLM header values are invalid.";
    }
    return std::nullopt;
  }

  const QString anim_name = read_fixed_latin1_string_at(bytes, 72, 64);

  QVector<BoneTransform> bones;
  QString gla_err;
  const QString gla_path = resolve_glm_gla_path(file_path, anim_name);
  if (!gla_path.isEmpty()) {
    (void)load_mdxa_base_transforms(gla_path, &bones, &gla_err);
  }

  struct SurfaceHierarchyInfo {
    QString name;
    QString shader;
  };
  QVector<SurfaceHierarchyInfo> hierarchy;
  hierarchy.resize(num_surfaces);

  if (ofs_surf_hierarchy >= 0 && ofs_surf_hierarchy < bytes.size()) {
    qint64 hptr = ofs_surf_hierarchy;
    for (int i = 0; i < num_surfaces; ++i) {
      if (!bytes_range_ok(bytes, hptr, 144)) {
        break;
      }
      hierarchy[i].name = read_fixed_latin1_string_at(bytes, static_cast<int>(hptr + 0), 64);
      hierarchy[i].shader = read_fixed_latin1_string_at(bytes, static_cast<int>(hptr + 68), 64);

      qint32 num_children = 0;
      (void)read_i32_at(bytes, static_cast<int>(hptr + 140), &num_children);
      if (num_children < 0 || num_children > 10000) {
        break;
      }
      const qint64 step = 144 + static_cast<qint64>(num_children) * 4;
      if (!bytes_range_ok(bytes, hptr, step)) {
        break;
      }
      hptr += step;
    }
  }

  qint32 lod_ofs_end = 0;
  if (!read_i32_at(bytes, ofs_lods, &lod_ofs_end)) {
    if (error) {
      *error = "GLM LOD header is incomplete.";
    }
    return std::nullopt;
  }
  (void)lod_ofs_end;

  const int lod_offset_table = ofs_lods + 4;
  if (!bytes_range_ok(bytes, lod_offset_table, static_cast<qint64>(num_surfaces) * 4)) {
    if (error) {
      *error = "GLM LOD surface offset table is invalid.";
    }
    return std::nullopt;
  }

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;
  surfaces.reserve(num_surfaces);

  for (int s = 0; s < num_surfaces; ++s) {
    qint32 rel_surf = 0;
    (void)read_i32_at(bytes, lod_offset_table + s * 4, &rel_surf);
    const int surf_off = ofs_lods + rel_surf;
    if (!bytes_range_ok(bytes, surf_off, 40)) {
      continue;
    }

    qint32 surf_ident = 0;
    qint32 this_surface_index = s;
    qint32 surf_ofs_header = 0;
    qint32 num_verts = 0;
    qint32 ofs_verts = 0;
    qint32 num_tris = 0;
    qint32 ofs_tris = 0;
    qint32 num_bone_refs = 0;
    qint32 ofs_bone_refs = 0;
    qint32 ofs_surf_end = 0;

    if (!read_i32_at(bytes, surf_off + 0, &surf_ident) || !read_i32_at(bytes, surf_off + 4, &this_surface_index) ||
        !read_i32_at(bytes, surf_off + 8, &surf_ofs_header) || !read_i32_at(bytes, surf_off + 12, &num_verts) ||
        !read_i32_at(bytes, surf_off + 16, &ofs_verts) || !read_i32_at(bytes, surf_off + 20, &num_tris) ||
        !read_i32_at(bytes, surf_off + 24, &ofs_tris) || !read_i32_at(bytes, surf_off + 28, &num_bone_refs) ||
        !read_i32_at(bytes, surf_off + 32, &ofs_bone_refs) || !read_i32_at(bytes, surf_off + 36, &ofs_surf_end)) {
      continue;
    }
    (void)surf_ident;
    (void)surf_ofs_header;
    (void)ofs_surf_end;

    if (num_verts <= 0 || num_verts > 2'000'000 || num_tris < 0 || num_tris > 4'000'000 || num_bone_refs < 0 || num_bone_refs > 4096) {
      continue;
    }

    const qint64 verts_off = static_cast<qint64>(surf_off) + ofs_verts;
    const qint64 verts_bytes = static_cast<qint64>(num_verts) * 32;
    if (!bytes_range_ok(bytes, verts_off, verts_bytes)) {
      continue;
    }
    const qint64 tex_off = verts_off + verts_bytes;
    if (!bytes_range_ok(bytes, tex_off, static_cast<qint64>(num_verts) * 8)) {
      continue;
    }

    QVector<int> bone_refs;
    bone_refs.resize(num_bone_refs);
    if (bytes_range_ok(bytes, static_cast<qint64>(surf_off) + ofs_bone_refs, static_cast<qint64>(num_bone_refs) * 4)) {
      for (int i = 0; i < num_bone_refs; ++i) {
        qint32 ref = 0;
        (void)read_i32_at(bytes, surf_off + ofs_bone_refs + i * 4, &ref);
        bone_refs[i] = ref;
      }
    } else {
      bone_refs.clear();
    }

    const int base_vertex = vertices.size();
    for (int v = 0; v < num_verts; ++v) {
      const int vo = static_cast<int>(verts_off + static_cast<qint64>(v) * 32);
      const int to = static_cast<int>(tex_off + static_cast<qint64>(v) * 8);

      float nx = 0.0f, ny = 0.0f, nz = 1.0f;
      float px = 0.0f, py = 0.0f, pz = 0.0f;
      quint32 packed = 0;
      (void)read_f32_at(bytes, vo + 0, &nx);
      (void)read_f32_at(bytes, vo + 4, &ny);
      (void)read_f32_at(bytes, vo + 8, &nz);
      (void)read_f32_at(bytes, vo + 12, &px);
      (void)read_f32_at(bytes, vo + 16, &py);
      (void)read_f32_at(bytes, vo + 20, &pz);
      (void)read_u32_at(bytes, vo + 24, &packed);

      const auto* bwp = reinterpret_cast<const unsigned char*>(bytes.constData() + vo + 28);
      const quint8 bw[4] = {bwp[0], bwp[1], bwp[2], bwp[3]};

      const QVector3D src_pos(px, py, pz);
      const QVector3D src_nrm(nx, ny, nz);
      QVector3D out_pos = src_pos;
      QVector3D out_nrm = src_nrm;

      if (!bones.isEmpty() && !bone_refs.isEmpty()) {
        const int nweights = std::clamp(static_cast<int>((packed >> 30) & 0x3) + 1, 1, 4);
        float total_w = 0.0f;
        QVector3D p(0.0f, 0.0f, 0.0f);
        QVector3D n(0.0f, 0.0f, 0.0f);
        for (int wi = 0; wi < nweights; ++wi) {
          float w = 0.0f;
          if (wi == nweights - 1) {
            w = std::max(0.0f, 1.0f - total_w);
          } else {
            int packed_w = static_cast<int>(bw[wi]);
            packed_w |= static_cast<int>((packed >> (12 + wi * 2)) & 0x300u);
            w = static_cast<float>(packed_w) * (1.0f / 1023.0f);
            total_w += w;
          }
          if (w <= 0.0f) {
            continue;
          }

          const int local_ref = static_cast<int>((packed >> (wi * 5)) & 0x1Fu);
          if (local_ref < 0 || local_ref >= bone_refs.size()) {
            continue;
          }
          const int global_bone = bone_refs[local_ref];
          if (global_bone < 0 || global_bone >= bones.size() || !bones[global_bone].valid) {
            continue;
          }

          p += transform_point(bones[global_bone], src_pos) * w;
          n += transform_direction(bones[global_bone], src_nrm) * w;
        }
        if (p.lengthSquared() > 0.0f) {
          out_pos = p;
        }
        if (n.lengthSquared() > 1e-12f) {
          out_nrm = n.normalized();
        }
      }

      float tu = 0.0f, tv = 0.0f;
      (void)read_f32_at(bytes, to + 0, &tu);
      (void)read_f32_at(bytes, to + 4, &tv);

      ModelVertex mv;
      mv.px = out_pos.x();
      mv.py = out_pos.y();
      mv.pz = out_pos.z();
      mv.nx = out_nrm.x();
      mv.ny = out_nrm.y();
      mv.nz = out_nrm.z();
      mv.u = tu;
      mv.v = 1.0f - tv;
      vertices.push_back(mv);
    }

    const int tri_off = surf_off + ofs_tris;
    if (!bytes_range_ok(bytes, tri_off, static_cast<qint64>(num_tris) * 12)) {
      continue;
    }

    const int first_index = indices.size();
    for (int t = 0; t < num_tris; ++t) {
      qint32 i0 = 0, i1 = 0, i2 = 0;
      const int to = tri_off + t * 12;
      (void)read_i32_at(bytes, to + 0, &i0);
      (void)read_i32_at(bytes, to + 4, &i1);
      (void)read_i32_at(bytes, to + 8, &i2);
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= num_verts || i1 >= num_verts || i2 >= num_verts) {
        continue;
      }
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i0));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i1));
      indices.push_back(static_cast<std::uint32_t>(base_vertex + i2));
    }

    const int index_count = indices.size() - first_index;
    if (index_count > 0) {
      ModelSurface ms;
      const int hi = (this_surface_index >= 0 && this_surface_index < hierarchy.size()) ? this_surface_index : s;
      ms.name = (hi >= 0 && hi < hierarchy.size()) ? hierarchy[hi].name : QString("surface%1").arg(s);
      ms.shader = (hi >= 0 && hi < hierarchy.size()) ? hierarchy[hi].shader : QString();
      ms.first_index = first_index;
      ms.index_count = index_count;
      surfaces.push_back(std::move(ms));
    }
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "GLM contains no drawable geometry.";
    }
    return std::nullopt;
  }
  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "glm";
  out.frame_count = 1;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
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

struct ObjVertKey {
  int v = -1;
  int vt = -1;
};

inline bool operator==(const ObjVertKey& a, const ObjVertKey& b) { return a.v == b.v && a.vt == b.vt; }

inline uint qHash(const ObjVertKey& k, uint seed = 0) noexcept {
  const quint32 v = static_cast<quint32>(k.v) * 0x9E3779B1u;
  const quint32 vt = static_cast<quint32>(k.vt) * 0x85EBCA77u;
  return ::qHash(v ^ (vt + 0xC2B2AE3Du + (seed << 6) + (seed >> 2)), seed);
}

std::optional<LoadedModel> load_obj(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open OBJ.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.isEmpty()) {
    if (error) {
      *error = "Empty OBJ.";
    }
    return std::nullopt;
  }

  struct LineTok {
    const char* data = nullptr;
    int len = 0;
    int pos = 0;

    void skip_ws() {
      while (pos < len && static_cast<unsigned char>(data[pos]) <= 0x20) {
        ++pos;
      }
    }

    [[nodiscard]] QString next() {
      skip_ws();
      if (pos >= len) {
        return {};
      }

      if (data[pos] == '"') {
        ++pos;
        const int start = pos;
        while (pos < len && data[pos] != '"') {
          ++pos;
        }
        const int end = pos;
        if (pos < len && data[pos] == '"') {
          ++pos;
        }
        return QString::fromLatin1(data + start, end - start);
      }

      const int start = pos;
      while (pos < len && static_cast<unsigned char>(data[pos]) > 0x20) {
        ++pos;
      }
      return QString::fromLatin1(data + start, pos - start);
    }
  };

  const QString obj_dir = QFileInfo(file_path).absolutePath();

  QHash<QString, QString> material_to_map;
  QHash<QString, bool> parsed_mtls;

  const auto parse_mtl_file = [&](const QString& mtl_path) {
    const QString abs = QFileInfo(mtl_path).absoluteFilePath();
    if (parsed_mtls.contains(abs)) {
      return;
    }
    parsed_mtls.insert(abs, true);

    QFile mf(mtl_path);
    if (!mf.open(QIODevice::ReadOnly)) {
      return;
    }
    const QByteArray mbytes = mf.readAll();
    if (mbytes.isEmpty()) {
      return;
    }

    QString current;

    int p = 0;
    while (p < mbytes.size()) {
      int nl = mbytes.indexOf('\n', p);
      if (nl < 0) {
        nl = mbytes.size();
      }
      int line_len = nl - p;
      if (line_len > 0 && mbytes[p + line_len - 1] == '\r') {
        --line_len;
      }

      const char* line = mbytes.constData() + p;
      p = nl + 1;

      // Strip comments.
      int effective_len = line_len;
      for (int i = 0; i < line_len; ++i) {
        if (line[i] == '#') {
          effective_len = i;
          break;
        }
      }

      // Trim.
      int b = 0;
      while (b < effective_len && static_cast<unsigned char>(line[b]) <= 0x20) {
        ++b;
      }
      int e = effective_len;
      while (e > b && static_cast<unsigned char>(line[e - 1]) <= 0x20) {
        --e;
      }
      if (e <= b) {
        continue;
      }

      LineTok tok{line + b, e - b, 0};
      const QString cmd = tok.next().toLower();
      if (cmd.isEmpty()) {
        continue;
      }
      if (cmd == "newmtl") {
        current = tok.next().trimmed();
        continue;
      }
      if (cmd == "map_kd") {
        if (current.isEmpty()) {
          continue;
        }
        QString last;
        for (;;) {
          const QString t = tok.next();
          if (t.isEmpty()) {
            break;
          }
          last = t;
        }
        if (last.isEmpty()) {
          continue;
        }
        material_to_map.insert(current.toLower(), last.trimmed());
      }
    }
  };

  QVector<QVector3D> positions;
  QVector<QVector2D> texcoords;
  QVector<QVector3D> normals;

  QVector<ModelVertex> vertices;
  QVector<std::uint32_t> indices;
  QHash<ObjVertKey, std::uint32_t> vert_map;

  QVector<ModelSurface> surfaces;
  QString current_object;
  QString current_group;
  QString current_material;
  int surface_first_index = 0;
  QString surface_name = "model";
  QString surface_shader;
  bool surface_active = false;

  const auto flush_surface = [&]() {
    if (!surface_active) {
      return;
    }
    const int count = indices.size() - surface_first_index;
    if (count <= 0) {
      surface_active = false;
      return;
    }
    ModelSurface s;
    s.name = surface_name.isEmpty() ? "model" : surface_name;
    s.shader = surface_shader;
    s.first_index = surface_first_index;
    s.index_count = count;
    surfaces.push_back(std::move(s));
    surface_active = false;
  };

  const auto begin_surface = [&]() {
    surface_first_index = indices.size();
    surface_active = true;
  };

  const auto resolve_shader = [&](const QString& material) -> QString {
    if (material.isEmpty()) {
      return {};
    }
    const QString key = material.trimmed().toLower();
    const QString mapped = material_to_map.value(key);
    return mapped.isEmpty() ? material : mapped;
  };

  const auto pick_surface_name = [&]() -> QString {
    if (!current_material.isEmpty()) {
      return current_material;
    }
    if (!current_group.isEmpty()) {
      return current_group;
    }
    if (!current_object.isEmpty()) {
      return current_object;
    }
    return "model";
  };

  const auto ensure_surface = [&]() {
    const QString want_name = pick_surface_name();
    const QString want_shader = resolve_shader(current_material);
    if (!surface_active) {
      surface_name = want_name;
      surface_shader = want_shader;
      begin_surface();
      return;
    }
    if (surface_name != want_name || surface_shader != want_shader) {
      flush_surface();
      surface_name = want_name;
      surface_shader = want_shader;
      begin_surface();
    }
  };

  auto parse_float = [](const QString& s, bool* ok) -> float {
    bool local_ok = false;
    const float out = s.toFloat(&local_ok);
    if (ok) {
      *ok = local_ok;
    }
    return out;
  };

  auto parse_int = [](const QString& s, bool* ok) -> int {
    bool local_ok = false;
    const int out = s.toInt(&local_ok);
    if (ok) {
      *ok = local_ok;
    }
    return out;
  };

  struct ObjRef {
    int v = -1;
    int vt = -1;
  };

  const auto parse_ref = [&](const QString& tok, ObjRef* out) -> bool {
    if (!out) {
      return false;
    }
    out->v = -1;
    out->vt = -1;

    const int s1 = tok.indexOf('/');
    const int s2 = (s1 >= 0) ? tok.indexOf('/', s1 + 1) : -1;

    const QString sv = (s1 < 0) ? tok : tok.left(s1);
    const QString st = (s1 < 0) ? QString() : (s2 < 0 ? tok.mid(s1 + 1) : tok.mid(s1 + 1, s2 - (s1 + 1)));

    bool okv = false;
    const int iv = parse_int(sv, &okv);
    if (!okv || iv == 0) {
      return false;
    }
    const int vcount = positions.size();
    const int v0 = (iv > 0) ? (iv - 1) : (vcount + iv);
    if (v0 < 0 || v0 >= vcount) {
      return false;
    }
    out->v = v0;

    if (!st.isEmpty()) {
      bool okt = false;
      const int it = parse_int(st, &okt);
      if (okt && it != 0) {
        const int tcount = texcoords.size();
        const int t0 = (it > 0) ? (it - 1) : (tcount + it);
        if (t0 >= 0 && t0 < tcount) {
          out->vt = t0;
        }
      }
    }
    return true;
  };

  const auto find_or_create_vertex = [&](const ObjRef& r) -> std::uint32_t {
    ObjVertKey key;
    key.v = r.v;
    key.vt = r.vt;
    const auto it = vert_map.find(key);
    if (it != vert_map.end()) {
      return *it;
    }

    const QVector3D p = positions[r.v];
    QVector2D uv(0.0f, 0.0f);
    if (r.vt >= 0 && r.vt < texcoords.size()) {
      uv = texcoords[r.vt];
    }

    ModelVertex v;
    v.px = p.x();
    v.py = p.y();
    v.pz = p.z();
    v.u = uv.x();
    v.v = uv.y();
    vertices.push_back(v);

    const std::uint32_t idx = static_cast<std::uint32_t>(vertices.size() - 1);
    vert_map.insert(key, idx);
    return idx;
  };

  int p = 0;
  while (p < bytes.size()) {
    int nl = bytes.indexOf('\n', p);
    if (nl < 0) {
      nl = bytes.size();
    }
    int line_len = nl - p;
    if (line_len > 0 && bytes[p + line_len - 1] == '\r') {
      --line_len;
    }

    const char* line = bytes.constData() + p;
    p = nl + 1;

    int effective_len = line_len;
    for (int i = 0; i < line_len; ++i) {
      if (line[i] == '#') {
        effective_len = i;
        break;
      }
    }

    int b = 0;
    while (b < effective_len && static_cast<unsigned char>(line[b]) <= 0x20) {
      ++b;
    }
    int e = effective_len;
    while (e > b && static_cast<unsigned char>(line[e - 1]) <= 0x20) {
      --e;
    }
    if (e <= b) {
      continue;
    }

    LineTok tok{line + b, e - b, 0};
    const QString cmd = tok.next().toLower();
    if (cmd.isEmpty()) {
      continue;
    }

    if (cmd == "v") {
      bool okx = false, oky = false, okz = false;
      const float x = parse_float(tok.next(), &okx);
      const float y = parse_float(tok.next(), &oky);
      const float z = parse_float(tok.next(), &okz);
      if (!(okx && oky && okz)) {
        continue;
      }
      positions.push_back(QVector3D(x, y, z));
      continue;
    }

    if (cmd == "vt") {
      const QString su = tok.next();
      const QString sv = tok.next();
      if (su.isEmpty()) {
        continue;
      }
      bool oku = false;
      const float u = parse_float(su, &oku);
      float v = 0.0f;
      bool okv = true;
      if (!sv.isEmpty()) {
        v = parse_float(sv, &okv);
      }
      if (!(oku && okv)) {
        continue;
      }
      texcoords.push_back(QVector2D(u, v));
      continue;
    }

    if (cmd == "vn") {
      bool okx = false, oky = false, okz = false;
      const float x = parse_float(tok.next(), &okx);
      const float y = parse_float(tok.next(), &oky);
      const float z = parse_float(tok.next(), &okz);
      if (!(okx && oky && okz)) {
        continue;
      }
      normals.push_back(QVector3D(x, y, z));
      continue;
    }

    if (cmd == "o") {
      const QString name = tok.next().trimmed();
      if (!name.isEmpty()) {
        current_object = name;
      }
      continue;
    }

    if (cmd == "g") {
      const QString name = tok.next().trimmed();
      if (!name.isEmpty()) {
        current_group = name;
      }
      continue;
    }

    if (cmd == "mtllib") {
      for (;;) {
        QString ref = tok.next().trimmed();
        if (ref.isEmpty()) {
          break;
        }
        ref.replace('\\', '/');

        QString candidate = QDir(obj_dir).filePath(ref);
        if (!QFileInfo::exists(candidate)) {
          const QString leaf = QFileInfo(ref).fileName();
          const QString alt = QDir(obj_dir).filePath(leaf);
          candidate = QFileInfo::exists(alt) ? alt : QString();
        }
        if (!candidate.isEmpty()) {
          parse_mtl_file(candidate);
        }
      }
      continue;
    }

    if (cmd == "usemtl") {
      current_material = tok.next().trimmed();
      continue;
    }

    if (cmd == "f") {
      if (positions.isEmpty()) {
        continue;
      }

      QVector<ObjRef> refs;
      refs.reserve(8);
      for (;;) {
        const QString t = tok.next();
        if (t.isEmpty()) {
          break;
        }
        ObjRef r;
        if (parse_ref(t, &r)) {
          refs.push_back(r);
        }
      }
      if (refs.size() < 3) {
        continue;
      }

      ensure_surface();
      const std::uint32_t i0 = find_or_create_vertex(refs[0]);
      for (int i = 1; i + 1 < refs.size(); ++i) {
        const std::uint32_t i1 = find_or_create_vertex(refs[i]);
        const std::uint32_t i2 = find_or_create_vertex(refs[i + 1]);
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
      }
      continue;
    }
  }

  flush_surface();

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "OBJ contains no drawable geometry.";
    }
    return std::nullopt;
  }

  // Update shaders from MTL mappings (surface name == material name).
  if (!material_to_map.isEmpty()) {
    for (ModelSurface& s : surfaces) {
      const QString mapped = material_to_map.value(s.name.trimmed().toLower());
      if (!mapped.isEmpty()) {
        s.shader = mapped;
      }
    }
  }

  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "obj";
  out.frame_count = 1;
  out.surface_count = std::max(1, static_cast<int>(surfaces.size()));
  out.mesh.vertices = std::move(vertices);
  out.mesh.indices = std::move(indices);
  out.surfaces = std::move(surfaces);
  compute_smooth_normals(&out.mesh);
  compute_bounds(&out.mesh);
  return out;
}

struct LwoLayer {
  int number = 0;
  QString name;

  QVector<QVector3D> points;
  QVector<QVector<quint32>> polys;               // point indices per polygon (poly index is array index)
  QVector<QPair<quint32, quint16>> ptag_surfs;   // (poly_index, tag_index) for SURF PTAGs

  QString uv_map_name;
  QHash<quint32, QVector2D> vmap_uv;  // point -> uv
  QHash<quint64, QVector2D> vmad_uv;  // (poly<<32)|point -> uv
};

std::optional<LoadedModel> load_lwo(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }

  QFile f(file_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open LWO.";
    }
    return std::nullopt;
  }
  const QByteArray bytes = f.readAll();
  if (bytes.isEmpty()) {
    if (error) {
      *error = "Empty LWO.";
    }
    return std::nullopt;
  }

  struct BeCursor {
    const QByteArray* data = nullptr;
    int pos = 0;

    [[nodiscard]] bool can_read(int n) const { return data && n >= 0 && pos >= 0 && pos + n <= data->size(); }

    bool seek(int p) {
      if (!data) {
        return false;
      }
      if (p < 0 || p > data->size()) {
        return false;
      }
      pos = p;
      return true;
    }

    bool skip(int n) { return seek(pos + n); }

    bool read_u16(quint16* out) {
      if (!can_read(2) || !out) {
        return false;
      }
      const quint8 b0 = static_cast<quint8>((*data)[pos + 0]);
      const quint8 b1 = static_cast<quint8>((*data)[pos + 1]);
      *out = static_cast<quint16>((b0 << 8) | b1);
      pos += 2;
      return true;
    }

    bool read_u32(quint32* out) {
      if (!can_read(4) || !out) {
        return false;
      }
      const quint8 b0 = static_cast<quint8>((*data)[pos + 0]);
      const quint8 b1 = static_cast<quint8>((*data)[pos + 1]);
      const quint8 b2 = static_cast<quint8>((*data)[pos + 2]);
      const quint8 b3 = static_cast<quint8>((*data)[pos + 3]);
      *out = (static_cast<quint32>(b0) << 24) | (static_cast<quint32>(b1) << 16) | (static_cast<quint32>(b2) << 8) |
             static_cast<quint32>(b3);
      pos += 4;
      return true;
    }

    bool read_f32(float* out) {
      quint32 u = 0;
      if (!read_u32(&u) || !out) {
        return false;
      }
      static_assert(sizeof(float) == sizeof(quint32));
      float f = 0.0f;
      memcpy(&f, &u, sizeof(float));
      *out = f;
      return true;
    }

    bool read_id(QByteArray* out) {
      if (!can_read(4) || !out) {
        return false;
      }
      *out = data->mid(pos, 4);
      pos += 4;
      return true;
    }

    bool read_s0(QString* out, int end_pos) {
      if (!data || pos < 0 || pos >= end_pos) {
        if (out) {
          out->clear();
        }
        return true;
      }
      const int start = pos;
      while (pos < end_pos && (*data)[pos] != '\0') {
        ++pos;
      }
      const int len = pos - start;
      if (out) {
        *out = QString::fromLatin1(data->constData() + start, len);
      }
      if (pos < end_pos && (*data)[pos] == '\0') {
        ++pos;
      }
      // Pad to even.
      if (((len + 1) & 1) != 0) {
        if (pos < end_pos) {
          ++pos;
        }
      }
      return true;
    }

    bool read_vx(quint32* out, int end_pos) {
      if (!out) {
        return false;
      }
      if (!can_read(2) || pos + 2 > end_pos) {
        return false;
      }
      quint16 w = 0;
      if (!read_u16(&w)) {
        return false;
      }
      if (w < 0xFF00u) {
        *out = static_cast<quint32>(w);
        return true;
      }
      if (!can_read(2) || pos + 2 > end_pos) {
        return false;
      }
      quint16 lo = 0;
      if (!read_u16(&lo)) {
        return false;
      }
      *out = (static_cast<quint32>(w & 0x00FFu) << 16) | static_cast<quint32>(lo);
      return true;
    }
  };

  BeCursor cur;
  cur.data = &bytes;

  QByteArray form;
  quint32 form_size = 0;
  QByteArray form_type;
  if (!cur.read_id(&form) || !cur.read_u32(&form_size) || !cur.read_id(&form_type)) {
    if (error) {
      *error = "LWO header is incomplete.";
    }
    return std::nullopt;
  }
  if (form != "FORM") {
    if (error) {
      *error = "Not an IFF FORM file.";
    }
    return std::nullopt;
  }
  (void)form_size;
  if (form_type != "LWO2" && form_type != "LWO3") {
    if (error) {
      *error = QString("Unsupported LWO type: %1").arg(QString::fromLatin1(form_type));
    }
    return std::nullopt;
  }

  QVector<QString> tags;
  QVector<LwoLayer> layers;
  layers.push_back(LwoLayer{});
  int current_layer = 0;

  auto layer = [&]() -> LwoLayer* {
    if (current_layer < 0 || current_layer >= layers.size()) {
      return nullptr;
    }
    return &layers[current_layer];
  };

  while (cur.pos + 8 <= bytes.size()) {
    QByteArray cid;
    quint32 csize = 0;
    if (!cur.read_id(&cid) || !cur.read_u32(&csize)) {
      break;
    }
    const int data_start = cur.pos;
    const int data_end = data_start + static_cast<int>(csize);
    if (data_end < data_start || data_end > bytes.size()) {
      if (error) {
        *error = "LWO chunk extends past end of file.";
      }
      return std::nullopt;
    }

    const auto finish = [&]() {
      cur.seek(data_end);
      if ((csize & 1u) != 0u) {
        cur.skip(1);
      }
    };

    if (cid == "TAGS") {
      cur.seek(data_start);
      while (cur.pos < data_end) {
        QString s;
        if (!cur.read_s0(&s, data_end)) {
          break;
        }
        tags.push_back(s);
      }
      finish();
      continue;
    }

    if (cid == "LAYR") {
      cur.seek(data_start);
      quint16 num = 0;
      quint16 flags = 0;
      if (!cur.read_u16(&num) || !cur.read_u16(&flags)) {
        finish();
        continue;
      }
      (void)flags;
      cur.skip(12);  // pivot
      QString lname;
      (void)cur.read_s0(&lname, data_end);

      LwoLayer l;
      l.number = static_cast<int>(num);
      l.name = lname;
      layers.push_back(std::move(l));
      current_layer = layers.size() - 1;
      finish();
      continue;
    }

    if (cid == "PNTS") {
      LwoLayer* l = layer();
      cur.seek(data_start);
      const int count = (data_end - data_start) / 12;
      if (l) {
        l->points.reserve(l->points.size() + count);
      }
      for (int i = 0; i < count; ++i) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!cur.read_f32(&x) || !cur.read_f32(&y) || !cur.read_f32(&z)) {
          break;
        }
        if (l) {
          l->points.push_back(QVector3D(x, y, z));
        }
      }
      finish();
      continue;
    }

    if (cid == "POLS") {
      LwoLayer* l = layer();
      cur.seek(data_start);
      QByteArray ptype;
      if (!cur.read_id(&ptype)) {
        finish();
        continue;
      }
      if (!l || ptype != "FACE") {
        finish();
        continue;
      }

      while (cur.pos < data_end) {
        quint16 nv_raw = 0;
        if (!cur.read_u16(&nv_raw)) {
          break;
        }
        int nv = static_cast<int>(nv_raw);
        if (nv > 1023) {
          nv = nv & 0x03FF;
        }

        QVector<quint32> poly;
        if (nv > 0) {
          poly.reserve(nv);
        }
        for (int i = 0; i < nv; ++i) {
          quint32 idx = 0;
          if (!cur.read_vx(&idx, data_end)) {
            break;
          }
          poly.push_back(idx);
        }
        l->polys.push_back(std::move(poly));
      }

      finish();
      continue;
    }

    if (cid == "PTAG") {
      LwoLayer* l = layer();
      cur.seek(data_start);
      QByteArray ptype;
      if (!cur.read_id(&ptype) || !l || ptype != "SURF") {
        finish();
        continue;
      }

      while (cur.pos < data_end) {
        quint32 poly = 0;
        quint16 tag = 0;
        if (!cur.read_vx(&poly, data_end) || !cur.read_u16(&tag)) {
          break;
        }
        l->ptag_surfs.push_back(qMakePair(poly, tag));
      }

      finish();
      continue;
    }

    if (cid == "VMAP" || cid == "VMAD") {
      LwoLayer* l = layer();
      cur.seek(data_start);
      QByteArray vtype;
      quint16 dim = 0;
      if (!cur.read_id(&vtype) || !cur.read_u16(&dim) || !l) {
        finish();
        continue;
      }
      QString name;
      (void)cur.read_s0(&name, data_end);

      const bool is_txuv = (vtype == "TXUV" && dim >= 2);
      if (!is_txuv) {
        finish();
        continue;
      }

      if (l->uv_map_name.isEmpty()) {
        l->uv_map_name = name;
      }
      const bool use = (name == l->uv_map_name);

      if (cid == "VMAP") {
        while (cur.pos < data_end) {
          quint32 point = 0;
          if (!cur.read_vx(&point, data_end)) {
            break;
          }
          float u = 0.0f, v = 0.0f;
          if (!cur.read_f32(&u) || !cur.read_f32(&v)) {
            break;
          }
          for (int i = 2; i < static_cast<int>(dim); ++i) {
            float tmp = 0.0f;
            if (!cur.read_f32(&tmp)) {
              break;
            }
          }
          if (use) {
            l->vmap_uv.insert(point, QVector2D(u, v));
          }
        }
      } else {  // VMAD
        while (cur.pos < data_end) {
          quint32 point = 0;
          quint32 poly = 0;
          if (!cur.read_vx(&point, data_end) || !cur.read_vx(&poly, data_end)) {
            break;
          }
          float u = 0.0f, v = 0.0f;
          if (!cur.read_f32(&u) || !cur.read_f32(&v)) {
            break;
          }
          for (int i = 2; i < static_cast<int>(dim); ++i) {
            float tmp = 0.0f;
            if (!cur.read_f32(&tmp)) {
              break;
            }
          }
          if (use) {
            const quint64 key = (static_cast<quint64>(poly) << 32) | static_cast<quint64>(point);
            l->vmad_uv.insert(key, QVector2D(u, v));
          }
        }
      }

      finish();
      continue;
    }

    // Unknown chunk.
    finish();
  }

  QVector<ModelVertex> vertices;
  QHash<QString, QVector<std::uint32_t>> indices_by_surface;
  QVector<QString> surface_order;

  for (const LwoLayer& l : layers) {
    if (l.points.isEmpty() || l.polys.isEmpty()) {
      continue;
    }

    QVector<int> poly_to_tag;
    poly_to_tag.resize(l.polys.size());
    for (int i = 0; i < poly_to_tag.size(); ++i) {
      poly_to_tag[i] = -1;
    }
    for (const auto& it : l.ptag_surfs) {
      const int poly = static_cast<int>(it.first);
      if (poly < 0 || poly >= poly_to_tag.size()) {
        continue;
      }
      poly_to_tag[poly] = static_cast<int>(it.second);
    }

    QHash<quint64, std::uint32_t> vert_map;
    vert_map.reserve(l.points.size());

    const auto vertex_for = [&](int poly_index, quint32 point_index) -> std::uint32_t {
      const quint64 vmad_key = (static_cast<quint64>(static_cast<quint32>(poly_index)) << 32) | static_cast<quint64>(point_index);
      const bool has_vmad = l.vmad_uv.contains(vmad_key);
      const quint32 uv_poly = has_vmad ? static_cast<quint32>(poly_index + 1) : 0u;
      const quint64 key = (static_cast<quint64>(point_index) << 32) | static_cast<quint64>(uv_poly);
      const auto it = vert_map.find(key);
      if (it != vert_map.end()) {
        return *it;
      }

      const QVector3D p = (point_index < static_cast<quint32>(l.points.size())) ? l.points[static_cast<int>(point_index)] : QVector3D();
      QVector2D uv(0.0f, 0.0f);
      if (has_vmad) {
        uv = l.vmad_uv.value(vmad_key);
      } else if (l.vmap_uv.contains(point_index)) {
        uv = l.vmap_uv.value(point_index);
      }

      ModelVertex v;
      v.px = p.x();
      v.py = p.y();
      v.pz = p.z();
      v.u = uv.x();
      v.v = 1.0f - uv.y();
      vertices.push_back(v);

      const std::uint32_t idx = static_cast<std::uint32_t>(vertices.size() - 1);
      vert_map.insert(key, idx);
      return idx;
    };

    for (int pi = 0; pi < l.polys.size(); ++pi) {
      const QVector<quint32>& poly = l.polys[pi];
      if (poly.size() < 3) {
        continue;
      }

      QString surf = "model";
      const int tag = poly_to_tag.value(pi, -1);
      if (tag >= 0 && tag < tags.size()) {
        const QString t = tags[tag].trimmed();
        if (!t.isEmpty()) {
          surf = t;
        }
      }

      if (!indices_by_surface.contains(surf)) {
        indices_by_surface.insert(surf, {});
        surface_order.push_back(surf);
      }
      QVector<std::uint32_t>& out = indices_by_surface[surf];

      const std::uint32_t i0 = vertex_for(pi, poly[0]);
      for (int i = 1; i + 1 < poly.size(); ++i) {
        const std::uint32_t i1 = vertex_for(pi, poly[i]);
        const std::uint32_t i2 = vertex_for(pi, poly[i + 1]);
        out.push_back(i0);
        out.push_back(i1);
        out.push_back(i2);
      }
    }
  }

  QVector<std::uint32_t> indices;
  QVector<ModelSurface> surfaces;

  for (const QString& surf : surface_order) {
    const QVector<std::uint32_t> tri = indices_by_surface.value(surf);
    if (tri.isEmpty()) {
      continue;
    }
    const int first = indices.size();
    indices += tri;

    ModelSurface s;
    s.name = surf;
    s.shader = surf;
    s.first_index = first;
    s.index_count = indices.size() - first;
    surfaces.push_back(std::move(s));
  }

  if (vertices.isEmpty() || indices.isEmpty()) {
    if (error) {
      *error = "LWO contains no drawable geometry.";
    }
    return std::nullopt;
  }

  if (surfaces.isEmpty()) {
    surfaces = {ModelSurface{QString("model"), QString(), 0, static_cast<int>(indices.size())}};
  }

  LoadedModel out;
  out.format = "lwo";
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
  if (ext == "mdc") {
    return load_mdc(info.absoluteFilePath(), error);
  }
  if (ext == "md4") {
    return load_md4(info.absoluteFilePath(), error);
  }
  if (ext == "mdr") {
    return load_mdr(info.absoluteFilePath(), error);
  }
  if (ext == "skb") {
    return load_skb(info.absoluteFilePath(), error);
  }
  if (ext == "skd") {
    return load_skd(info.absoluteFilePath(), error);
  }
  if (ext == "mdm") {
    return load_mdm(info.absoluteFilePath(), error);
  }
  if (ext == "glm") {
    return load_glm(info.absoluteFilePath(), error);
  }
  if (ext == "iqm") {
    return load_iqm(info.absoluteFilePath(), error);
  }
  if (ext == "md5mesh") {
    return load_md5mesh(info.absoluteFilePath(), error);
  }
  if (ext == "tan") {
    return load_tan(info.absoluteFilePath(), error);
  }
  if (ext == "obj") {
    // Wavefront OBJ
    return load_obj(info.absoluteFilePath(), error);
  }
  if (ext == "lwo") {
    // LightWave Object (LWO2/LWO3)
    return load_lwo(info.absoluteFilePath(), error);
  }

  if (error) {
    *error = "Unsupported model format.";
  }
  return std::nullopt;
}
