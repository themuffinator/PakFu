#include "formats/quake3_shader.h"

#include <algorithm>
#include <cmath>

namespace {
struct Token {
  QString text;
  int start = 0;
  int end = 0;
  int line = 1;
  bool quoted = false;
};

class ShaderTokenizer {
 public:
  explicit ShaderTokenizer(const QString& text) : text_(text) {}

  [[nodiscard]] bool next(Token* out) {
    if (!out) {
      return false;
    }
    if (has_peek_) {
      *out = peek_;
      has_peek_ = false;
      return true;
    }
    Token tok;
    if (!read_token(&tok)) {
      return false;
    }
    *out = tok;
    return true;
  }

  [[nodiscard]] bool peek(Token* out) {
    if (!out) {
      return false;
    }
    if (!has_peek_) {
      if (!read_token(&peek_)) {
        return false;
      }
      has_peek_ = true;
    }
    *out = peek_;
    return true;
  }

 private:
  void skip_space_and_comments() {
    while (pos_ < text_.size()) {
      const QChar c = text_[pos_];
      if (c == '\n') {
        ++line_;
        ++pos_;
        continue;
      }
      if (c == '\r' || c == '\t' || c == ' ' || c == '\f' || c == '\v') {
        ++pos_;
        continue;
      }
      if (c == '/' && pos_ + 1 < text_.size()) {
        const QChar next = text_[pos_ + 1];
        if (next == '/') {
          pos_ += 2;
          while (pos_ < text_.size() && text_[pos_] != '\n') {
            ++pos_;
          }
          continue;
        }
        if (next == '*') {
          pos_ += 2;
          while (pos_ + 1 < text_.size()) {
            if (text_[pos_] == '\n') {
              ++line_;
            }
            if (text_[pos_] == '*' && text_[pos_ + 1] == '/') {
              pos_ += 2;
              break;
            }
            ++pos_;
          }
          continue;
        }
      }
      break;
    }
  }

  [[nodiscard]] bool read_token(Token* out) {
    skip_space_and_comments();
    if (pos_ >= text_.size()) {
      return false;
    }

    const int start = pos_;
    const int tok_line = line_;
    const QChar c = text_[pos_];

    if (c == '{' || c == '}' || c == '(' || c == ')') {
      ++pos_;
      out->text = QString(c);
      out->start = start;
      out->end = pos_;
      out->line = tok_line;
      out->quoted = false;
      return true;
    }

    if (c == '"') {
      ++pos_;
      QString content;
      content.reserve(32);
      while (pos_ < text_.size()) {
        const QChar q = text_[pos_++];
        if (q == '\n') {
          ++line_;
        }
        if (q == '"' && (pos_ < 2 || text_[pos_ - 2] != '\\')) {
          break;
        }
        content += q;
      }
      out->text = content;
      out->start = start;
      out->end = pos_;
      out->line = tok_line;
      out->quoted = true;
      return true;
    }

    QString content;
    content.reserve(32);
    while (pos_ < text_.size()) {
      const QChar q = text_[pos_];
      if (q.isSpace() || q == '{' || q == '}' || q == '(' || q == ')') {
        break;
      }
      if (q == '/' && pos_ + 1 < text_.size()) {
        const QChar next = text_[pos_ + 1];
        if (next == '/' || next == '*') {
          break;
        }
      }
      content += q;
      ++pos_;
    }

    out->text = content;
    out->start = start;
    out->end = pos_;
    out->line = tok_line;
    out->quoted = false;
    return !content.isEmpty();
  }

  QString text_;
  int pos_ = 0;
  int line_ = 1;
  bool has_peek_ = false;
  Token peek_;
};

QString normalize_tex_ref(QString ref) {
  ref = ref.trimmed();
  ref.replace('\\', '/');
  while (ref.startsWith('/')) {
    ref.remove(0, 1);
  }
  return ref.toLower();
}

QString normalize_shader_name_token(QString name) {
  name = name.trimmed();
  name.replace('\\', '/');
  for (int i = 0; i < name.size(); ++i) {
    const ushort u = name[i].unicode();
    if (u < 32 || u > 126) {
      name[i] = QChar('?');
    }
  }
  while (!name.isEmpty() && !(name[0].isLetterOrNumber() || name[0] == '_' || name[0] == '/')) {
    name.remove(0, 1);
  }
  while (name.startsWith('/')) {
    name.remove(0, 1);
  }
  return name;
}

bool parse_float_token(const Token& tok, float* out) {
  if (!out) {
    return false;
  }
  bool ok = false;
  const float v = tok.text.toFloat(&ok);
  if (!ok) {
    return false;
  }
  *out = v;
  return true;
}

bool parse_float(ShaderTokenizer& tokenizer, float* out) {
  Token tok;
  if (!tokenizer.next(&tok)) {
    return false;
  }
  return parse_float_token(tok, out);
}

bool parse_wave_func(const QString& token, Quake3WaveFunc* out) {
  if (!out) {
    return false;
  }
  const QString key = token.trimmed().toLower();
  if (key == "sin") {
    *out = Quake3WaveFunc::Sin;
    return true;
  }
  if (key == "square") {
    *out = Quake3WaveFunc::Square;
    return true;
  }
  if (key == "triangle") {
    *out = Quake3WaveFunc::Triangle;
    return true;
  }
  if (key == "sawtooth") {
    *out = Quake3WaveFunc::Sawtooth;
    return true;
  }
  if (key == "inversesawtooth") {
    *out = Quake3WaveFunc::InverseSawtooth;
    return true;
  }
  if (key == "noise") {
    *out = Quake3WaveFunc::Noise;
    return true;
  }
  return false;
}

bool parse_wave_form(ShaderTokenizer& tokenizer, Quake3WaveForm* out) {
  if (!out) {
    return false;
  }
  Token func_tok;
  if (!tokenizer.next(&func_tok)) {
    return false;
  }

  Quake3WaveFunc func = Quake3WaveFunc::Sin;
  if (!parse_wave_func(func_tok.text, &func)) {
    return false;
  }

  float base = 0.0f;
  float amp = 0.0f;
  float phase = 0.0f;
  float freq = 0.0f;
  if (!parse_float(tokenizer, &base) || !parse_float(tokenizer, &amp) || !parse_float(tokenizer, &phase) ||
      !parse_float(tokenizer, &freq)) {
    return false;
  }

  out->func = func;
  out->base = base;
  out->amplitude = amp;
  out->phase = phase;
  out->frequency = freq;
  out->valid = true;
  return true;
}

bool parse_vec3(ShaderTokenizer& tokenizer, QVector3D* out) {
  if (!out) {
    return false;
  }

  Token tok;
  if (!tokenizer.peek(&tok)) {
    return false;
  }
  if (tok.text == "(") {
    (void)tokenizer.next(&tok);
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!parse_float(tokenizer, &x) || !parse_float(tokenizer, &y) || !parse_float(tokenizer, &z)) {
    return false;
  }

  if (tokenizer.peek(&tok) && tok.text == ")") {
    (void)tokenizer.next(&tok);
  }

  *out = QVector3D(x, y, z);
  return true;
}

bool parse_blend_factor(const QString& token, bool src, Quake3BlendFactor* out) {
  if (!out) {
    return false;
  }
  const QString key = token.trimmed().toUpper();
  if (key == "GL_ONE") {
    *out = Quake3BlendFactor::One;
    return true;
  }
  if (key == "GL_ZERO") {
    *out = Quake3BlendFactor::Zero;
    return true;
  }
  if (key == "GL_SRC_COLOR" && !src) {
    *out = Quake3BlendFactor::SrcColor;
    return true;
  }
  if (key == "GL_ONE_MINUS_SRC_COLOR" && !src) {
    *out = Quake3BlendFactor::OneMinusSrcColor;
    return true;
  }
  if (key == "GL_DST_COLOR" && src) {
    *out = Quake3BlendFactor::DstColor;
    return true;
  }
  if (key == "GL_ONE_MINUS_DST_COLOR" && src) {
    *out = Quake3BlendFactor::OneMinusDstColor;
    return true;
  }
  if (key == "GL_SRC_ALPHA") {
    *out = Quake3BlendFactor::SrcAlpha;
    return true;
  }
  if (key == "GL_ONE_MINUS_SRC_ALPHA") {
    *out = Quake3BlendFactor::OneMinusSrcAlpha;
    return true;
  }
  if (key == "GL_DST_ALPHA") {
    *out = Quake3BlendFactor::DstAlpha;
    return true;
  }
  if (key == "GL_ONE_MINUS_DST_ALPHA") {
    *out = Quake3BlendFactor::OneMinusDstAlpha;
    return true;
  }
  if (key == "GL_SRC_ALPHA_SATURATE" && src) {
    *out = Quake3BlendFactor::SrcAlphaSaturate;
    return true;
  }
  return false;
}

void parse_tcmod_command(ShaderTokenizer& tokenizer, Quake3ShaderStage* stage, QStringList* warnings) {
  if (!stage) {
    return;
  }
  Token kind_tok;
  if (!tokenizer.next(&kind_tok)) {
    if (warnings) {
      warnings->push_back("tcMod: missing mode.");
    }
    return;
  }

  const QString kind = kind_tok.text.trimmed().toLower();
  Quake3TcMod mod;

  if (kind == "turb") {
    mod.type = Quake3TcModType::Turbulent;
    mod.wave.func = Quake3WaveFunc::Sin;
    mod.wave.valid = true;
    if (!parse_float(tokenizer, &mod.wave.base) || !parse_float(tokenizer, &mod.wave.amplitude) ||
        !parse_float(tokenizer, &mod.wave.phase) || !parse_float(tokenizer, &mod.wave.frequency)) {
      if (warnings) {
        warnings->push_back("tcMod turb: invalid parameters.");
      }
      return;
    }
  } else if (kind == "scale") {
    mod.type = Quake3TcModType::Scale;
    if (!parse_float(tokenizer, &mod.scaleS) || !parse_float(tokenizer, &mod.scaleT)) {
      if (warnings) {
        warnings->push_back("tcMod scale: invalid parameters.");
      }
      return;
    }
  } else if (kind == "scroll") {
    mod.type = Quake3TcModType::Scroll;
    if (!parse_float(tokenizer, &mod.scrollS) || !parse_float(tokenizer, &mod.scrollT)) {
      if (warnings) {
        warnings->push_back("tcMod scroll: invalid parameters.");
      }
      return;
    }
  } else if (kind == "stretch") {
    mod.type = Quake3TcModType::Stretch;
    if (!parse_wave_form(tokenizer, &mod.wave)) {
      if (warnings) {
        warnings->push_back("tcMod stretch: invalid waveform.");
      }
      return;
    }
  } else if (kind == "transform") {
    mod.type = Quake3TcModType::Transform;
    if (!parse_float(tokenizer, &mod.matrix00) || !parse_float(tokenizer, &mod.matrix01) ||
        !parse_float(tokenizer, &mod.matrix10) || !parse_float(tokenizer, &mod.matrix11) ||
        !parse_float(tokenizer, &mod.translateS) || !parse_float(tokenizer, &mod.translateT)) {
      if (warnings) {
        warnings->push_back("tcMod transform: invalid parameters.");
      }
      return;
    }
  } else if (kind == "rotate") {
    mod.type = Quake3TcModType::Rotate;
    if (!parse_float(tokenizer, &mod.rotateSpeed)) {
      if (warnings) {
        warnings->push_back("tcMod rotate: invalid speed.");
      }
      return;
    }
  } else if (kind == "entitytranslate") {
    mod.type = Quake3TcModType::EntityTranslate;
  } else {
    if (warnings) {
      warnings->push_back(QString("tcMod: unknown mode '%1'.").arg(kind_tok.text));
    }
    return;
  }

  stage->tc_mods.push_back(mod);
}

void finalize_stage_defaults(Quake3ShaderStage* stage) {
  if (!stage) {
    return;
  }

  // Quake III treats GL_ONE/GL_ZERO as an opaque pass (blending disabled).
  if (stage->blend_enabled && stage->blend_src == Quake3BlendFactor::One && stage->blend_dst == Quake3BlendFactor::Zero) {
    stage->blend_enabled = false;
  }

  if (!stage->rgb_gen_explicit) {
    if (!stage->blend_enabled || stage->blend_src == Quake3BlendFactor::One || stage->blend_src == Quake3BlendFactor::SrcAlpha) {
      stage->rgb_gen = Quake3RgbGen::IdentityLighting;
    } else {
      stage->rgb_gen = Quake3RgbGen::Identity;
    }
  }

  if (!stage->alpha_gen_explicit) {
    stage->alpha_gen = Quake3AlphaGen::Identity;
  }

  if (stage->alpha_gen == Quake3AlphaGen::Identity &&
      (stage->rgb_gen == Quake3RgbGen::Identity || stage->rgb_gen == Quake3RgbGen::LightingDiffuse)) {
    stage->alpha_gen = Quake3AlphaGen::Skip;
  }

  if (!stage->blend_enabled) {
    stage->depth_write = true;
  }
}

void parse_stage_block(ShaderTokenizer& tokenizer, Quake3ShaderBlock* shader, QStringList* warnings) {
  if (!shader) {
    return;
  }

  Quake3ShaderStage stage;

  Token tok;
  while (tokenizer.next(&tok)) {
    if (tok.text == "}") {
      break;
    }
    if (tok.text == "{") {
      continue;
    }

    const QString key = tok.text.trimmed().toLower();
    const int line = tok.line;

    if (key == "map") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("map: missing parameter.");
        }
        continue;
      }
      const QString map = value.text.trimmed();
      if (map.compare("$lightmap", Qt::CaseInsensitive) == 0) {
        stage.is_lightmap = true;
      } else if (map.compare("$whiteimage", Qt::CaseInsensitive) == 0) {
        stage.is_whiteimage = true;
      } else {
        stage.map = map;
      }
      continue;
    }

    if (key == "clampmap") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("clampMap: missing parameter.");
        }
        continue;
      }
      stage.clamp_map = true;
      stage.map = value.text.trimmed();
      continue;
    }

    if (key == "animmap") {
      if (!parse_float(tokenizer, &stage.anim_frequency)) {
        if (warnings) {
          warnings->push_back("animMap: missing frequency.");
        }
        continue;
      }
      Token frame;
      while (tokenizer.peek(&frame)) {
        if (frame.line != line || frame.text == "{" || frame.text == "}") {
          break;
        }
        (void)tokenizer.next(&frame);
        stage.anim_maps.push_back(frame.text.trimmed());
      }
      continue;
    }

    if (key == "videomap") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("videoMap: missing parameter.");
        }
        continue;
      }
      stage.video_map = value.text.trimmed();
      continue;
    }

    if (key == "alphafunc") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("alphaFunc: missing parameter.");
        }
        continue;
      }
      const QString v = value.text.trimmed().toUpper();
      if (v == "GT0") {
        stage.alpha_func = Quake3AlphaFunc::GT0;
      } else if (v == "LT128") {
        stage.alpha_func = Quake3AlphaFunc::LT128;
      } else if (v == "GE128") {
        stage.alpha_func = Quake3AlphaFunc::GE128;
      } else {
        stage.alpha_func = Quake3AlphaFunc::None;
      }
      continue;
    }

    if (key == "depthfunc") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("depthFunc: missing parameter.");
        }
        continue;
      }
      stage.depth_func = (value.text.compare("equal", Qt::CaseInsensitive) == 0) ? Quake3DepthFunc::Equal
                                                                                  : Quake3DepthFunc::Lequal;
      continue;
    }

    if (key == "depthwrite") {
      stage.depth_write = true;
      continue;
    }

    if (key == "detail") {
      stage.detail = true;
      continue;
    }

    if (key == "blendfunc") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("blendFunc: missing parameter.");
        }
        continue;
      }

      const QString first = value.text.trimmed().toLower();
      if (first == "add") {
        stage.blend_enabled = true;
        stage.blend_src = Quake3BlendFactor::One;
        stage.blend_dst = Quake3BlendFactor::One;
      } else if (first == "filter") {
        stage.blend_enabled = true;
        stage.blend_src = Quake3BlendFactor::DstColor;
        stage.blend_dst = Quake3BlendFactor::Zero;
      } else if (first == "blend") {
        stage.blend_enabled = true;
        stage.blend_src = Quake3BlendFactor::SrcAlpha;
        stage.blend_dst = Quake3BlendFactor::OneMinusSrcAlpha;
      } else {
        Quake3BlendFactor src = Quake3BlendFactor::One;
        Quake3BlendFactor dst = Quake3BlendFactor::Zero;
        const bool src_ok = parse_blend_factor(value.text, true, &src);

        Token value2;
        const bool has_dst = tokenizer.next(&value2);
        const bool dst_ok = has_dst && parse_blend_factor(value2.text, false, &dst);

        stage.blend_enabled = true;
        stage.blend_src = src_ok ? src : Quake3BlendFactor::One;
        stage.blend_dst = dst_ok ? dst : Quake3BlendFactor::Zero;
      }
      continue;
    }

    if (key == "rgbgen") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("rgbGen: missing parameter.");
        }
        continue;
      }

      stage.rgb_gen_explicit = true;
      const QString v = value.text.trimmed().toLower();
      if (v == "wave") {
        if (!parse_wave_form(tokenizer, &stage.rgb_wave)) {
          if (warnings) {
            warnings->push_back("rgbGen wave: invalid waveform.");
          }
        }
        stage.rgb_gen = Quake3RgbGen::Wave;
      } else if (v == "const") {
        QVector3D color(1.0f, 1.0f, 1.0f);
        if (parse_vec3(tokenizer, &color)) {
          stage.rgb_constant = QColor::fromRgbF(std::clamp(color.x(), 0.0f, 1.0f),
                                                std::clamp(color.y(), 0.0f, 1.0f),
                                                std::clamp(color.z(), 0.0f, 1.0f),
                                                stage.rgb_constant.alphaF());
        }
        stage.rgb_gen = Quake3RgbGen::Constant;
      } else if (v == "identity") {
        stage.rgb_gen = Quake3RgbGen::Identity;
      } else if (v == "identitylighting") {
        stage.rgb_gen = Quake3RgbGen::IdentityLighting;
      } else if (v == "entity") {
        stage.rgb_gen = Quake3RgbGen::Entity;
      } else if (v == "oneminusentity") {
        stage.rgb_gen = Quake3RgbGen::OneMinusEntity;
      } else if (v == "vertex") {
        stage.rgb_gen = Quake3RgbGen::Vertex;
      } else if (v == "exactvertex") {
        stage.rgb_gen = Quake3RgbGen::ExactVertex;
      } else if (v == "lightingdiffuse") {
        stage.rgb_gen = Quake3RgbGen::LightingDiffuse;
      } else if (v == "oneminusvertex") {
        stage.rgb_gen = Quake3RgbGen::OneMinusVertex;
      } else {
        stage.rgb_gen = Quake3RgbGen::IdentityLighting;
      }
      continue;
    }

    if (key == "alphagen") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("alphaGen: missing parameter.");
        }
        continue;
      }

      stage.alpha_gen_explicit = true;
      const QString v = value.text.trimmed().toLower();
      if (v == "wave") {
        if (!parse_wave_form(tokenizer, &stage.alpha_wave)) {
          if (warnings) {
            warnings->push_back("alphaGen wave: invalid waveform.");
          }
        }
        stage.alpha_gen = Quake3AlphaGen::Wave;
      } else if (v == "const") {
        float a = 1.0f;
        if (parse_float(tokenizer, &a)) {
          stage.alpha_constant = std::clamp(a, 0.0f, 1.0f);
        }
        stage.alpha_gen = Quake3AlphaGen::Constant;
      } else if (v == "identity") {
        stage.alpha_gen = Quake3AlphaGen::Identity;
      } else if (v == "entity") {
        stage.alpha_gen = Quake3AlphaGen::Entity;
      } else if (v == "oneminusentity") {
        stage.alpha_gen = Quake3AlphaGen::OneMinusEntity;
      } else if (v == "vertex") {
        stage.alpha_gen = Quake3AlphaGen::Vertex;
      } else if (v == "oneminusvertex") {
        stage.alpha_gen = Quake3AlphaGen::OneMinusVertex;
      } else if (v == "lightingspecular") {
        stage.alpha_gen = Quake3AlphaGen::LightingSpecular;
      } else if (v == "portal") {
        stage.alpha_gen = Quake3AlphaGen::Portal;
        float range = stage.portal_range;
        if (parse_float(tokenizer, &range)) {
          stage.portal_range = std::max(1.0f, range);
        }
      } else {
        stage.alpha_gen = Quake3AlphaGen::Identity;
      }
      continue;
    }

    if (key == "texgen" || key == "tcgen") {
      Token value;
      if (!tokenizer.next(&value)) {
        if (warnings) {
          warnings->push_back("tcGen: missing parameter.");
        }
        continue;
      }
      const QString v = value.text.trimmed().toLower();
      if (v == "environment") {
        stage.tc_gen = Quake3TcGen::Environment;
      } else if (v == "lightmap") {
        stage.tc_gen = Quake3TcGen::Lightmap;
      } else if (v == "texture" || v == "base") {
        stage.tc_gen = Quake3TcGen::Texture;
      } else if (v == "vector") {
        QVector3D s(1.0f, 0.0f, 0.0f);
        QVector3D t(0.0f, 1.0f, 0.0f);
        if (parse_vec3(tokenizer, &s) && parse_vec3(tokenizer, &t)) {
          stage.tc_gen = Quake3TcGen::Vector;
          stage.tc_gen_vector_s = s;
          stage.tc_gen_vector_t = t;
        }
      }
      continue;
    }

    if (key == "tcmod") {
      parse_tcmod_command(tokenizer, &stage, warnings);
      continue;
    }

    // Unknown stage key: skip same-line trailing params so next line can continue parsing.
    Token line_tok;
    while (tokenizer.peek(&line_tok) && line_tok.line == line && line_tok.text != "{" && line_tok.text != "}") {
      (void)tokenizer.next(&line_tok);
    }
  }

  finalize_stage_defaults(&stage);
  shader->stages.push_back(stage);
}

void parse_deform(ShaderTokenizer& tokenizer, Quake3ShaderBlock* shader, QStringList* warnings) {
  if (!shader) {
    return;
  }
  Token kind_tok;
  if (!tokenizer.next(&kind_tok)) {
    if (warnings) {
      warnings->push_back("deformVertexes: missing mode.");
    }
    return;
  }

  const QString kind = kind_tok.text.trimmed().toLower();
  Quake3ShaderDeform deform;

  if (kind == "projectionshadow") {
    deform.type = Quake3DeformType::ProjectionShadow;
    shader->deforms.push_back(deform);
    return;
  }
  if (kind == "autosprite") {
    deform.type = Quake3DeformType::AutoSprite;
    shader->deforms.push_back(deform);
    return;
  }
  if (kind == "autosprite2") {
    deform.type = Quake3DeformType::AutoSprite2;
    shader->deforms.push_back(deform);
    return;
  }
  if (kind.startsWith("text")) {
    bool ok = false;
    const int idx = kind.mid(4).toInt(&ok);
    deform.type = Quake3DeformType::Text;
    deform.text_index = ok ? std::clamp(idx, 0, 7) : 0;
    shader->deforms.push_back(deform);
    return;
  }
  if (kind == "bulge") {
    deform.type = Quake3DeformType::Bulge;
    if (!parse_float(tokenizer, &deform.bulge_width) || !parse_float(tokenizer, &deform.bulge_height) ||
        !parse_float(tokenizer, &deform.bulge_speed)) {
      if (warnings) {
        warnings->push_back("deformVertexes bulge: invalid parameters.");
      }
      return;
    }
    shader->deforms.push_back(deform);
    return;
  }
  if (kind == "wave") {
    deform.type = Quake3DeformType::Wave;
    float spread_div = 0.0f;
    if (!parse_float(tokenizer, &spread_div)) {
      if (warnings) {
        warnings->push_back("deformVertexes wave: missing spread.");
      }
      return;
    }
    deform.spread = (std::abs(spread_div) > 0.0001f) ? (1.0f / spread_div) : 100.0f;
    if (!parse_wave_form(tokenizer, &deform.wave)) {
      if (warnings) {
        warnings->push_back("deformVertexes wave: invalid waveform.");
      }
      return;
    }
    shader->deforms.push_back(deform);
    return;
  }
  if (kind == "normal") {
    deform.type = Quake3DeformType::Normal;
    deform.wave.valid = true;
    if (!parse_float(tokenizer, &deform.wave.amplitude) || !parse_float(tokenizer, &deform.wave.frequency)) {
      if (warnings) {
        warnings->push_back("deformVertexes normal: invalid parameters.");
      }
      return;
    }
    shader->deforms.push_back(deform);
    return;
  }
  if (kind == "move") {
    deform.type = Quake3DeformType::Move;
    if (!parse_vec3(tokenizer, &deform.move_vector) || !parse_wave_form(tokenizer, &deform.wave)) {
      if (warnings) {
        warnings->push_back("deformVertexes move: invalid parameters.");
      }
      return;
    }
    shader->deforms.push_back(deform);
    return;
  }

  if (warnings) {
    warnings->push_back(QString("deformVertexes: unknown mode '%1'.").arg(kind_tok.text));
  }
}

void parse_shader_block_body(const QString& body, Quake3ShaderBlock* shader, QStringList* warnings) {
  if (!shader) {
    return;
  }

  ShaderTokenizer tokenizer(body);
  Token tok;
  while (tokenizer.next(&tok)) {
    if (tok.text == "}" || tok.text == "{") {
      if (tok.text == "{") {
        parse_stage_block(tokenizer, shader, warnings);
      }
      continue;
    }

    const QString key = tok.text.trimmed().toLower();
    if (key == "deformvertexes") {
      parse_deform(tokenizer, shader, warnings);
      continue;
    }
    if (key == "surfaceparm") {
      Token parm;
      if (tokenizer.next(&parm)) {
        const QString p = parm.text.trimmed().toLower();
        if (!p.isEmpty()) {
          shader->surface_parms.push_back(p);
          if (p == "nodraw") {
            shader->no_draw = true;
          }
        }
      }
      continue;
    }
    if (key == "cull") {
      Token mode;
      if (tokenizer.next(&mode)) {
        shader->cull_mode = mode.text.trimmed().toLower();
      }
      continue;
    }
    if (key == "sort") {
      Token value;
      if (tokenizer.next(&value)) {
        shader->sort_value = value.text.trimmed();
      }
      continue;
    }
  }
}
}  // namespace

bool parse_quake3_shader_text(const QString& text, Quake3ShaderDocument* out, QString* error) {
  if (error) {
    error->clear();
  }
  if (!out) {
    if (error) {
      *error = "Invalid output container.";
    }
    return false;
  }

  out->shaders.clear();
  out->warnings.clear();

  ShaderTokenizer tokenizer(text);
  Token tok;
  while (tokenizer.next(&tok)) {
    if (tok.text == "{" || tok.text == "}") {
      continue;
    }

    Quake3ShaderBlock shader;
    shader.name = normalize_shader_name_token(tok.text);
    if (shader.name.isEmpty()) {
      continue;
    }
    shader.start_offset = tok.start;

    Token open_brace;
    if (!tokenizer.next(&open_brace) || open_brace.text != "{") {
      out->warnings.push_back(QString("Shader '%1' is missing an opening '{'.").arg(shader.name));
      continue;
    }

    int depth = 1;
    int close_start = -1;
    int block_end = -1;

    Token block_tok;
    while (tokenizer.next(&block_tok)) {
      if (block_tok.text == "{") {
        ++depth;
        continue;
      }
      if (block_tok.text == "}") {
        --depth;
        if (depth == 0) {
          close_start = block_tok.start;
          block_end = block_tok.end;
          break;
        }
      }
    }

    if (depth != 0 || close_start < open_brace.end || block_end <= shader.start_offset) {
      if (error) {
        *error = QString("Unterminated shader block for '%1'.").arg(shader.name);
      }
      return false;
    }

    shader.end_offset = block_end;
    shader.script_text = text.mid(shader.start_offset, shader.end_offset - shader.start_offset).trimmed();

    const QString body = text.mid(open_brace.end, close_start - open_brace.end);
    parse_shader_block_body(body, &shader, &out->warnings);

    out->shaders.push_back(std::move(shader));
  }

  if (out->shaders.isEmpty() && !text.trimmed().isEmpty()) {
    if (error) {
      *error = "No shader blocks were found in this file.";
    }
    return false;
  }

  return true;
}

QSet<QString> collect_quake3_shader_texture_refs(const Quake3ShaderBlock& shader) {
  QSet<QString> refs;
  for (const Quake3ShaderStage& stage : shader.stages) {
    const QString map = normalize_tex_ref(stage.map);
    if (!map.isEmpty() && !map.startsWith('$')) {
      refs.insert(map);
    }
    for (const QString& anim : stage.anim_maps) {
      const QString ref = normalize_tex_ref(anim);
      if (!ref.isEmpty() && !ref.startsWith('$')) {
        refs.insert(ref);
      }
    }
  }
  return refs;
}

QString join_quake3_shader_blocks_text(const Quake3ShaderDocument& doc, const QVector<int>& indices) {
  QStringList parts;
  parts.reserve(indices.size());
  for (const int idx : indices) {
    if (idx < 0 || idx >= doc.shaders.size()) {
      continue;
    }
    const QString block = doc.shaders[idx].script_text.trimmed();
    if (!block.isEmpty()) {
      parts.push_back(block);
    }
  }
  if (parts.isEmpty()) {
    return {};
  }
  return parts.join("\n\n") + "\n";
}

QString append_quake3_shader_blocks_text(const QString& base_text, const Quake3ShaderDocument& blocks_to_append) {
  QStringList parts;
  parts.reserve(blocks_to_append.shaders.size());
  for (const Quake3ShaderBlock& block : blocks_to_append.shaders) {
    const QString s = block.script_text.trimmed();
    if (!s.isEmpty()) {
      parts.push_back(s);
    }
  }
  if (parts.isEmpty()) {
    return base_text;
  }

  QString out = base_text;
  const QString append_chunk = parts.join("\n\n");
  if (out.trimmed().isEmpty()) {
    out = append_chunk + "\n";
    return out;
  }

  if (!out.endsWith('\n')) {
    out += '\n';
  }
  if (!out.endsWith("\n\n")) {
    out += '\n';
  }
  out += append_chunk;
  if (!out.endsWith('\n')) {
    out += '\n';
  }
  return out;
}
