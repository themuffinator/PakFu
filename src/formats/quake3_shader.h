#pragma once

#include <QColor>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

enum class Quake3WaveFunc {
  Sin,
  Square,
  Triangle,
  Sawtooth,
  InverseSawtooth,
  Noise,
};

struct Quake3WaveForm {
  Quake3WaveFunc func = Quake3WaveFunc::Sin;
  float base = 0.0f;
  float amplitude = 0.0f;
  float phase = 0.0f;
  float frequency = 0.0f;
  bool valid = false;
};

enum class Quake3BlendFactor {
  Zero,
  One,
  SrcColor,
  OneMinusSrcColor,
  DstColor,
  OneMinusDstColor,
  SrcAlpha,
  OneMinusSrcAlpha,
  DstAlpha,
  OneMinusDstAlpha,
  SrcAlphaSaturate,
};

enum class Quake3AlphaFunc {
  None,
  GT0,
  LT128,
  GE128,
};

enum class Quake3DepthFunc {
  Lequal,
  Equal,
};

enum class Quake3RgbGen {
  IdentityLighting,
  Identity,
  Entity,
  OneMinusEntity,
  Vertex,
  ExactVertex,
  OneMinusVertex,
  LightingDiffuse,
  Wave,
  Constant,
};

enum class Quake3AlphaGen {
  Identity,
  Skip,
  Entity,
  OneMinusEntity,
  Vertex,
  OneMinusVertex,
  LightingSpecular,
  Wave,
  Portal,
  Constant,
};

enum class Quake3TcGen {
  Texture,
  Lightmap,
  Environment,
  Vector,
};

enum class Quake3TcModType {
  Turbulent,
  Scale,
  Scroll,
  Stretch,
  Transform,
  Rotate,
  EntityTranslate,
};

struct Quake3TcMod {
  Quake3TcModType type = Quake3TcModType::Scroll;
  Quake3WaveForm wave;
  float matrix00 = 1.0f;
  float matrix01 = 0.0f;
  float matrix10 = 0.0f;
  float matrix11 = 1.0f;
  float translateS = 0.0f;
  float translateT = 0.0f;
  float scaleS = 1.0f;
  float scaleT = 1.0f;
  float scrollS = 0.0f;
  float scrollT = 0.0f;
  float rotateSpeed = 0.0f;
};

struct Quake3ShaderStage {
  QString map;
  bool clamp_map = false;
  bool is_lightmap = false;
  bool is_whiteimage = false;

  float anim_frequency = 0.0f;
  QStringList anim_maps;
  QString video_map;

  bool blend_enabled = false;
  Quake3BlendFactor blend_src = Quake3BlendFactor::One;
  Quake3BlendFactor blend_dst = Quake3BlendFactor::Zero;

  Quake3AlphaFunc alpha_func = Quake3AlphaFunc::None;
  Quake3DepthFunc depth_func = Quake3DepthFunc::Lequal;
  bool depth_write = false;
  bool detail = false;

  Quake3RgbGen rgb_gen = Quake3RgbGen::IdentityLighting;
  Quake3WaveForm rgb_wave;
  QColor rgb_constant = QColor::fromRgbF(1.0f, 1.0f, 1.0f, 1.0f);
  bool rgb_gen_explicit = false;

  Quake3AlphaGen alpha_gen = Quake3AlphaGen::Identity;
  Quake3WaveForm alpha_wave;
  float alpha_constant = 1.0f;
  float portal_range = 256.0f;
  bool alpha_gen_explicit = false;

  Quake3TcGen tc_gen = Quake3TcGen::Texture;
  QVector3D tc_gen_vector_s = QVector3D(1.0f, 0.0f, 0.0f);
  QVector3D tc_gen_vector_t = QVector3D(0.0f, 1.0f, 0.0f);
  QVector<Quake3TcMod> tc_mods;
};

enum class Quake3DeformType {
  Wave,
  Normal,
  Move,
  Bulge,
  ProjectionShadow,
  AutoSprite,
  AutoSprite2,
  Text,
};

struct Quake3ShaderDeform {
  Quake3DeformType type = Quake3DeformType::Wave;
  Quake3WaveForm wave;
  float spread = 0.0f;
  QVector3D move_vector = QVector3D(0.0f, 0.0f, 0.0f);
  float bulge_width = 0.0f;
  float bulge_height = 0.0f;
  float bulge_speed = 0.0f;
  int text_index = 0;
};

struct Quake3ShaderBlock {
  QString name;
  QString script_text;
  int start_offset = -1;
  int end_offset = -1;

  QString cull_mode;
  QString sort_value;
  QStringList surface_parms;
  bool no_draw = false;

  QVector<Quake3ShaderDeform> deforms;
  QVector<Quake3ShaderStage> stages;
};

struct Quake3ShaderDocument {
  QVector<Quake3ShaderBlock> shaders;
  QStringList warnings;
};

[[nodiscard]] bool parse_quake3_shader_text(const QString& text, Quake3ShaderDocument* out, QString* error = nullptr);
[[nodiscard]] QSet<QString> collect_quake3_shader_texture_refs(const Quake3ShaderBlock& shader);
[[nodiscard]] QString join_quake3_shader_blocks_text(const Quake3ShaderDocument& doc, const QVector<int>& indices);
[[nodiscard]] QString append_quake3_shader_blocks_text(const QString& base_text, const Quake3ShaderDocument& blocks_to_append);

