#include "gfx/IRenderBackend.h"

#include <cstring>
#include <map>
#include <stdexcept>

#if defined(_WIN32)
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#error "Backend OpenGL en Windows: pendiente de cargador glad (ver ROADMAP.md)"
#endif

namespace vj::gfx {
namespace {

const char* kVertex = R"(#version 410 core
uniform mat3 uMatrix;
uniform bool uFlipV;
out vec2 vUv;
out vec2 vPos;
void main() {
  vec2 p = vec2((gl_VertexID & 1) * 2.0 - 1.0, (gl_VertexID & 2) - 1.0);
  vUv = uFlipV ? p * 0.5 + 0.5 : vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  vec3 q = uMatrix * vec3(p, 1.0);
  gl_Position = vec4(q.xy, 0.0, 1.0);
  vPos = q.xy * 0.5 + 0.5;
})";

const char* kPresent = R"(#version 410 core
uniform sampler2D uTex0;
uniform vec4 uParams[4];
in vec2 vUv;
out vec4 color;
void main() {
  color = vec4(texture(uTex0, vUv).rgb * uParams[0].x, 1.0);
})";

const char* kMeshVertex = R"(#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec2 aSlice;
out vec2 vUv;
out vec2 vSlice;
void main() {
  vUv = aUv;
  vSlice = aSlice;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = GL_FALSE;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    throw std::runtime_error(std::string("shader: ") + log);
  }
  return s;
}

void linear(GLenum target) {
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

class OpenGL41Backend final : public IRenderBackend {
 public:
  OpenGL41Backend() {
#if defined(_WIN32)
    if (!gladLoadGL(glfwGetProcAddress)) throw std::runtime_error("no se pudo cargar OpenGL");
#endif
    glGenVertexArrays(1, &vao_);
    glGenVertexArrays(1, &meshVao_);
    glGenBuffers(1, &meshVbo_);
    glBindVertexArray(meshVao_);
    glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
    const GLsizei stride = 6 * sizeof(float);
    for (GLuint a = 0; a < 3; ++a) {
      glEnableVertexAttribArray(a);
      glVertexAttribPointer(a, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(static_cast<uintptr_t>(a * 2 * sizeof(float))));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    present_ = createProgram(kPresent);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
  }

  ~OpenGL41Backend() override {
    for (auto& [id, _] : textures_) glDeleteTextures(1, &id);
    for (auto& [id, t] : targets_) {
      glDeleteTextures(1, &t.texture);
      glDeleteFramebuffers(1, &t.fbo);
    }
    for (auto& [id, _] : programs_) glDeleteProgram(id);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &meshVao_);
    glDeleteBuffers(1, &meshVbo_);
  }

  std::string name() const override { return "OpenGL 4.1"; }

  TextureId uploadTexture(TextureId id, int width, int height, PixelFormat format, int stride,
                          const void* pixels) override {
    if (id == kNoTexture) {
      glGenTextures(1, &id);
      glBindTexture(GL_TEXTURE_2D, id);
      linear(GL_TEXTURE_2D);
      textures_[id] = {};
    }
    glBindTexture(GL_TEXTURE_2D, id);
    const GLenum glFormat = format == PixelFormat::BGRA8 ? GL_BGRA : GL_RGBA;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    Size& size = textures_[id];
    if (size.w == width && size.h == height) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, glFormat, GL_UNSIGNED_BYTE, pixels);
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, glFormat, GL_UNSIGNED_BYTE, pixels);
      size = {width, height};
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return id;
  }

  void destroyTexture(TextureId id) override {
    if (textures_.erase(id)) glDeleteTextures(1, &id);
  }

  TargetId createTarget(int width, int height, bool deep) override {
    Target t;
    glGenFramebuffers(1, &t.fbo);
    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, deep ? GL_RGBA16F : GL_RGBA8, width, height, 0, GL_RGBA, deep ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    linear(GL_TEXTURE_2D);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("destino de render OpenGL incompleto");
    t.w = width;
    t.h = height;
    targets_[t.fbo] = t;
    return t.fbo;
  }

  void destroyTarget(TargetId id) override {
    auto it = targets_.find(id);
    if (it == targets_.end()) return;
    glDeleteTextures(1, &it->second.texture);
    glDeleteFramebuffers(1, &it->second.fbo);
    targets_.erase(it);
  }

  TextureId targetTexture(TargetId id) override { return targets_.at(id).texture; }

  ProgramId createProgram(const std::string& fragment) override { return link(kVertex, fragment); }
  ProgramId createMeshProgram(const std::string& fragment) override { return link(kMeshVertex, fragment); }

  ProgramId link(const char* vertex, const std::string& fragment) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment.c_str());
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) throw std::runtime_error("no se pudo enlazar el programa OpenGL");
    Uniforms u;
    u.matrix = glGetUniformLocation(p, "uMatrix");
    u.flip = glGetUniformLocation(p, "uFlipV");
    u.params = glGetUniformLocation(p, "uParams");
    u.mode = glGetUniformLocation(p, "uMode");
    for (int i = 0; i < 3; ++i) u.tex[i] = glGetUniformLocation(p, ("uTex" + std::to_string(i)).c_str());
    programs_[p] = u;
    return p;
  }

  void beginPass(TargetId target, bool clear) override {
    const Target& t = targets_.at(target);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.w, t.h);
    if (clear) {
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
    }
  }

  void draw(const DrawCall& c) override {
    const Uniforms& u = programs_.at(c.program);
    glUseProgram(c.program);
    glBindVertexArray(vao_);
    for (int i = 0; i < 3; ++i) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, c.textures[i]);
      if (u.tex[i] >= 0) glUniform1i(u.tex[i], i);
    }
    if (u.matrix >= 0) glUniformMatrix3fv(u.matrix, 1, GL_FALSE, c.matrix.data());
    if (u.flip >= 0) glUniform1i(u.flip, c.flipV ? 1 : 0);
    if (u.params >= 0) glUniform4fv(u.params, 4, c.params.data());
    if (u.mode >= 0) glUniform1i(u.mode, c.mode);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }

  void drawMesh(const MeshCall& c) override {
    const Uniforms& u = programs_.at(c.program);
    glUseProgram(c.program);
    glBindVertexArray(meshVao_);
    glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
    glBufferData(GL_ARRAY_BUFFER, c.vertexCount * 6 * static_cast<GLsizeiptr>(sizeof(float)), c.vertices, GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, c.texture);
    if (u.tex[0] >= 0) glUniform1i(u.tex[0], 0);
    if (u.tex[1] >= 0) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, c.texture2);
      glUniform1i(u.tex[1], 1);
      glActiveTexture(GL_TEXTURE0);
    }
    if (u.params >= 0) glUniform4fv(u.params, 4, c.params.data());
    if (c.blendOver) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (c.colorMask >= 0) {
      // Las máscaras escriben un gris fijo: sin blending y con el color como constante.
      glEnable(GL_BLEND);
      glBlendColor(c.colorMask, c.colorMask, c.colorMask, 1);
      glBlendFunc(GL_CONSTANT_COLOR, GL_ZERO);
    }
    glDrawArrays(GL_TRIANGLES, 0, c.vertexCount);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  void clearTarget(TargetId target, float r, float g, float b, float a) override {
    glBindFramebuffer(GL_FRAMEBUFFER, targets_.at(target).fbo);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  void setPresentContext(int id) override {
    auto it = presentVaos_.find(id);
    if (it == presentVaos_.end()) {
      GLuint v = 0;
      glGenVertexArrays(1, &v);
      it = presentVaos_.emplace(id, v).first;
    }
    presentVao_ = it->second;
  }

  void releasePresentContext(int id) override {
    auto it = presentVaos_.find(id);
    if (it == presentVaos_.end()) return;
    glDeleteVertexArrays(1, &it->second);
    presentVaos_.erase(it);
    presentVao_ = 0;
  }

  bool runExternal(TargetId output, TargetId input, const std::function<void(uint32_t, uint32_t)>& fn) override {
    const Target& out = targets_.at(output);
    const Target& in = targets_.at(input);
    const auto defaults = [] {
      glUseProgram(0);
      glBindVertexArray(0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDisable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_CULL_FACE);
    };
    defaults();
    glBindFramebuffer(GL_FRAMEBUFFER, out.fbo);
    glViewport(0, 0, out.w, out.h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    fn(in.texture, out.fbo);
    // Un plugin puede dejar el contexto como quiera: esto no debe afectar a la pasada siguiente.
    defaults();
    glBlendFunc(GL_ONE, GL_ZERO);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return true;
  }

  void present(TextureId texture, int width, int height, const DrawParams& p) override {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    DrawCall c;
    c.program = present_;
    c.textures[0] = texture;
    c.matrix = {p.scaleX, 0, 0, 0, p.scaleY, 0, 0, 0, 1};
    c.params[0] = p.opacity;
    c.flipV = true;
    const GLuint main = vao_;
    if (presentVao_) vao_ = presentVao_;
    draw(c);
    vao_ = main;
    glFlush();
  }

  std::vector<uint8_t> readback(TargetId target, int width, int height) override {
    std::vector<uint8_t> px(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, targets_.at(target).fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const size_t row = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < height / 2; ++y) {
      uint8_t* a = px.data() + y * row;
      uint8_t* b = px.data() + (height - 1 - y) * row;
      std::memcpy(tmp.data(), a, row);
      std::memcpy(a, b, row);
      std::memcpy(b, tmp.data(), row);
    }
    return px;
  }

 private:
  struct Size {
    int w = 0, h = 0;
  };
  struct Target {
    GLuint fbo = 0, texture = 0;
    int w = 0, h = 0;
  };
  struct Uniforms {
    GLint matrix = -1, flip = -1, params = -1, mode = -1;
    GLint tex[3] = {-1, -1, -1};
  };

  GLuint vao_ = 0, meshVao_ = 0, meshVbo_ = 0, presentVao_ = 0;
  std::map<int, GLuint> presentVaos_;
  ProgramId present_ = 0;
  std::map<GLuint, Size> textures_;
  std::map<GLuint, Target> targets_;
  std::map<GLuint, Uniforms> programs_;
};

}  // namespace

std::unique_ptr<IRenderBackend> makeOpenGL41Backend() { return std::make_unique<OpenGL41Backend>(); }

}  // namespace vj::gfx
