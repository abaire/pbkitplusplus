#ifndef PBKITPLUSPLUS__VERTEX_BUFFER_H_
#define PBKITPLUSPLUS__VERTEX_BUFFER_H_

#include <cstdint>
#include <vector>

#include "xbox_math_types.h"

using namespace XboxMath;

namespace PBKitPlusPlus {

#define TO_BGRA(float_vals)                                                                      \
  (((uint32_t)((float_vals)[3] * 255.0f) << 24) + ((uint32_t)((float_vals)[0] * 255.0f) << 16) + \
   ((uint32_t)((float_vals)[1] * 255.0f) << 8) + ((uint32_t)((float_vals)[2] * 255.0f)))

#pragma pack(1)
typedef struct Vertex {
  float pos[4];
  float weight[4];
  float normal[4];
  float diffuse[4];
  float specular[4];
  float fog_coord[4];
  float point_size[4];
  float back_diffuse[4];
  float back_specular[4];
  float texcoord0[4];
  float texcoord1[4];
  float texcoord2[4];
  float texcoord3[4];

  inline void SetPosition(const float* value, uint32_t size = 3) { memcpy(pos, value, size * sizeof(float)); }

  inline void SetPosition(float x, float y, float z, float w = 1.0f) {
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    pos[3] = w;
  }

  inline void SetWeight(const float* value, uint32_t size = 1) { memcpy(weight, value, size * sizeof(float)); }
  inline void SetWeight(float w) { weight[0] = w; }
  inline void SetWeight(float w1, float w2) {
    weight[0] = w1;
    weight[1] = w2;
  }
  inline void SetWeight(float w1, float w2, float w3) {
    weight[0] = w1;
    weight[1] = w2;
    weight[2] = w3;
  }
  inline void SetWeight(float w1, float w2, float w3, float w4) {
    weight[0] = w1;
    weight[1] = w2;
    weight[2] = w3;
    weight[3] = w4;
  }

  inline void SetNormal(const float* value, uint32_t size = 3) { memcpy(normal, value, size * sizeof(float)); }

  inline void SetNormal(float x, float y, float z) {
    normal[0] = x;
    normal[1] = y;
    normal[2] = z;
  }

  inline void SetDiffuse(const float* value, uint32_t size = 4) { memcpy(diffuse, value, size * sizeof(float)); }

  inline void SetDiffuse(float r, float g, float b, float a = 1.0f) {
    diffuse[0] = r;
    diffuse[1] = g;
    diffuse[2] = b;
    diffuse[3] = a;
  }

  inline void SetSpecular(const float* value, uint32_t size = 4) { memcpy(specular, value, size * sizeof(float)); }

  inline void SetSpecular(float r, float g, float b, float a = 1.0f) {
    specular[0] = r;
    specular[1] = g;
    specular[2] = b;
    specular[3] = a;
  }

  inline void SetFogCoord(const float* value, uint32_t size = 1) { memcpy(fog_coord, value, size * sizeof(float)); }
  inline void SetFogCoord(float v) { fog_coord[0] = v; }

  inline void SetPointSize(const float* value, uint32_t size = 1) { memcpy(point_size, value, size * sizeof(float)); }
  inline void SetPointSize(float v) { point_size[0] = v; }

  inline void SetBackDiffuse(const float* value, uint32_t size = 4) {
    memcpy(back_diffuse, value, size * sizeof(float));
  }

  inline void SetBackDiffuse(float r, float g, float b, float a = 1.0f) {
    back_diffuse[0] = r;
    back_diffuse[1] = g;
    back_diffuse[2] = b;
    back_diffuse[3] = a;
  }

  inline void SetBackSpecular(const float* value, uint32_t size = 4) {
    memcpy(back_specular, value, size * sizeof(float));
  }

  inline void SetBackSpecular(float r, float g, float b, float a = 1.0f) {
    back_specular[0] = r;
    back_specular[1] = g;
    back_specular[2] = b;
    back_specular[3] = a;
  }

  inline void SetTexCoord0(const float* value, uint32_t size = 2) { memcpy(texcoord0, value, size * sizeof(float)); }

  inline void SetTexCoord0(const float u, const float v) {
    texcoord0[0] = u;
    texcoord0[1] = v;
  }

  inline void SetTexCoord0(const float u, const float v, const float s, const float q) {
    texcoord0[0] = u;
    texcoord0[1] = v;
    texcoord0[2] = s;
    texcoord0[3] = q;
  }

  inline void SetTexCoord1(const float* value, uint32_t size = 2) { memcpy(texcoord1, value, size * sizeof(float)); }

  inline void SetTexCoord1(const float u, const float v) {
    texcoord1[0] = u;
    texcoord1[1] = v;
  }

  inline void SetTexCoord1(const float u, const float v, const float s, const float q) {
    texcoord1[0] = u;
    texcoord1[1] = v;
    texcoord1[2] = s;
    texcoord1[3] = q;
  }

  inline void SetTexCoord2(const float* value, uint32_t size = 2) { memcpy(texcoord2, value, size * sizeof(float)); }

  inline void SetTexCoord2(const float u, const float v) {
    texcoord2[0] = u;
    texcoord2[1] = v;
  }

  inline void SetTexCoord2(const float u, const float v, const float s, const float q) {
    texcoord2[0] = u;
    texcoord2[1] = v;
    texcoord2[2] = s;
    texcoord2[3] = q;
  }

  inline void SetTexCoord3(const float* value, uint32_t size = 2) { memcpy(texcoord3, value, size * sizeof(float)); }

  inline void SetTexCoord3(const float u, const float v) {
    texcoord3[0] = u;
    texcoord3[1] = v;
  }

  inline void SetTexCoord3(const float u, const float v, const float s, const float q) {
    texcoord3[0] = u;
    texcoord3[1] = v;
    texcoord3[2] = s;
    texcoord3[3] = q;
  }

  inline void SetDiffuseGrey(float val) { SetDiffuse(val, val, val); }

  void SetDiffuseGrey(float val, float alpha) { SetDiffuse(val, val, val, alpha); }

  inline void SetSpecularGrey(float val) { SetSpecular(val, val, val); }

  void SetSpecularGrey(float val, float alpha) { SetSpecular(val, val, val, alpha); }

  [[nodiscard]] uint32_t GetDiffuseARGB() const { return TO_BGRA(diffuse); }

  [[nodiscard]] uint32_t GetSpecularARGB() const { return TO_BGRA(specular); }

  void Translate(float x, float y, float z, float w);
} Vertex;
#pragma pack()

struct Color {
  Color() = default;
  Color(float red, float green, float blue, float alpha = 1.0f) : r(red), g(green), b(blue), a(alpha) {}

  void SetRGB(float red, float green, float blue) {
    r = red;
    g = green;
    b = blue;
  }

  void SetRGBA(float red, float green, float blue, float alpha) {
    r = red;
    g = green;
    b = blue;
    a = alpha;
  }

  void SetGrey(float val) {
    r = val;
    g = val;
    b = val;
  }

  void SetGreyA(float val, float alpha) {
    r = val;
    g = val;
    b = val;
    a = alpha;
  }

  [[nodiscard]] uint32_t AsBGRA() const {
    float vals[4] = {r, g, b, a};
    return TO_BGRA(vals);
  }

  float r{0.0f};
  float g{0.0f};
  float b{0.0};
  float a{1.0};
};

class NV2AState;

class VertexBuffer {
 public:
  explicit VertexBuffer(uint32_t num_vertices);
  ~VertexBuffer();

  // Returns a new VertexBuffer containing vertices suitable for rendering as triangles by treating the contents of this
  // buffer as a triangle strip.
  [[nodiscard]] std::shared_ptr<VertexBuffer> ConvertFromTriangleStripToTriangles() const;

  Vertex* Lock();
  void Unlock();

  [[nodiscard]] uint32_t GetNumVertices() const { return num_vertices_; }

  void SetCacheValid(bool valid = true) { cache_valid_ = valid; }
  [[nodiscard]] bool IsCacheValid() const { return cache_valid_; }

  void Linearize(float texture_width, float texture_height);

  // Defines a triangle with the give 3-element vertices.
  // Defines a triangle with the give 3-element vertices.
  void DefineTriangleCCW(uint32_t start_index, const float* one, const float* two, const float* three,
                         uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3);
  void DefineTriangleCCW(uint32_t start_index, const float* one, const float* two, const float* three,
                         const float* normal_one, const float* normal_two, const float* normal_three,
                         uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3,
                         uint32_t normal_one_size = 3, uint32_t normal_two_size = 3, uint32_t normal_three_size = 3);
  void DefineTriangleCCW(uint32_t start_index, const float* one, const float* two, const float* three,
                         const Color& one_diffuse, const Color& two_diffuse, const Color& three_diffuse,
                         uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3);
  void DefineTriangleCCW(uint32_t start_index, const float* one, const float* two, const float* three,
                         const float* normal_one, const float* normal_two, const float* normal_three,
                         const Color& diffuse_one, const Color& diffuse_two, const Color& diffuse_three,
                         uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3,
                         uint32_t normal_one_size = 3, uint32_t normal_two_size = 3, uint32_t normal_three_size = 3);
  void DefineTriangle(uint32_t start_index, const float* one, const float* two, const float* three,
                      uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3);
  void DefineTriangle(uint32_t start_index, const float* one, const float* two, const float* three,
                      const float* normal_one, const float* normal_two, const float* normal_three,
                      uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3,
                      uint32_t normal_one_size = 3, uint32_t normal_two_size = 3, uint32_t normal_three_size = 3);
  void DefineTriangle(uint32_t start_index, const float* one, const float* two, const float* three,
                      const Color& diffuse_one, const Color& diffuse_two, const Color& diffuse_three,
                      uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3);
  void DefineTriangle(uint32_t start_index, const float* one, const float* two, const float* three,
                      const float* normal_one, const float* normal_two, const float* normal_three,
                      const Color& diffuse_one, const Color& diffuse_two, const Color& diffuse_three,
                      uint32_t one_size = 3, uint32_t two_size = 3, uint32_t three_size = 3,
                      uint32_t normal_one_size = 3, uint32_t normal_two_size = 3, uint32_t normal_three_size = 3);

  // Defines a quad made of two coplanar triangles (i.e., 6 vertices suitable for rendering as triangle primitives)
  void DefineBiTriCCW(uint32_t start_index, float left, float top, float right, float bottom);
  void DefineBiTriCCW(uint32_t start_index, float left, float top, float right, float bottom, float z);
  void DefineBiTriCCW(uint32_t start_index, float left, float top, float right, float bottom, float ul_z, float ll_z,
                      float lr_z, float ur_z);
  void DefineBiTriCCW(uint32_t start_index, float left, float top, float right, float bottom, float ul_z, float ll_z,
                      float lr_z, float ur_z, const Color& ul_diffuse, const Color& ll_diffuse, const Color& lr_diffuse,
                      const Color& ur_diffuse);
  void DefineBiTriCCW(uint32_t start_index, float left, float top, float right, float bottom, float ul_z, float ll_z,
                      float lr_z, float ur_z, const Color& ul_diffuse, const Color& ll_diffuse, const Color& lr_diffuse,
                      const Color& ur_diffuse, const Color& ul_specular, const Color& ll_specular,
                      const Color& lr_specular, const Color& ur_specular);

  void DefineBiTri(uint32_t start_index, float left, float top, float right, float bottom);
  void DefineBiTri(uint32_t start_index, float left, float top, float right, float bottom, float z);
  void DefineBiTri(uint32_t start_index, float left, float top, float right, float bottom, float ul_z, float ll_z,
                   float lr_z, float ur_z);
  void DefineBiTri(uint32_t start_index, float left, float top, float right, float bottom, float ul_z, float ll_z,
                   float lr_z, float ur_z, const Color& ul_diffuse, const Color& ll_diffuse, const Color& lr_diffuse,
                   const Color& ur_diffuse);
  void DefineBiTri(uint32_t start_index, float left, float top, float right, float bottom, float ul_z, float ll_z,
                   float lr_z, float ur_z, const Color& ul_diffuse, const Color& ll_diffuse, const Color& lr_diffuse,
                   const Color& ur_diffuse, const Color& ul_specular, const Color& ll_specular,
                   const Color& lr_specular, const Color& ur_specular);

  inline void DefineBiTri(uint32_t start_index, const vector_t ul, const vector_t ll, const vector_t lr,
                          const vector_t ur, uint32_t ul_size = 3, uint32_t ll_size = 3, uint32_t lr_size = 3,
                          uint32_t ur_size = 3) {
    Color diffuse(1.0, 1.0, 1.0, 1.0);
    Color specular(1.0, 1.0, 1.0, 1.0);
    DefineBiTri(start_index, ul, ll, lr, ur, diffuse, diffuse, diffuse, diffuse, specular, specular, specular, specular,
                ul_size, ll_size, lr_size, ur_size);
  }

  inline void DefineBiTri(uint32_t start_index, const vector_t ul, const vector_t ll, const vector_t lr,
                          const vector_t ur, const Color& ul_diffuse, const Color& ll_diffuse, const Color& lr_diffuse,
                          const Color& ur_diffuse, uint32_t ul_size = 3, uint32_t ll_size = 3, uint32_t lr_size = 3,
                          uint32_t ur_size = 3) {
    Color specular(1.0, 1.0, 1.0, 1.0);
    DefineBiTri(start_index, ul, ll, lr, ur, ul_diffuse, ll_diffuse, lr_diffuse, ur_diffuse, specular, specular,
                specular, specular, ul_size, ll_size, lr_size, ur_size);
  }

  void DefineBiTri(uint32_t start_index, const vector_t ul, const vector_t ll, const vector_t lr, const vector_t ur,
                   const Color& ul_diffuse, const Color& ll_diffuse, const Color& lr_diffuse, const Color& ur_diffuse,
                   const Color& ul_specular, const Color& ll_specular, const Color& lr_specular,
                   const Color& ur_specular, uint32_t ul_size = 3, uint32_t ll_size = 3, uint32_t lr_size = 3,
                   uint32_t ur_size = 3);

  void SetDiffuse(uint32_t vertex_index, const Color& color);
  void SetSpecular(uint32_t vertex_index, const Color& color);

  inline void SetPositionIncludesW(bool enabled = true) { position_count_ = enabled ? 4 : 3; }
  inline void SetWeightCount(uint32_t val) { weight_count_ = val; }
  inline void SetNormalCount(uint32_t val) { normal_count_ = val; }
  inline void SetDiffuseCount(uint32_t val) { diffuse_count_ = val; }
  inline void SetSpecularCount(uint32_t val) { specular_count_ = val; }
  inline void SetFogCoordCount(uint32_t val) { fog_coord_count_ = val; }
  inline void SetPointSizeCount(uint32_t val) { point_size_count_ = val; }
  inline void SetBackDiffuseCount(uint32_t val) { back_diffuse_count_ = val; }
  inline void SetBackSpecularCount(uint32_t val) { back_specular_count_ = val; }
  inline void SetTexCoord0Count(uint32_t val) { tex0_coord_count_ = val; }
  inline void SetTexCoord1Count(uint32_t val) { tex1_coord_count_ = val; }
  inline void SetTexCoord2Count(uint32_t val) { tex2_coord_count_ = val; }
  inline void SetTexCoord3Count(uint32_t val) { tex3_coord_count_ = val; }

  void Translate(float x, float y, float z, float w = 0.0f);

 private:
  friend class NV2AState;

  uint32_t num_vertices_;
  Vertex* linear_vertex_buffer_ = nullptr;      // texcoords 0 to kFramebufferWidth/kFramebufferHeight
  Vertex* normalized_vertex_buffer_ = nullptr;  // texcoords normalized 0 to 1

  // Number of components in the vertex position (3 or 4).
  uint32_t position_count_ = 3;
  uint32_t weight_count_ = 1;
  uint32_t normal_count_ = 3;
  uint32_t diffuse_count_ = 4;
  uint32_t specular_count_ = 4;
  uint32_t fog_coord_count_ = 1;
  uint32_t point_size_count_ = 1;
  uint32_t back_diffuse_count_ = 4;
  uint32_t back_specular_count_ = 4;

  // Number of components in the vertex texcoord fields.
  uint32_t tex0_coord_count_ = 2;
  uint32_t tex1_coord_count_ = 2;
  uint32_t tex2_coord_count_ = 2;
  uint32_t tex3_coord_count_ = 2;

  bool cache_valid_{false};  // Indicates whether the HW should be forced to reload this buffer.
};

}  // namespace PBKitPlusPlus

#endif  // PBKITPLUSPLUS__VERTEX_BUFFER_H_
