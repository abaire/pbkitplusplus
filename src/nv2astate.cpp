#include "nv2astate.h"

#include <SDL.h>
#include <SDL_image.h>
#include <strings.h>

#include <cmath>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#include <windows.h>
#pragma clang diagnostic pop

#include <xboxkrnl/xboxkrnl.h>

#include <algorithm>
#include <utility>

#include "nxdk_ext.h"
#include "pushbuffer.h"
#include "shaders/vertex_shader_program.h"
#include "texture_generator.h"
#include "vertex_buffer.h"
#include "xbox_math_d3d.h"
#include "xbox_math_matrix.h"
#include "xbox_math_types.h"
#include "xbox_math_util.h"

using namespace XboxMath;

namespace PBKitPlusPlus {

static void SetVertexAttribute(uint32_t index, uint32_t format, uint32_t size, uint32_t stride, const void *data);
static void ClearVertexAttribute(uint32_t index);
static void GetCompositeMatrix(matrix4_t &result, const matrix4_t &model_view, const matrix4_t &projection);

// From pbkit.c, DMA_A is set to channel 3 by default
// NV097_SET_CONTEXT_DMA_A == NV20_TCL_PRIMITIVE_3D_SET_OBJECT1
static constexpr uint32_t kDefaultDMAChannelA = 3;
// From pbkit.c, DMA_COLOR is set to channel 9 by default
// NV097_SET_CONTEXT_DMA_COLOR == NV20_TCL_PRIMITIVE_3D_SET_OBJECT3
const uint32_t kDefaultDMAColorChannel = 9;

NV2AState::NV2AState(uint32_t framebuffer_width, uint32_t framebuffer_height, uint32_t max_texture_width,
                     uint32_t max_texture_height, uint32_t max_texture_depth)
    : framebuffer_width_(framebuffer_width),
      framebuffer_height_(framebuffer_height),
      max_texture_width_(max_texture_width),
      max_texture_height_(max_texture_height),
      max_texture_depth_(max_texture_depth) {
  Pushbuffer::Initialize();

  // allocate texture memory buffer large enough for all types
  uint32_t stride = max_texture_width_ * 4;
  max_single_texture_size_ = stride * max_texture_height * max_texture_depth;

  static constexpr uint32_t kMaxPaletteSize = 256 * 4;
  uint32_t palette_size = kMaxPaletteSize * 4;

  static constexpr uint32_t kMaxTextures = 4;
  texture_memory_size_ = max_single_texture_size_ * kMaxTextures;
  uint32_t total_size = texture_memory_size_ + palette_size;

  texture_memory_ = static_cast<uint8_t *>(
      MmAllocateContiguousMemoryEx(total_size, 0, MAXRAM, 0, PAGE_WRITECOMBINE | PAGE_READWRITE));
  PBKPP_ASSERT(texture_memory_ && "Failed to allocate texture memory.");

  texture_palette_memory_ = texture_memory_ + max_single_texture_size_;

  MatrixSetIdentity(fixed_function_model_view_matrix_);
  MatrixSetIdentity(fixed_function_projection_matrix_);
  MatrixSetIdentity(fixed_function_composite_matrix_);
  MatrixSetIdentity(fixed_function_inverse_composite_matrix_);

  uint32_t texture_offset = 0;
  uint32_t palette_offset = 0;
  for (auto i = 0; i < 4; ++i, texture_offset += max_single_texture_size_, palette_offset += kMaxPaletteSize) {
    texture_stage_[i].SetStage(i);
    texture_stage_[i].SetTextureDimensions(max_texture_width, max_texture_height);
    texture_stage_[i].SetImageDimensions(max_texture_width, max_texture_height);
    texture_stage_[i].SetTextureOffset(texture_offset);
    texture_stage_[i].SetPaletteOffset(palette_offset);
  }

  SetSurfaceFormat(SCF_A8R8G8B8, SZF_Z24S8, framebuffer_width_, framebuffer_height_, surface_swizzle_);
}

NV2AState::~NV2AState() {
  vertex_buffer_.reset();
  if (texture_memory_) {
    MmFreeContiguousMemory(texture_memory_);
  }
  // texture_palette_memory_ is an offset into texture_memory_ and is intentionally not freed.
  texture_palette_memory_ = nullptr;
}

void NV2AState::ClearDepthStencilRegion(uint32_t depth_value, uint8_t stencil_value, uint32_t left, uint32_t top,
                                        uint32_t width, uint32_t height) const {
  if (!width || width > framebuffer_width_) {
    width = framebuffer_width_;
  }
  if (!height || height > framebuffer_height_) {
    height = framebuffer_height_;
  }

  pb_set_depth_stencil_buffer_region(depth_buffer_format_, depth_value, stencil_value, left, top, width, height);
}

void NV2AState::ClearColorRegion(uint32_t argb, uint32_t left, uint32_t top, uint32_t width, uint32_t height) const {
  if (!width || width > framebuffer_width_) {
    width = framebuffer_width_;
  }
  if (!height || height > framebuffer_height_) {
    height = framebuffer_height_;
  }
  pb_fill(static_cast<int>(left), static_cast<int>(top), static_cast<int>(width), static_cast<int>(height), argb);
}

void NV2AState::EraseText() { pb_erase_text_screen(); }

void NV2AState::Clear(uint32_t argb, uint32_t depth_value, uint8_t stencil_value) const {
  SetupControl0();
  ClearColorRegion(argb);
  ClearDepthStencilRegion(depth_value, stencil_value);
  EraseText();
}

void NV2AState::SetSurfaceFormat(SurfaceColorFormat color_format, SurfaceZetaFormat depth_format, uint32_t width,
                                 uint32_t height, bool swizzle, uint32_t clip_x, uint32_t clip_y, uint32_t clip_width,
                                 uint32_t clip_height, AntiAliasingSetting aa) {
  surface_color_format_ = color_format;
  depth_buffer_format_ = depth_format;
  surface_swizzle_ = swizzle;
  surface_width_ = width;
  surface_height_ = height;
  surface_clip_x_ = clip_x;
  surface_clip_y_ = clip_y;
  surface_clip_width_ = clip_width;
  surface_clip_height_ = clip_height;
  antialiasing_setting_ = aa;

  HandleDepthBufferFormatChange();
}

void NV2AState::SetSurfaceFormatImmediate(SurfaceColorFormat color_format, SurfaceZetaFormat depth_format,
                                          uint32_t width, uint32_t height, bool swizzle, uint32_t clip_x,
                                          uint32_t clip_y, uint32_t clip_width, uint32_t clip_height,
                                          AntiAliasingSetting aa) {
  SetSurfaceFormat(color_format, depth_format, width, height, swizzle, clip_x, clip_y, clip_width, clip_height, aa);
  CommitSurfaceFormat();
}

void NV2AState::CommitSurfaceFormat() const {
  uint32_t value = SET_MASK(NV097_SET_SURFACE_FORMAT_COLOR, surface_color_format_) |
                   SET_MASK(NV097_SET_SURFACE_FORMAT_ZETA, depth_buffer_format_) |
                   SET_MASK(NV097_SET_SURFACE_FORMAT_ANTI_ALIASING, antialiasing_setting_) |
                   SET_MASK(NV097_SET_SURFACE_FORMAT_TYPE, surface_swizzle_ ? NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE
                                                                            : NV097_SET_SURFACE_FORMAT_TYPE_PITCH);
  if (surface_swizzle_) {
    uint32_t log_width = 31 - __builtin_clz(surface_width_);
    value |= SET_MASK(NV097_SET_SURFACE_FORMAT_WIDTH, log_width);
    uint32_t log_height = 31 - __builtin_clz(surface_height_);
    value |= SET_MASK(NV097_SET_SURFACE_FORMAT_HEIGHT, log_height);
  }

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SURFACE_FORMAT, value);
  if (!surface_swizzle_) {
    uint32_t width = surface_clip_width_ ? surface_clip_width_ : surface_width_;
    uint32_t height = surface_clip_height_ ? surface_clip_height_ : surface_height_;
    Pushbuffer::Push(NV097_SET_SURFACE_CLIP_HORIZONTAL, (width << 16) + surface_clip_x_);
    Pushbuffer::Push(NV097_SET_SURFACE_CLIP_VERTICAL, (height << 16) + surface_clip_y_);
  }
  Pushbuffer::End();

  float max_depth = MaxDepthBufferValue(depth_buffer_format_, depth_buffer_mode_float_);
  SetDepthClip(0.0f, max_depth);
}

void NV2AState::SetDepthClip(float min, float max) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_CLIP_MIN, min);
  Pushbuffer::PushF(NV097_SET_CLIP_MAX, max);
  Pushbuffer::End();
}

float NV2AState::MaxDepthBufferValue(uint32_t depth_buffer_format, bool float_mode) {
  float max_depth;
  if (depth_buffer_format == NV097_SET_SURFACE_FORMAT_ZETA_Z16) {
    if (float_mode) {
      *(uint32_t *)&max_depth = 0x43FFF800;  // z16_max as 32-bit float.
    } else {
      max_depth = static_cast<float>(0xFFFF);
    }
  } else {
    if (float_mode) {
      // max_depth = 0x7F7FFF80;  // z24_max as 32-bit float.
      *(uint32_t *)&max_depth =
          0x7149F2CA;  // Observed value, 1e+30 (also used for directional lighting as "infinity").
    } else {
      max_depth = static_cast<float>(0x00FFFFFF);
    }
  }

  return max_depth;
}

void NV2AState::PrepareDraw(uint32_t argb, uint32_t depth_value, uint8_t stencil_value) {
  pb_wait_for_vbl();
  Pushbuffer::Flush();

  SetupTextureStages();
  CommitSurfaceFormat();

  // Override the values set in pb_init. Unfortunately the default is not exposed and must be recreated here.

  Clear(argb, depth_value, stencil_value);

  if (vertex_shader_program_) {
    vertex_shader_program_->PrepareDraw();
  }

  while (pb_busy()) {
    /* Wait for completion... */
  }
}

void NV2AState::SetVertexBufferAttributes(uint32_t enabled_fields) {
  PBKPP_ASSERT(vertex_buffer_ && "Vertex buffer must be set before calling SetVertexBufferAttributes.");
  if (!vertex_buffer_->IsCacheValid()) {
    Pushbuffer::Begin();
    Pushbuffer::Push(NV097_BREAK_VERTEX_BUFFER_CACHE, 0);
    Pushbuffer::End();
    vertex_buffer_->SetCacheValid();
  }

  // TODO: FIXME: Linearize on a per-stage basis instead of basing entirely on stage 0.
  // E.g., if texture unit 0 uses linear and 1 uses swizzle, TEX0 should be linearized, TEX1 should be normalized.
  bool is_linear = texture_stage_[0].enabled_ && texture_stage_[0].IsLinear();
  Vertex *vptr = is_linear ? vertex_buffer_->linear_vertex_buffer_ : vertex_buffer_->normalized_vertex_buffer_;

  auto set = [this, enabled_fields](VertexAttribute attribute, uint32_t attribute_index, uint32_t format, uint32_t size,
                                    const void *data) {
    if (enabled_fields & attribute) {
      uint32_t stride = sizeof(Vertex);
      if (vertex_attribute_stride_override_[attribute_index] != kNoStrideOverride) {
        stride = vertex_attribute_stride_override_[attribute_index];
      }
      SetVertexAttribute(attribute_index, format, size, stride, data);
    } else {
      ClearVertexAttribute(attribute_index);
    }
  };

  set(POSITION, NV2A_VERTEX_ATTR_POSITION, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, vertex_buffer_->position_count_,
      &vptr[0].pos);
  // TODO: Support multi-component weights.
  set(WEIGHT, NV2A_VERTEX_ATTR_WEIGHT, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 1, &vptr[0].weight);
  set(NORMAL, NV2A_VERTEX_ATTR_NORMAL, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, &vptr[0].normal);
  set(DIFFUSE, NV2A_VERTEX_ATTR_DIFFUSE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, &vptr[0].diffuse);
  set(SPECULAR, NV2A_VERTEX_ATTR_SPECULAR, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, &vptr[0].specular);
  set(FOG_COORD, NV2A_VERTEX_ATTR_FOG_COORD, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 1, &vptr[0].fog_coord);
  set(POINT_SIZE, NV2A_VERTEX_ATTR_POINT_SIZE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 1, &vptr[0].point_size);

  set(BACK_DIFFUSE, NV2A_VERTEX_ATTR_BACK_DIFFUSE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, &vptr[0].back_diffuse);
  set(BACK_SPECULAR, NV2A_VERTEX_ATTR_BACK_SPECULAR, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4,
      &vptr[0].back_specular);

  set(TEXCOORD0, NV2A_VERTEX_ATTR_TEXTURE0, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
      vertex_buffer_->tex0_coord_count_, &vptr[0].texcoord0);
  set(TEXCOORD1, NV2A_VERTEX_ATTR_TEXTURE1, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
      vertex_buffer_->tex1_coord_count_, &vptr[0].texcoord1);
  set(TEXCOORD2, NV2A_VERTEX_ATTR_TEXTURE2, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
      vertex_buffer_->tex2_coord_count_, &vptr[0].texcoord2);
  set(TEXCOORD3, NV2A_VERTEX_ATTR_TEXTURE3, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,
      vertex_buffer_->tex3_coord_count_, &vptr[0].texcoord3);

  //  set(V13, NV2A_VERTEX_ATTR_13, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, &vptr[0].v13);
  ClearVertexAttribute(NV2A_VERTEX_ATTR_13);

  //  set(V14, NV2A_VERTEX_ATTR_14, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, &vptr[0].v14);
  ClearVertexAttribute(NV2A_VERTEX_ATTR_14);

  //  set(V15, NV2A_VERTEX_ATTR_15, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, &vptr[0].v15);
  ClearVertexAttribute(NV2A_VERTEX_ATTR_15);
}

void NV2AState::DrawArrays(uint32_t enabled_vertex_fields, DrawPrimitive primitive) {
  if (vertex_shader_program_) {
    vertex_shader_program_->PrepareDraw();
  }

  PBKPP_ASSERT(vertex_buffer_ && "Vertex buffer must be set before calling DrawArrays.");
  auto num_vertices = vertex_buffer_->num_vertices_;
  PBKPP_ASSERT(num_vertices <= 0x100FE &&
               "NV097_DRAW_ARRAYS only allows start indices below 0xFFFF and 0xFF vertices per push");

  // Max 0xFF due to NV097_DRAW_ARRAYS_COUNT bitmask.
  static constexpr uint32_t kVerticesPerPush = 0xFF;

  SetVertexBufferAttributes(enabled_vertex_fields);

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BEGIN_END, primitive);

  uint32_t start = 0;
  while (start < num_vertices) {
    auto remaining = num_vertices - start;
    auto count = remaining < kVerticesPerPush ? remaining : kVerticesPerPush;

    Pushbuffer::Push(NV097_DRAW_ARRAYS,
                     MASK(NV097_DRAW_ARRAYS_COUNT, count - 1) | MASK(NV097_DRAW_ARRAYS_START_INDEX, start));

    start += count;
  }

  Pushbuffer::Push(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  Pushbuffer::End();
}

void NV2AState::Begin(DrawPrimitive primitive) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BEGIN_END, primitive);
  Pushbuffer::End();
}

void NV2AState::End() const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  Pushbuffer::End();
}

void NV2AState::DrawInlineBuffer(uint32_t enabled_vertex_fields, DrawPrimitive primitive) {
  if (vertex_shader_program_) {
    vertex_shader_program_->PrepareDraw();
  }

  PBKPP_ASSERT(vertex_buffer_ && "Vertex buffer must be set before calling DrawInlineBuffer.");

  PBKitFlushPushbufer();

  Begin(primitive);

  auto vertex = vertex_buffer_->Lock();
  for (auto i = 0; i < vertex_buffer_->GetNumVertices(); ++i, ++vertex) {
    // Flush the pushbuffer occasionally.
    if ((i & 0x0FFF) == 0) {
      PBKitFlushPushbufer();
    }

    if (enabled_vertex_fields & WEIGHT) {
      SetWeight(vertex->weight);
    }
    if (enabled_vertex_fields & NORMAL) {
      SetNormal(vertex->normal[0], vertex->normal[1], vertex->normal[2]);
    }
    if (enabled_vertex_fields & DIFFUSE) {
      SetDiffuse(vertex->diffuse[0], vertex->diffuse[1], vertex->diffuse[2], vertex->diffuse[3]);
    }
    if (enabled_vertex_fields & SPECULAR) {
      SetSpecular(vertex->specular[0], vertex->specular[1], vertex->specular[2], vertex->specular[3]);
    }
    if (enabled_vertex_fields & FOG_COORD) {
      SetFogCoord(vertex->fog_coord);
    }
    if (enabled_vertex_fields & POINT_SIZE) {
      SetPointSize(vertex->point_size);
    }
    if (enabled_vertex_fields & BACK_DIFFUSE) {
      SetBackDiffuse(vertex->back_diffuse);
    }
    if (enabled_vertex_fields & BACK_SPECULAR) {
      SetBackSpecular(vertex->back_specular);
    }

    if (enabled_vertex_fields & TEXCOORD0) {
      if (vertex_buffer_->tex0_coord_count_ == 2) {
        SetTexCoord0(vertex->texcoord0[0], vertex->texcoord0[1]);
      } else if (vertex_buffer_->tex0_coord_count_ == 4) {
        SetTexCoord0(vertex->texcoord0[0], vertex->texcoord0[1], vertex->texcoord0[2], vertex->texcoord0[3]);
      } else {
        PBKPP_ASSERT(!"Invalid texcoord count");
      }
    }
    if (enabled_vertex_fields & TEXCOORD1) {
      if (vertex_buffer_->tex1_coord_count_ == 2) {
        SetTexCoord1(vertex->texcoord1[0], vertex->texcoord1[1]);
      } else if (vertex_buffer_->tex1_coord_count_ == 4) {
        SetTexCoord1(vertex->texcoord1[0], vertex->texcoord1[1], vertex->texcoord1[2], vertex->texcoord1[3]);
      } else {
        PBKPP_ASSERT(!"Invalid texcoord count");
      }
    }
    if (enabled_vertex_fields & TEXCOORD2) {
      if (vertex_buffer_->tex2_coord_count_ == 2) {
        SetTexCoord2(vertex->texcoord2[0], vertex->texcoord2[1]);
      } else if (vertex_buffer_->tex2_coord_count_ == 4) {
        SetTexCoord2(vertex->texcoord2[0], vertex->texcoord2[1], vertex->texcoord2[2], vertex->texcoord2[3]);
      } else {
        PBKPP_ASSERT(!"Invalid texcoord count");
      }
    }
    if (enabled_vertex_fields & TEXCOORD3) {
      if (vertex_buffer_->tex3_coord_count_ == 2) {
        SetTexCoord3(vertex->texcoord3[0], vertex->texcoord3[1]);
      } else if (vertex_buffer_->tex3_coord_count_ == 4) {
        SetTexCoord3(vertex->texcoord3[0], vertex->texcoord3[1], vertex->texcoord3[2], vertex->texcoord3[3]);
      } else {
        PBKPP_ASSERT(!"Invalid texcoord count");
      }
    }

    // Setting the position locks in the previously set values and must be done last.
    if (enabled_vertex_fields & POSITION) {
      if (vertex_buffer_->position_count_ == 3) {
        SetVertex(vertex->pos[0], vertex->pos[1], vertex->pos[2]);
      } else {
        SetVertex(vertex->pos[0], vertex->pos[1], vertex->pos[2], vertex->pos[3]);
      }
    }
  }
  vertex_buffer_->Unlock();
  vertex_buffer_->SetCacheValid();

  End();
}

void NV2AState::DrawInlineArray(uint32_t enabled_vertex_fields, DrawPrimitive primitive) {
  if (vertex_shader_program_) {
    vertex_shader_program_->PrepareDraw();
  }

  PBKPP_ASSERT(vertex_buffer_ && "Vertex buffer must be set before calling DrawInlineArray.");
  SetVertexBufferAttributes(enabled_vertex_fields);

  PBKitFlushPushbufer();
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BEGIN_END, primitive);

  auto vertex = vertex_buffer_->Lock();
  for (auto i = 0; i < vertex_buffer_->GetNumVertices(); ++i, ++vertex) {
    // Note: Ordering is important and must follow the NV2A_VERTEX_ATTR_POSITION, ... ordering.
    if (enabled_vertex_fields & POSITION) {
      if (vertex_buffer_->position_count_ == 3) {
        Pushbuffer::Push3F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->pos);
      } else {
        Pushbuffer::Push4F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->pos);
      }
    }
    // TODO: Support multi-element weights.
    if (enabled_vertex_fields & WEIGHT) {
      Pushbuffer::PushF(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->weight);
    }
    if (enabled_vertex_fields & NORMAL) {
      Pushbuffer::Push3F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->normal);
    }
    if (enabled_vertex_fields & DIFFUSE) {
      Pushbuffer::Push4F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->diffuse);
    }
    if (enabled_vertex_fields & SPECULAR) {
      Pushbuffer::Push4F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->specular);
    }
    if (enabled_vertex_fields & FOG_COORD) {
      Pushbuffer::PushF(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->fog_coord);
    }
    if (enabled_vertex_fields & POINT_SIZE) {
      Pushbuffer::PushF(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->point_size);
    }
    if (enabled_vertex_fields & BACK_DIFFUSE) {
      Pushbuffer::Push4F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->back_diffuse);
    }
    if (enabled_vertex_fields & BACK_SPECULAR) {
      Pushbuffer::Push4F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), vertex->back_specular);
    }

#define PUSH_TEXCOORD(count, field)                                                   \
  if ((count) == 4) {                                                                 \
    Pushbuffer::Push4F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), (field)); \
  } else if ((count) == 2) {                                                          \
    Pushbuffer::Push2F(NV2A_SUPPRESS_COMMAND_INCREMENT(NV097_INLINE_ARRAY), (field)); \
  } else {                                                                            \
    PBKPP_ASSERT(!"Invalid texcoord count");                                          \
  }

    if (enabled_vertex_fields & TEXCOORD0) {
      PUSH_TEXCOORD(vertex_buffer_->tex0_coord_count_, vertex->texcoord0)
    }
    if (enabled_vertex_fields & TEXCOORD1) {
      PUSH_TEXCOORD(vertex_buffer_->tex1_coord_count_, vertex->texcoord1)
    }
    if (enabled_vertex_fields & TEXCOORD2) {
      PUSH_TEXCOORD(vertex_buffer_->tex2_coord_count_, vertex->texcoord2)
    }
    if (enabled_vertex_fields & TEXCOORD3) {
      PUSH_TEXCOORD(vertex_buffer_->tex3_coord_count_, vertex->texcoord3)
    }

#undef PUSH_TEXCOORD
  }
  vertex_buffer_->Unlock();
  vertex_buffer_->SetCacheValid();

  Pushbuffer::Push(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  Pushbuffer::End();
}

void NV2AState::DrawInlineElements16(const std::vector<uint32_t> &indices, uint32_t enabled_vertex_fields,
                                     DrawPrimitive primitive) {
  if (vertex_shader_program_) {
    vertex_shader_program_->PrepareDraw();
  }

  PBKPP_ASSERT(vertex_buffer_ && "Vertex buffer must be set before calling DrawInlineElements.");

  SetVertexBufferAttributes(enabled_vertex_fields);

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BEGIN_END, primitive);

  PBKPP_ASSERT(indices.size() < 0x7FFFFFFF);
  int indices_remaining = static_cast<int>(indices.size());
  const uint32_t *next_index = indices.data();
  while (indices_remaining >= 2) {
    uint32_t index_pair = *next_index++ & 0xFFFF;
    index_pair += *next_index++ << 16;

    Pushbuffer::Push(NV097_ARRAY_ELEMENT16, index_pair);

    indices_remaining -= 2;
  }

  if (indices_remaining) {
    Pushbuffer::Push(NV097_ARRAY_ELEMENT32, *next_index);
  }

  Pushbuffer::Push(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  Pushbuffer::End();
}

void NV2AState::DrawInlineElements32(const std::vector<uint32_t> &indices, uint32_t enabled_vertex_fields,
                                     DrawPrimitive primitive) {
  if (vertex_shader_program_) {
    vertex_shader_program_->PrepareDraw();
  }

  PBKPP_ASSERT(vertex_buffer_ && "Vertex buffer must be set before calling DrawInlineElementsForce32.");
  SetVertexBufferAttributes(enabled_vertex_fields);

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BEGIN_END, primitive);

  for (auto index : indices) {
    Pushbuffer::Push(NV097_ARRAY_ELEMENT32, index);
  }

  Pushbuffer::Push(NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
  Pushbuffer::End();
}

void NV2AState::SetVertex(float x, float y, float z) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_VERTEX3F, x, y, z);
  Pushbuffer::End();
}

void NV2AState::SetVertex(float x, float y, float z, float w) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_VERTEX4F, x, y, z, w);
  Pushbuffer::End();
}

void NV2AState::SetWeight(float w1, float w2, float w3, float w4) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_WEIGHT4F, w1, w2, w3, w4);
  Pushbuffer::End();
}

void NV2AState::SetWeight(float w) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_WEIGHT1F, w);
  Pushbuffer::End();
}

void NV2AState::SetWeight(float w1, float w2) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_WEIGHT2F, w1, w2);
  Pushbuffer::End();
}

void NV2AState::SetWeight(float w1, float w2, float w3) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_WEIGHT3F, w1, w2, w3);
  Pushbuffer::End();
}

void NV2AState::SetNormal(float x, float y, float z) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_NORMAL3F, x, y, z);
  Pushbuffer::End();
}

void NV2AState::SetNormal(const float *vals) const {
  Pushbuffer::Begin();
  Pushbuffer::Push3F(NV097_SET_NORMAL3F, vals);
  Pushbuffer::End();
}

void NV2AState::SetNormal3S(int x, int y, int z) const {
  Pushbuffer::Begin();
  uint32_t xy = (x & 0xFFFF) | y << 16;
  uint32_t z0 = z & 0xFFFF;
  Pushbuffer::Push(NV097_SET_NORMAL3S, xy, z0);
  Pushbuffer::End();
}

void NV2AState::SetDiffuse(float r, float g, float b, float a) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_DIFFUSE_COLOR4F, r, g, b, a);
  Pushbuffer::End();
}

void NV2AState::SetDiffuse(float r, float g, float b) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_DIFFUSE_COLOR3F, r, g, b);
  Pushbuffer::End();
}

void NV2AState::SetDiffuse(uint32_t color) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_DIFFUSE_COLOR4I, color);
  Pushbuffer::End();
}

void NV2AState::SetSpecular(float r, float g, float b, float a) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_SPECULAR_COLOR4F, r, g, b, a);
  Pushbuffer::End();
}

void NV2AState::SetSpecular(float r, float g, float b) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_SPECULAR_COLOR3F, r, g, b);
  Pushbuffer::End();
}

void NV2AState::SetSpecular(uint32_t color) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SPECULAR_COLOR4I, color);
  Pushbuffer::End();
}

void NV2AState::SetFogCoord(float fc) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_FOG_COORD, fc);
  Pushbuffer::End();
}

void NV2AState::SetPointSize(float ps) const {
  auto size = static_cast<uint32_t>(ps * 8.f);
  if (size > 0x1FF) {
    size = 0x1FF;
  }
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_POINT_SIZE, size);
  Pushbuffer::End();
}

void NV2AState::SetBackDiffuse(uint32_t color) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_VERTEX_DATA4UB + (4 * NV2A_VERTEX_ATTR_BACK_DIFFUSE), color);
  Pushbuffer::End();
}

void NV2AState::SetBackSpecular(uint32_t color) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_VERTEX_DATA4UB + (4 * NV2A_VERTEX_ATTR_BACK_SPECULAR), color);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord0(float u, float v) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_TEXCOORD0_2F, *(uint32_t *)&u, *(uint32_t *)&v);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord0S(int u, int v) const {
  Pushbuffer::Begin();
  uint32_t uv = (u & 0xFFFF) | (v << 16);
  Pushbuffer::Push(NV097_SET_TEXCOORD0_2S, uv);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord0(float s, float t, float p, float q) const {
  auto pb = pb_begin();
  pb = pb_push4f(pb, NV097_SET_TEXCOORD0_4F, s, t, p, q);
  pb_end(pb);
}

void NV2AState::SetTexCoord0S(int s, int t, int p, int q) const {
  auto pb = pb_begin();
  uint32_t st = (s & 0xFFFF) | (t << 16);
  uint32_t pq = (p & 0xFFFF) | (q << 16);
  pb = pb_push2(pb, NV097_SET_TEXCOORD0_4S, st, pq);
  pb_end(pb);
}

void NV2AState::SetTexCoord1(float u, float v) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_TEXCOORD1_2F, *(uint32_t *)&u, *(uint32_t *)&v);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord1S(int u, int v) const {
  Pushbuffer::Begin();
  uint32_t uv = (u & 0xFFFF) | (v << 16);
  Pushbuffer::Push(NV097_SET_TEXCOORD1_2S, uv);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord1(float s, float t, float p, float q) const {
  auto pb = pb_begin();
  pb = pb_push4f(pb, NV097_SET_TEXCOORD1_4F, s, t, p, q);
  pb_end(pb);
}

void NV2AState::SetTexCoord1S(int s, int t, int p, int q) const {
  auto pb = pb_begin();
  uint32_t st = (s & 0xFFFF) | (t << 16);
  uint32_t pq = (p & 0xFFFF) | (q << 16);
  pb = pb_push2(pb, NV097_SET_TEXCOORD1_4S, st, pq);
  pb_end(pb);
}

void NV2AState::SetTexCoord2(float u, float v) const {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_TEXCOORD2_2F, u, v);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord2S(int u, int v) const {
  Pushbuffer::Begin();
  uint32_t uv = (u & 0xFFFF) | (v << 16);
  Pushbuffer::Push(NV097_SET_TEXCOORD2_2S, uv);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord2(float s, float t, float p, float q) const {
  auto pb = pb_begin();
  pb = pb_push4f(pb, NV097_SET_TEXCOORD2_4F, s, t, p, q);
  pb_end(pb);
}

void NV2AState::SetTexCoord2S(int s, int t, int p, int q) const {
  auto pb = pb_begin();
  uint32_t st = (s & 0xFFFF) | (t << 16);
  uint32_t pq = (p & 0xFFFF) | (q << 16);
  pb = pb_push2(pb, NV097_SET_TEXCOORD2_4S, st, pq);
  pb_end(pb);
}

void NV2AState::SetTexCoord3(float u, float v) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_TEXCOORD3_2F, *(uint32_t *)&u, *(uint32_t *)&v);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord3S(int u, int v) const {
  Pushbuffer::Begin();
  uint32_t uv = (u & 0xFFFF) | (v << 16);
  Pushbuffer::Push(NV097_SET_TEXCOORD3_2S, uv);
  Pushbuffer::End();
}

void NV2AState::SetTexCoord3(float s, float t, float p, float q) const {
  auto pb = pb_begin();
  pb = pb_push4f(pb, NV097_SET_TEXCOORD3_4F, s, t, p, q);
  pb_end(pb);
}

void NV2AState::SetTexCoord3S(int s, int t, int p, int q) const {
  auto pb = pb_begin();
  uint32_t st = (s & 0xFFFF) | (t << 16);
  uint32_t pq = (p & 0xFFFF) | (q << 16);
  pb = pb_push2(pb, NV097_SET_TEXCOORD3_4S, st, pq);
  pb_end(pb);
}

void NV2AState::SetupControl0(bool enable_stencil_write, bool w_buffered, bool texture_perspective_enable) const {
  // FIXME: Figure out what to do in cases where there are multiple stages with different conversion needs.
  // Is this supported by hardware?
  bool requires_colorspace_conversion = texture_stage_[0].RequiresColorspaceConversion();

  uint32_t control0 = enable_stencil_write ? NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE : 0;
  control0 |= depth_buffer_mode_float_ ? NV097_SET_CONTROL0_Z_FORMAT_FLOAT : NV097_SET_CONTROL0_Z_FORMAT_FIXED;
  control0 |= MASK(NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE, w_buffered ? 1 : 0);

  if (texture_perspective_enable) {
    control0 |= NV097_SET_CONTROL0_TEXTURE_PERSPECTIVE_ENABLE;
  }

  if (requires_colorspace_conversion) {
    control0 |= NV097_SET_CONTROL0_COLOR_SPACE_CONVERT_CRYCB_TO_RGB;
  }
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_CONTROL0, control0);
  Pushbuffer::End();
}

void NV2AState::SetupTextureStages() const {
  // TODO: Support texture memory that is not allocated from the base of the DMA target registered by pbkit.
  auto texture_dma_offset = reinterpret_cast<uint32_t>(texture_memory_);
  auto palette_dma_offset = reinterpret_cast<uint32_t>(texture_palette_memory_);
  for (auto &stage : texture_stage_) {
    stage.Commit(texture_dma_offset, palette_dma_offset);
  }
}

void NV2AState::SetTextureFormat(const TextureFormatInfo &fmt, uint32_t stage) { texture_stage_[stage].SetFormat(fmt); }

void NV2AState::SetDefaultTextureParams(uint32_t stage) {
  texture_stage_[stage].Reset();
  texture_stage_[stage].SetTextureDimensions(max_texture_width_, max_texture_height_);
  texture_stage_[stage].SetImageDimensions(max_texture_width_, max_texture_height_);
}

void NV2AState::HandleDepthBufferFormatChange() {
  // Note: This method intentionally recalculates matrices even if the format has not changed as it is called by
  // SetDepthBufferFloatMode when that mode changes.
  switch (fixed_function_matrix_mode_) {
    case MATRIX_MODE_USER:
      break;

    case MATRIX_MODE_DEFAULT_XDK:
      SetXDKDefaultViewportAndFixedFunctionMatrices();
      break;

    case MATRIX_MODE_DEFAULT_NXDK:
      SetDefaultViewportAndFixedFunctionMatrices();
      break;
  }
}

void NV2AState::SetDepthBufferFloatMode(bool enabled) {
  if (enabled == depth_buffer_mode_float_) {
    return;
  }
  depth_buffer_mode_float_ = enabled;
  HandleDepthBufferFormatChange();
}

int NV2AState::SetTexture(SDL_Surface *surface, uint32_t stage) {
  return texture_stage_[stage].SetTexture(surface, texture_memory_);
}

int NV2AState::SetVolumetricTexture(const SDL_Surface **surface, uint32_t depth, uint32_t stage) {
  return texture_stage_[stage].SetVolumetricTexture(surface, depth, texture_memory_);
}

int NV2AState::SetRawTexture(const uint8_t *source, uint32_t width, uint32_t height, uint32_t depth, uint32_t pitch,
                             uint32_t bytes_per_pixel, bool swizzle, uint32_t stage) {
  const uint32_t max_stride = max_texture_width_ * 4;
  const uint32_t max_texture_size = max_stride * max_texture_height_ * max_texture_depth_;

  const uint32_t layer_size = pitch * height;
  const uint32_t surface_size = layer_size * depth;
  PBKPP_ASSERT(surface_size < max_texture_size && "Texture too large.");

  return texture_stage_[stage].SetRawTexture(source, width, height, depth, pitch, bytes_per_pixel, swizzle,
                                             texture_memory_);
}

int NV2AState::SetPalette(const uint32_t *palette, PaletteSize size, uint32_t stage) {
  return texture_stage_[stage].SetPalette(palette, size, texture_palette_memory_);
}

void NV2AState::SetPaletteSize(PaletteSize size, uint32_t stage) { texture_stage_[stage].SetPaletteSize(size); }

void NV2AState::FinishDraw() {
  PBKitBusyWait();

  /* Swap buffers (if we can) */
  while (pb_finished()) {
    /* Not ready to swap yet */
  }
}

void NV2AState::SetVertexShaderProgram(std::shared_ptr<VertexShaderProgram> program) {
  vertex_shader_program_ = std::move(program);

  if (vertex_shader_program_) {
    vertex_shader_program_->Activate();
  } else {
    Pushbuffer::Begin();
    Pushbuffer::Push(
        NV097_SET_TRANSFORM_EXECUTION_MODE,
        MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_FIXED) |
            MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));
    Pushbuffer::Push(NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0x0);
    Pushbuffer::Push(NV097_SET_TRANSFORM_CONSTANT_LOAD, 0x0);
    Pushbuffer::End();
  }
}

std::shared_ptr<VertexBuffer> NV2AState::AllocateVertexBuffer(uint32_t num_vertices) {
  vertex_buffer_.reset();
  vertex_buffer_ = std::make_shared<VertexBuffer>(num_vertices);
  return vertex_buffer_;
}

void NV2AState::SetVertexBuffer(std::shared_ptr<VertexBuffer> buffer) { vertex_buffer_ = std::move(buffer); }

void NV2AState::SetXDKDefaultViewportAndFixedFunctionMatrices() {
  SetWindowClip(framebuffer_width_, framebuffer_height_);
  SetViewportOffset(0.531250f, 0.531250f, 0, 0);
  SetViewportScale(0, -0, 0, 0);

  matrix4_t matrix;
  BuildDefaultXDKModelViewMatrix(matrix);
  SetFixedFunctionModelViewMatrix(matrix);

  BuildDefaultXDKProjectionMatrix(matrix);
  SetFixedFunctionProjectionMatrix(matrix);

  fixed_function_matrix_mode_ = MATRIX_MODE_DEFAULT_XDK;
}

void NV2AState::SetDefaultViewportAndFixedFunctionMatrices() {
  SetWindowClip(framebuffer_width_, framebuffer_height_);
  SetViewportOffset(320, 240, 0, 0);
  if (depth_buffer_format_ == NV097_SET_SURFACE_FORMAT_ZETA_Z16) {
    SetViewportScale(320.000000, -240.000000, 65535.000000, 0);
  } else {
    SetViewportScale(320.000000, -240.000000, 16777215.000000, 0);
  }

  matrix4_t matrix;
  MatrixSetIdentity(matrix);
  SetFixedFunctionModelViewMatrix(matrix);

  matrix[0][0] = 640.0f;
  matrix[1][0] = 0.0f;
  matrix[2][0] = 0.0f;
  matrix[3][0] = 640.0f;

  matrix[0][1] = 0.0f;
  matrix[1][1] = -240.0;
  matrix[2][1] = 0.0f;
  matrix[3][1] = 240.0f;

  matrix[0][2] = 0.0f;
  matrix[1][2] = 0.0f;
  if (depth_buffer_format_ == NV097_SET_SURFACE_FORMAT_ZETA_Z16) {
    matrix[2][2] = 65535.0f;
  } else {
    matrix[2][2] = 16777215.0f;
  }
  matrix[3][2] = 0.0f;

  matrix[0][3] = 0.0f;
  matrix[1][3] = 0.0f;
  matrix[2][3] = 0.0f;
  matrix[3][3] = 1.0f;
  SetFixedFunctionProjectionMatrix(matrix);

  fixed_function_matrix_mode_ = MATRIX_MODE_DEFAULT_NXDK;
}

void NV2AState::BuildDefaultXDKModelViewMatrix(matrix4_t &matrix) {
  vector_t eye{0.0f, 0.0f, -7.0f, 1.0f};
  vector_t at{0.0f, 0.0f, 0.0f, 1.0f};
  vector_t up{0.0f, 1.0f, 0.0f, 1.0f};
  BuildD3DModelViewMatrix(matrix, eye, at, up);
}

void NV2AState::BuildD3DModelViewMatrix(matrix4_t &matrix, const vector_t &eye, const vector_t &at,
                                        const vector_t &up) {
  CreateD3DLookAtLH(matrix, eye, at, up);
}

void NV2AState::BuildD3DProjectionViewportMatrix(matrix4_t &result, float fov, float z_near, float z_far) const {
  matrix4_t viewport;
  if (depth_buffer_format_ == NV097_SET_SURFACE_FORMAT_ZETA_Z16) {
    if (depth_buffer_mode_float_) {
      CreateD3DStandardViewport16BitFloat(viewport, GetFramebufferWidthF(), GetFramebufferHeightF());
    } else {
      CreateD3DStandardViewport16Bit(viewport, GetFramebufferWidthF(), GetFramebufferHeightF());
    }
  } else {
    if (depth_buffer_mode_float_) {
      CreateD3DStandardViewport24BitFloat(viewport, GetFramebufferWidthF(), GetFramebufferHeightF());
    } else {
      CreateD3DStandardViewport24Bit(viewport, GetFramebufferWidthF(), GetFramebufferHeightF());
    }
  }

  matrix4_t projection;
  CreateD3DPerspectiveFOVLH(projection, fov, GetFramebufferWidthF() / GetFramebufferHeightF(), z_near, z_far);

  MatrixMultMatrix(projection, viewport, result);
}

void NV2AState::BuildD3DOrthographicProjectionMatrix(XboxMath::matrix4_t &result, float left, float right, float top,
                                                     float bottom, float z_near, float z_far) {
  CreateD3DOrthographicLH(result, left, right, top, bottom, z_near, z_far);
}

void NV2AState::BuildDefaultXDKProjectionMatrix(matrix4_t &matrix) const {
  BuildD3DProjectionViewportMatrix(matrix, M_PI * 0.25f, 1.0f, 200.0f);
}

void NV2AState::ProjectPoint(vector_t &result, const vector_t &world_point) const {
  XboxMath::ProjectPoint(world_point, fixed_function_composite_matrix_, result);
}

void NV2AState::UnprojectPoint(vector_t &result, const vector_t &screen_point) const {
  XboxMath::UnprojectPoint(screen_point, fixed_function_inverse_composite_matrix_, result);
}

void NV2AState::UnprojectPoint(vector_t &result, const vector_t &screen_point, float world_z) const {
  XboxMath::UnprojectPoint(screen_point, fixed_function_inverse_composite_matrix_, world_z, result);
}

void NV2AState::SetWindowClipExclusive(bool exclusive) {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_WINDOW_CLIP_TYPE, exclusive);
  Pushbuffer::End();
}

void NV2AState::SetWindowClip(uint32_t right, uint32_t bottom, uint32_t left, uint32_t top, uint32_t region) {
  Pushbuffer::Begin();
  const uint32_t offset = region * 4;
  Pushbuffer::Push(NV097_SET_WINDOW_CLIP_HORIZONTAL + offset, left + (right << 16));
  Pushbuffer::Push(NV097_SET_WINDOW_CLIP_VERTICAL + offset, top + (bottom << 16));
  Pushbuffer::End();
}

void NV2AState::SetViewportOffset(float x, float y, float z, float w) {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_VIEWPORT_OFFSET, x, y, z, w);
  Pushbuffer::End();
}

void NV2AState::SetViewportScale(float x, float y, float z, float w) {
  Pushbuffer::Begin();
  Pushbuffer::PushF(NV097_SET_VIEWPORT_SCALE, x, y, z, w);
  Pushbuffer::End();
}

void NV2AState::SetFixedFunctionModelViewMatrix(const matrix4_t &model_matrix) {
  MatrixCopyMatrix(fixed_function_model_view_matrix_, model_matrix);

  Pushbuffer::Begin();
  Pushbuffer::PushTransposedMatrix(NV097_SET_MODEL_VIEW_MATRIX, fixed_function_model_view_matrix_[0]);
  matrix4_t inverse;
  MatrixInvert(fixed_function_model_view_matrix_, inverse);
  Pushbuffer::Push4x3Matrix(NV097_SET_INVERSE_MODEL_VIEW_MATRIX, inverse[0]);
  Pushbuffer::End();

  fixed_function_matrix_mode_ = MATRIX_MODE_USER;

  // Update the composite matrix.
  SetFixedFunctionProjectionMatrix(fixed_function_projection_matrix_);
}

void NV2AState::SetFixedFunctionProjectionMatrix(const matrix4_t &projection_matrix) {
  MatrixCopyMatrix(fixed_function_projection_matrix_, projection_matrix);
  memcpy(fixed_function_projection_matrix_, projection_matrix, sizeof(fixed_function_projection_matrix_));

  GetCompositeMatrix(fixed_function_composite_matrix_, fixed_function_model_view_matrix_,
                     fixed_function_projection_matrix_);

  Pushbuffer::Begin();
  Pushbuffer::PushTransposedMatrix(NV097_SET_COMPOSITE_MATRIX, fixed_function_composite_matrix_[0]);
  Pushbuffer::End();

  BuildInverseCompositeMatrix(fixed_function_composite_matrix_, fixed_function_inverse_composite_matrix_);

  fixed_function_matrix_mode_ = MATRIX_MODE_USER;

  // https://developer.download.nvidia.com/assets/gamedev/docs/W_buffering2.pdf
  auto &proj = fixed_function_projection_matrix_;
  w_near_ = proj[3][3] - (proj[3][2] / proj[2][2] * proj[2][3]);
  w_far_ = (proj[3][3] - proj[3][2]) / (proj[2][2] - proj[2][3]) * proj[2][3] + proj[3][3];
}

void NV2AState::SetTextureStageEnabled(uint32_t stage, bool enabled) {
  PBKPP_ASSERT(stage < kNV2ATextureStages && "Only 4 texture stages are supported.");
  texture_stage_[stage].SetEnabled(enabled);
}

std::string NV2AState::GetPrimitiveName(NV2AState::DrawPrimitive primitive) {
  switch (primitive) {
    case PRIMITIVE_POINTS:
      return "Points";

    case PRIMITIVE_LINES:
      return "Lines";

    case PRIMITIVE_LINE_LOOP:
      return "LineLoop";

    case PRIMITIVE_LINE_STRIP:
      return "LineStrip";

    case PRIMITIVE_TRIANGLES:
      return "Triangles";

    case PRIMITIVE_TRIANGLE_STRIP:
      return "TriStrip";

    case PRIMITIVE_TRIANGLE_FAN:
      return "TriFan";

    case PRIMITIVE_QUADS:
      return "Quads";

    case PRIMITIVE_QUAD_STRIP:
      return "QuadStrip";

    case PRIMITIVE_POLYGON:
      return "Polygon";
  }
}

void NV2AState::SetColorMask(uint32_t mask) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COLOR_MASK, mask);
  Pushbuffer::End();
}

void NV2AState::SetBlend(bool enable, uint32_t func, uint32_t sfactor, uint32_t dfactor) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BLEND_ENABLE, enable);
  if (enable) {
    Pushbuffer::Push(NV097_SET_BLEND_EQUATION, func);
    Pushbuffer::Push(NV097_SET_BLEND_FUNC_SFACTOR, sfactor);
    Pushbuffer::Push(NV097_SET_BLEND_FUNC_DFACTOR, dfactor);
  }
  Pushbuffer::End();
}

void NV2AState::SetBlendColorConstant(uint32_t color) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_BLEND_COLOR, color);
  Pushbuffer::End();
}

void NV2AState::SetAlphaReference(uint32_t alpha) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_ALPHA_REF, alpha);
  Pushbuffer::End();
}

void NV2AState::SetAlphaFunc(bool enable, uint32_t func) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_ALPHA_TEST_ENABLE, enable);
  Pushbuffer::Push(NV097_SET_ALPHA_FUNC, func);
  Pushbuffer::End();
}

void NV2AState::SetCombinerControl(int num_combiners, bool same_factor0, bool same_factor1, bool mux_msb) const {
  PBKPP_ASSERT(num_combiners > 0 && num_combiners < 8);
  uint32_t setting = MASK(NV097_SET_COMBINER_CONTROL_ITERATION_COUNT, num_combiners);
  if (!same_factor0) {
    setting |= MASK(NV097_SET_COMBINER_CONTROL_FACTOR0, NV097_SET_COMBINER_CONTROL_FACTOR0_EACH_STAGE);
  }
  if (!same_factor1) {
    setting |= MASK(NV097_SET_COMBINER_CONTROL_FACTOR1, NV097_SET_COMBINER_CONTROL_FACTOR1_EACH_STAGE);
  }
  if (mux_msb) {
    setting |= MASK(NV097_SET_COMBINER_CONTROL_MUX_SELECT, NV097_SET_COMBINER_CONTROL_MUX_SELECT_MSB);
  }

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_CONTROL, setting);
  Pushbuffer::End();
}

void NV2AState::SetInputColorCombiner(int combiner, CombinerSource a_source, bool a_alpha, CombinerMapping a_mapping,
                                      CombinerSource b_source, bool b_alpha, CombinerMapping b_mapping,
                                      CombinerSource c_source, bool c_alpha, CombinerMapping c_mapping,
                                      CombinerSource d_source, bool d_alpha, CombinerMapping d_mapping) const {
  uint32_t value = MakeInputCombiner(a_source, a_alpha, a_mapping, b_source, b_alpha, b_mapping, c_source, c_alpha,
                                     c_mapping, d_source, d_alpha, d_mapping);
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_ICW + combiner * 4, value);
  Pushbuffer::End();
}

void NV2AState::ClearInputColorCombiner(int combiner) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_ICW + combiner * 4, 0);
  Pushbuffer::End();
}

void NV2AState::ClearInputColorCombiners() const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_ICW, 0, 0, 0, 0);
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_ICW + 0x0C, 0, 0, 0, 0);
  Pushbuffer::End();
}

void NV2AState::SetInputAlphaCombiner(int combiner, CombinerSource a_source, bool a_alpha, CombinerMapping a_mapping,
                                      CombinerSource b_source, bool b_alpha, CombinerMapping b_mapping,
                                      CombinerSource c_source, bool c_alpha, CombinerMapping c_mapping,
                                      CombinerSource d_source, bool d_alpha, CombinerMapping d_mapping) const {
  uint32_t value = MakeInputCombiner(a_source, a_alpha, a_mapping, b_source, b_alpha, b_mapping, c_source, c_alpha,
                                     c_mapping, d_source, d_alpha, d_mapping);
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_ICW + combiner * 4, value);
  Pushbuffer::End();
}

void NV2AState::ClearInputAlphaColorCombiner(int combiner) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_ICW + combiner * 4, 0);
  Pushbuffer::End();
}

void NV2AState::ClearInputAlphaCombiners() const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_ICW, 0, 0, 0, 0);
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_ICW + 0x0C, 0, 0, 0, 0);
  Pushbuffer::End();
}

uint32_t NV2AState::MakeInputCombiner(CombinerSource a_source, bool a_alpha, CombinerMapping a_mapping,
                                      CombinerSource b_source, bool b_alpha, CombinerMapping b_mapping,
                                      CombinerSource c_source, bool c_alpha, CombinerMapping c_mapping,
                                      CombinerSource d_source, bool d_alpha, CombinerMapping d_mapping) const {
  auto channel = [](CombinerSource src, bool alpha, CombinerMapping mapping) {
    return src + (alpha << 4) + (mapping << 5);
  };

  uint32_t ret = (channel(a_source, a_alpha, a_mapping) << 24) + (channel(b_source, b_alpha, b_mapping) << 16) +
                 (channel(c_source, c_alpha, c_mapping) << 8) + channel(d_source, d_alpha, d_mapping);
  return ret;
}

void NV2AState::SetOutputColorCombiner(int combiner, NV2AState::CombinerDest ab_dst, NV2AState::CombinerDest cd_dst,
                                       NV2AState::CombinerDest sum_dst, bool ab_dot_product, bool cd_dot_product,
                                       NV2AState::CombinerSumMuxMode sum_or_mux, NV2AState::CombinerOutOp op,
                                       bool alpha_from_ab_blue, bool alpha_from_cd_blue) const {
  uint32_t value = MakeOutputCombiner(ab_dst, cd_dst, sum_dst, ab_dot_product, cd_dot_product, sum_or_mux, op);
  if (alpha_from_ab_blue) {
    value |= (1 << 19);
  }
  if (alpha_from_cd_blue) {
    value |= (1 << 18);
  }

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_OCW + combiner * 4, value);
  Pushbuffer::End();
}

void NV2AState::ClearOutputColorCombiner(int combiner) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_OCW + combiner * 4, 0);
  Pushbuffer::End();
}

void NV2AState::ClearOutputColorCombiners() const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_OCW, 0, 0, 0, 0);
  Pushbuffer::Push(NV097_SET_COMBINER_COLOR_OCW + 0x0C, 0, 0, 0, 0);
  Pushbuffer::End();
}

void NV2AState::SetOutputAlphaCombiner(int combiner, CombinerDest ab_dst, CombinerDest cd_dst, CombinerDest sum_dst,
                                       bool ab_dot_product, bool cd_dot_product, CombinerSumMuxMode sum_or_mux,
                                       CombinerOutOp op) const {
  uint32_t value = MakeOutputCombiner(ab_dst, cd_dst, sum_dst, ab_dot_product, cd_dot_product, sum_or_mux, op);
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_OCW + combiner * 4, value);
  Pushbuffer::End();
}

void NV2AState::ClearOutputAlphaColorCombiner(int combiner) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_OCW + combiner * 4, 0);
  Pushbuffer::End();
}

void NV2AState::ClearOutputAlphaCombiners() const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_OCW, 0, 0, 0, 0);
  Pushbuffer::Push(NV097_SET_COMBINER_ALPHA_OCW + 0x0C, 0, 0, 0, 0);
  Pushbuffer::End();
}

uint32_t NV2AState::MakeOutputCombiner(NV2AState::CombinerDest ab_dst, NV2AState::CombinerDest cd_dst,
                                       NV2AState::CombinerDest sum_dst, bool ab_dot_product, bool cd_dot_product,
                                       NV2AState::CombinerSumMuxMode sum_or_mux, NV2AState::CombinerOutOp op) const {
  uint32_t ret = cd_dst | (ab_dst << 4) | (sum_dst << 8);
  if (cd_dot_product) {
    ret |= 1 << 12;
  }
  if (ab_dot_product) {
    ret |= 1 << 13;
  }
  if (sum_or_mux) {
    ret |= 1 << 14;
  }
  ret |= op << 15;

  return ret;
}

void NV2AState::SetFinalCombiner0(NV2AState::CombinerSource a_source, bool a_alpha, bool a_invert,
                                  NV2AState::CombinerSource b_source, bool b_alpha, bool b_invert,
                                  NV2AState::CombinerSource c_source, bool c_alpha, bool c_invert,
                                  NV2AState::CombinerSource d_source, bool d_alpha, bool d_invert) {
  auto channel = [](CombinerSource src, bool alpha, bool invert) { return src + (alpha << 4) + (invert << 5); };

  uint32_t value = (channel(a_source, a_alpha, a_invert) << 24) + (channel(b_source, b_alpha, b_invert) << 16) +
                   (channel(c_source, c_alpha, c_invert) << 8) + channel(d_source, d_alpha, d_invert);

  last_specular_fog_cw0_ = value;

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_SPECULAR_FOG_CW0, value);
  Pushbuffer::End();
}

void NV2AState::SetFinalCombiner1(NV2AState::CombinerSource e_source, bool e_alpha, bool e_invert,
                                  NV2AState::CombinerSource f_source, bool f_alpha, bool f_invert,
                                  NV2AState::CombinerSource g_source, bool g_alpha, bool g_invert,
                                  bool specular_add_invert_r0, bool specular_add_invert_v1, bool specular_clamp) {
  auto channel = [](CombinerSource src, bool alpha, bool invert) { return src + (alpha << 4) + (invert << 5); };

  // The V1+R0 sum is not available in CW1.
  PBKPP_ASSERT(e_source != SRC_SPEC_R0_SUM && f_source != SRC_SPEC_R0_SUM && g_source != SRC_SPEC_R0_SUM);

  uint32_t value = (channel(e_source, e_alpha, e_invert) << 24) + (channel(f_source, f_alpha, f_invert) << 16) +
                   (channel(g_source, g_alpha, g_invert) << 8);
  if (specular_add_invert_r0) {
    // NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_ADD_INVERT_R12 crashes on hardware.
    value += (1 << 5);
  }
  if (specular_add_invert_v1) {
    value += NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_ADD_INVERT_R5;
  }
  if (specular_clamp) {
    value += NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_CLAMP;
  }

  last_specular_fog_cw1_ = value;

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_SPECULAR_FOG_CW1, value);
  Pushbuffer::End();
}

void NV2AState::RestoreFinalCombinerState(const std::pair<uint32_t, uint32_t> &state) {
  last_specular_fog_cw0_ = state.first;
  last_specular_fog_cw1_ = state.second;
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_SPECULAR_FOG_CW0, state.first);
  Pushbuffer::Push(NV097_SET_COMBINER_SPECULAR_FOG_CW1, state.second);
  Pushbuffer::End();
}

void NV2AState::SetCombinerFactorC0(int combiner, uint32_t value) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_FACTOR0 + 4 * combiner, value);
  Pushbuffer::End();
}

void NV2AState::SetCombinerFactorC0(int combiner, float red, float green, float blue, float alpha) const {
  float rgba[4]{red, green, blue, alpha};
  SetCombinerFactorC0(combiner, TO_BGRA(rgba));
}

void NV2AState::SetCombinerFactorC1(int combiner, uint32_t value) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_COMBINER_FACTOR1 + 4 * combiner, value);
  Pushbuffer::End();
}

void NV2AState::SetCombinerFactorC1(int combiner, float red, float green, float blue, float alpha) const {
  float rgba[4]{red, green, blue, alpha};
  SetCombinerFactorC1(combiner, TO_BGRA(rgba));
}

void NV2AState::SetFinalCombinerFactorC0(uint32_t value) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SPECULAR_FOG_FACTOR, value);
  Pushbuffer::End();
}

void NV2AState::SetFinalCombinerFactorC0(float red, float green, float blue, float alpha) const {
  float rgba[4]{red, green, blue, alpha};
  SetFinalCombinerFactorC0(TO_BGRA(rgba));
}

void NV2AState::SetFinalCombinerFactorC1(uint32_t value) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SPECULAR_FOG_FACTOR + 0x04, value);
  Pushbuffer::End();
}

void NV2AState::SetFinalCombinerFactorC1(float red, float green, float blue, float alpha) const {
  float rgba[4]{red, green, blue, alpha};
  SetFinalCombinerFactorC1(TO_BGRA(rgba));
}

void NV2AState::SetShaderStageProgram(ShaderStageProgram stage_0, ShaderStageProgram stage_1,
                                      ShaderStageProgram stage_2, ShaderStageProgram stage_3) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SHADER_STAGE_PROGRAM, MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, stage_0) |
                                                       MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE1, stage_1) |
                                                       MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE2, stage_2) |
                                                       MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE3, stage_3));
  Pushbuffer::End();
}

void NV2AState::SetShaderStageInput(uint32_t stage_2_input, uint32_t stage_3_input) const {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SHADER_OTHER_STAGE_INPUT,
                   MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE1, 0) |
                       MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE2, stage_2_input) |
                       MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE3, stage_3_input));
  Pushbuffer::End();
}

void NV2AState::SetShaderClipPlaneComparator(uint32_t stage, bool s_ge_zero, bool t_ge_zero, bool r_ge_zero,
                                             bool q_ge_zero) {
  uint32_t current_mode = clip_plane_mode_[stage];
  clip_plane_mode_[stage] = s_ge_zero | (t_ge_zero << 1) | (r_ge_zero << 2) | (q_ge_zero << 3);

  if (current_mode == clip_plane_mode_[stage]) {
    return;
  }

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_SHADER_CLIP_PLANE_MODE, clip_plane_mode_[0] | (clip_plane_mode_[1] << 4) |
                                                         (clip_plane_mode_[2] << 8) | (clip_plane_mode_[3] << 12));
  Pushbuffer::End();
}

void NV2AState::OverrideVertexAttributeStride(NV2AState::VertexAttribute attribute, uint32_t stride) {
  for (auto i = 0; i < 16; ++i) {
    if (attribute & (1 << i)) {
      vertex_attribute_stride_override_[i] = stride;
      return;
    }
  }

  PBKPP_ASSERT(!"Invalid attribute");
}

void NV2AState::ClearVertexAttributeStrideOverride(NV2AState::VertexAttribute attribute) {
  OverrideVertexAttributeStride(attribute, kNoStrideOverride);
}

float NV2AState::NV2ARound(float input) {
  // The hardware rounding boundary is 1/16th of a pixel past 0.5
  float fraction = input - static_cast<float>(static_cast<uint32_t>(input));
  if (fraction >= 0.5625f) {
    return ceilf(input);
  }

  return floorf(input);
}

static void SetVertexAttribute(uint32_t index, uint32_t format, uint32_t size, uint32_t stride, const void *data) {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + index * 4,
                   MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, format) |
                       MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, size) |
                       MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
  if (size && data) {
    Pushbuffer::Push(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + index * 4, (uint32_t)data & 0x03ffffff);
  }
  Pushbuffer::End();
}

static void ClearVertexAttribute(uint32_t index) {
  // Note: xemu has asserts on the count for several formats, so any format without that PBKPP_ASSERT must be used.
  SetVertexAttribute(index, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 0, 0, nullptr);
}

static void GetCompositeMatrix(matrix4_t &result, const matrix4_t &model_view, const matrix4_t &projection) {
  MatrixMultMatrix(model_view, projection, result);
}

void NV2AState::DrawCheckerboardUnproject(uint32_t first_color, uint32_t second_color, uint32_t checker_size) {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_LIGHTING_ENABLE, false);
  Pushbuffer::Push(NV097_SET_SPECULAR_ENABLE, false);
  Pushbuffer::End();

  auto combiner_state = GetFinalCombinerState();

  SetFinalCombiner0Just(NV2AState::SRC_TEX0);
  SetFinalCombiner1Just(NV2AState::SRC_ZERO, true, true);
  SetTextureStageEnabled(0, true);
  SetShaderStageProgram(NV2AState::STAGE_2D_PROJECTIVE);

  static constexpr uint32_t kTextureSize = 256;

  auto &texture_stage = GetTextureStage(0);
  texture_stage.SetFormat(GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8));
  texture_stage.SetTextureDimensions(kTextureSize, kTextureSize);
  SetupTextureStages();

  auto texture_memory = GetTextureMemoryForStage(0);
  GenerateSwizzledRGBACheckerboard(texture_memory, 0, 0, kTextureSize, kTextureSize, kTextureSize * 4, first_color,
                                   second_color, checker_size);

  Begin(PRIMITIVE_QUADS);
  SetTexCoord0(0.0f, 0.0f);
  vector_t world_point;
  vector_t screen_point = {0.f, 0.f, 0.0f, 1.0f};
  UnprojectPoint(world_point, screen_point, 1.f);
  SetVertex(world_point);

  SetTexCoord0(1.0f, 0.0f);
  screen_point[0] = GetFramebufferWidthF();
  UnprojectPoint(world_point, screen_point, 1.f);
  SetVertex(world_point);

  SetTexCoord0(1.0f, 1.0f);
  screen_point[1] = GetFramebufferHeightF();
  UnprojectPoint(world_point, screen_point, 1.f);
  SetVertex(world_point);

  SetTexCoord0(0.0f, 1.0f);
  screen_point[0] = 0.f;
  UnprojectPoint(world_point, screen_point, 1.f);
  SetVertex(world_point);

  End();

  SetTextureStageEnabled(0, false);
  SetShaderStageProgram(STAGE_NONE);

  RestoreFinalCombinerState(combiner_state);
}

void NV2AState::DrawCheckerboard(uint32_t first_color, uint32_t second_color, uint32_t checker_size) {
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_LIGHTING_ENABLE, false);
  Pushbuffer::Push(NV097_SET_SPECULAR_ENABLE, false);
  Pushbuffer::End();

  auto combiner_state = GetFinalCombinerState();

  SetFinalCombiner0Just(NV2AState::SRC_TEX0);
  SetFinalCombiner1Just(NV2AState::SRC_ZERO, true, true);
  SetTextureStageEnabled(0, true);
  SetShaderStageProgram(NV2AState::STAGE_2D_PROJECTIVE);

  static constexpr uint32_t kTextureSize = 256;

  auto &texture_stage = GetTextureStage(0);
  texture_stage.SetFormat(GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8));
  texture_stage.SetTextureDimensions(kTextureSize, kTextureSize);
  SetupTextureStages();

  auto texture_memory = GetTextureMemoryForStage(0);
  GenerateSwizzledRGBACheckerboard(texture_memory, 0, 0, kTextureSize, kTextureSize, kTextureSize * 4, first_color,
                                   second_color, checker_size);

  const auto width = GetFramebufferWidthF();
  const auto height = GetFramebufferHeightF();

  Begin(PRIMITIVE_QUADS);
  SetTexCoord0(0.0f, 0.0f);
  SetVertex(0.0f, 0.0f, 0.1f, 1.0f);
  SetTexCoord0(1.0f, 0.0f);
  SetVertex(width, 0.0f, 0.1f, 1.0f);
  SetTexCoord0(1.0f, 1.0f);
  SetVertex(width, height, 0.1f, 1.0f);
  SetTexCoord0(0.0f, 1.0f);
  SetVertex(0.0f, height, 0.1f, 1.0f);
  End();

  SetTextureStageEnabled(0, false);
  SetShaderStageProgram(STAGE_NONE);

  RestoreFinalCombinerState(combiner_state);
}

void NV2AState::RenderToSurfaceStart(void *surface_address, SurfaceColorFormat color_format, uint32_t width,
                                     uint32_t height, bool swizzle, uint32_t clip_x, uint32_t clip_y,
                                     uint32_t clip_width, uint32_t clip_height, AntiAliasingSetting aa) {
  const auto kFramebufferPitch = GetFramebufferWidth() * 4;

  const uint32_t surface_pitch = NV2AState::GetSurfaceColorPitch(color_format, width);

  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_CONTEXT_DMA_COLOR, kDefaultDMAChannelA);
  Pushbuffer::Push(NV097_SET_SURFACE_PITCH, SET_MASK(NV097_SET_SURFACE_PITCH_COLOR, surface_pitch) |
                                                SET_MASK(NV097_SET_SURFACE_PITCH_ZETA, kFramebufferPitch));
  Pushbuffer::Push(NV097_SET_SURFACE_COLOR_OFFSET, VRAM_ADDR(surface_address));
  Pushbuffer::End();

  framebuffer_surface_color_format_ = surface_color_format_;

  // Failing to disable alpha blending on B8 and G8B8 will trigger a hardware exception.
  SetBlend(SurfaceSupportsAlpha(color_format));

  SetSurfaceFormatImmediate(color_format, depth_buffer_format_, width, height, swizzle, clip_x, clip_y, clip_width,
                            clip_height, aa);
}

void NV2AState::RenderToSurfaceEnd() {
  const uint32_t kFramebufferPitch = GetFramebufferWidth() * 4;
  Pushbuffer::Begin();
  Pushbuffer::Push(NV097_SET_CONTEXT_DMA_COLOR, kDefaultDMAColorChannel);
  Pushbuffer::Push(NV097_SET_SURFACE_COLOR_OFFSET, 0);
  Pushbuffer::Push(NV097_SET_SURFACE_PITCH, SET_MASK(NV097_SET_SURFACE_PITCH_COLOR, kFramebufferPitch) |
                                                SET_MASK(NV097_SET_SURFACE_PITCH_ZETA, kFramebufferPitch));
  Pushbuffer::End();

  SetSurfaceFormatImmediate(framebuffer_surface_color_format_, depth_buffer_format_, GetFramebufferWidth(),
                            GetFramebufferHeight(), false);

  SetBlend(true);
}

}  // namespace PBKitPlusPlus
