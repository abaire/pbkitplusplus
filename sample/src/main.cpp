#ifndef XBOX
#error Must be built with nxdk
#endif

#include <SDL.h>
#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include <utility>

#include "dds_image.h"
#include "light.h"
#include "nv2astate.h"
#include "shaders/passthrough_vertex_shader.h"
#include "torus_model.h"
#include "xbox_math_matrix.h"

static const int kFramebufferWidth = 640;
static const int kFramebufferHeight = 480;
static const int kBitsPerPixel = 32;
static const int kMaxTextureSize = 512;
static const int kMaxTextureDepth = 3;

// From the left, pointing right and into the screen.
static constexpr vector_t kDirectionalLightDir{1.f, 0.f, 1.f, 1.f};
static constexpr vector_t kDirectionalLightAmbientColor{0.f, 0.f, 0.05f, 0.f};
static constexpr vector_t kDirectionalLightDiffuseColor{0.25f, 0.f, 0.f, 0.f};
static constexpr vector_t kDirectionalLightSpecularColor{0.f, 0.2f, 0.4f, 0.f};

static constexpr const char kAlphaDXT5[] = "D:\\images\\plasma_alpha_dxt5.dds";

extern "C" __cdecl int automount_d_drive(void);

static void SetupTexture(PBKitPlusPlus::NV2AState &state) {
  PBKitPlusPlus::DDSImage img;
  bool loaded = img.LoadFile(kAlphaDXT5, true);
  PBKPP_ASSERT(loaded && "Failed to load test image from file.");
  auto data = img.GetPrimaryImage();

  auto &texture_stage = state.GetTextureStage(0);
  texture_stage.SetFormat(PBKitPlusPlus::GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8));
  texture_stage.SetTextureDimensions(data->width, data->height);
  state.SetRawTexture(data->data.data(), data->compressed_width, data->compressed_height, data->depth, data->pitch,
                      data->bytes_per_pixel, false);
}

static void DrawTexturedGeometry(PBKitPlusPlus::NV2AState &state) {
  static constexpr float kOutputSize = 256.0f;
  float left = floorf((state.GetFramebufferWidthF() - kOutputSize) * 0.5f);
  float right = left + kOutputSize;
  float top = floorf((state.GetFramebufferHeightF() - kOutputSize) * 0.5f);
  float bottom = top + kOutputSize;

  state.SetTextureStageEnabled(0, true);
  state.SetupTextureStages();
  state.SetShaderStageProgram(PBKitPlusPlus::NV2AState::STAGE_2D_PROJECTIVE);

  state.SetFinalCombiner0Just(PBKitPlusPlus::NV2AState::SRC_TEX0);
  state.SetFinalCombiner1Just(PBKitPlusPlus::NV2AState::SRC_TEX0, true);

  state.Begin(PBKitPlusPlus::NV2AState::PRIMITIVE_QUADS);
  state.SetTexCoord0(0.0f, 0.0f);
  state.SetScreenVertex(left, top, 0.1f);

  state.SetTexCoord0(1.0f, 0.0f);
  state.SetScreenVertex(right, top, 0.1f);

  state.SetTexCoord0(1.0f, 1.0f);
  state.SetScreenVertex(right, bottom, 0.1f);

  state.SetTexCoord0(0.0f, 1.0f);
  state.SetScreenVertex(left, bottom, 0.1f);
  state.End();

  state.SetTextureStageEnabled(0, false);
}

static void DrawLitGeometry(PBKitPlusPlus::NV2AState &state, PBKitPlusPlus::Light &light,
                            std::shared_ptr<PBKitPlusPlus::VertexBuffer> torus_vb) {
  state.DisableTextureStages();
  state.SetupTextureStages();

  state.SetCombinerControl(1);
  state.SetInputColorCombiner(
      0, PBKitPlusPlus::NV2AState::SRC_DIFFUSE, false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_IDENTITY,
      PBKitPlusPlus::NV2AState::SRC_ZERO, false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_INVERT,
      PBKitPlusPlus::NV2AState::SRC_SPECULAR, false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_IDENTITY,
      PBKitPlusPlus::NV2AState::SRC_ZERO, false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_INVERT);

  state.SetInputAlphaCombiner(
      0, PBKitPlusPlus::NV2AState::SRC_DIFFUSE, true, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_IDENTITY,
      PBKitPlusPlus::NV2AState::SRC_ZERO, false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_INVERT,
      PBKitPlusPlus::NV2AState::SRC_SPECULAR, true, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_IDENTITY,
      PBKitPlusPlus::NV2AState::SRC_ZERO, false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_INVERT);

  state.SetOutputColorCombiner(0, PBKitPlusPlus::NV2AState::DST_DISCARD, PBKitPlusPlus::NV2AState::DST_DISCARD,
                               PBKitPlusPlus::NV2AState::DST_R0);
  state.SetOutputAlphaCombiner(0, PBKitPlusPlus::NV2AState::DST_DISCARD, PBKitPlusPlus::NV2AState::DST_DISCARD,
                               PBKitPlusPlus::NV2AState::DST_R0);

  state.SetFinalCombiner0Just(PBKitPlusPlus::NV2AState::SRC_R0);
  state.SetFinalCombiner1(PBKitPlusPlus::NV2AState::SRC_ZERO, false, false, PBKitPlusPlus::NV2AState::SRC_ZERO, false,
                          false, PBKitPlusPlus::NV2AState::SRC_R0, true, false,
                          /*specular_add_invert_r0*/ false, /* specular_add_invert_v1*/ false,
                          /* specular_clamp */ true);

  PBKitPlusPlus::Pushbuffer::Begin();
  PBKitPlusPlus::Pushbuffer::Push(NV097_SET_LIGHTING_ENABLE, true);
  PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SPECULAR_ENABLE, true);
  PBKitPlusPlus::Pushbuffer::End();

  state.SetVertexBuffer(std::move(torus_vb));
  state.DrawArrays(PBKitPlusPlus::NV2AState::POSITION | PBKitPlusPlus::NV2AState::NORMAL |
                   PBKitPlusPlus::NV2AState::DIFFUSE | PBKitPlusPlus::NV2AState::SPECULAR);
}

static void DrawProgrammableShaderGeometry(PBKitPlusPlus::NV2AState &state,
                                           std::shared_ptr<PBKitPlusPlus::VertexShaderProgram> shader) {
  state.SetVertexShaderProgram(shader);
  state.SetFinalCombiner0Just(PBKitPlusPlus::NV2AState::SRC_DIFFUSE);
  state.SetFinalCombiner1Just(PBKitPlusPlus::NV2AState::SRC_ZERO, true, true);

  const float left = 32.f;
  const float right = state.GetFramebufferWidthF() - left;
  const float top = 32.f;
  ;
  const float bottom = state.GetFramebufferHeightF() - top;

  state.Begin(PBKitPlusPlus::NV2AState::PRIMITIVE_LINE_LOOP);
  state.SetDiffuse(0xFF00FF00);
  state.SetVertex(left, top, 0.1f);

  state.SetDiffuse(0xFF00FFFF);
  state.SetVertex(right, top, 0.1f);

  state.SetDiffuse(0xFF0000FF);
  state.SetVertex(right, bottom, 0.1f);

  state.SetDiffuse(0xFFFF0000);
  state.SetVertex(left, bottom, 0.1f);
  state.End();

  state.SetVertexShaderProgram(nullptr);
}

static void Initialize(PBKitPlusPlus::NV2AState &state) {
  const uint32_t kFramebufferPitch = state.GetFramebufferWidth() * 4;
  state.SetSurfaceFormat(PBKitPlusPlus::NV2AState::SCF_A8R8G8B8, PBKitPlusPlus::NV2AState::SZF_Z16,
                         state.GetFramebufferWidth(), state.GetFramebufferHeight());

  {
    PBKitPlusPlus::Pushbuffer::Begin();
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_EYE_POSITION, 0.0f, 0.0f, 0.0f, 1.0f);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_ZMIN_MAX_CONTROL, NV097_SET_ZMIN_MAX_CONTROL_CULL_NEAR_FAR_EN_TRUE |
                                                                    NV097_SET_ZMIN_MAX_CONTROL_ZCLAMP_EN_CULL |
                                                                    NV097_SET_ZMIN_MAX_CONTROL_CULL_IGNORE_W_FALSE);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SURFACE_PITCH,
                                    SET_MASK(NV097_SET_SURFACE_PITCH_COLOR, kFramebufferPitch) |
                                        SET_MASK(NV097_SET_SURFACE_PITCH_ZETA, kFramebufferPitch));
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SURFACE_CLIP_HORIZONTAL, state.GetFramebufferWidth() << 16);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SURFACE_CLIP_VERTICAL, state.GetFramebufferHeight() << 16);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_LIGHTING_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SPECULAR_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_LIGHT_CONTROL, NV097_SET_LIGHT_CONTROL_V_ALPHA_FROM_MATERIAL_SPECULAR |
                                                                 NV097_SET_LIGHT_CONTROL_V_SEPARATE_SPECULAR);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_LIGHT_ENABLE_MASK, NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_OFF);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_COLOR_MATERIAL, NV097_SET_COLOR_MATERIAL_ALL_FROM_MATERIAL);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SCENE_AMBIENT_COLOR, 0x0, 0x0, 0x0);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_MATERIAL_EMISSION, 0x0, 0x0, 0x0);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_MATERIAL_ALPHA, 1.0f);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_BACK_MATERIAL_ALPHA, 1.f);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_LIGHT_TWO_SIDE_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FRONT_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_BACK_POLYGON_MODE, NV097_SET_FRONT_POLYGON_MODE_V_FILL);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_POINT_PARAMS_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_POINT_SMOOTH_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_POINT_SIZE, 8);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_DOT_RGBMAPPING, 0);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SHADE_MODEL, NV097_SET_SHADE_MODEL_SMOOTH);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FLAT_SHADE_OP, NV097_SET_FLAT_SHADE_OP_VERTEX_LAST);
    PBKitPlusPlus::Pushbuffer::End();
  }

  PBKitPlusPlus::NV2AState::SetWindowClipExclusive(false);
  // Note, setting the first clip region will cause the hardware to also set all subsequent regions.
  PBKitPlusPlus::NV2AState::SetWindowClip(state.GetFramebufferWidth(), state.GetFramebufferHeight());

  state.SetBlend();

  state.ClearInputColorCombiners();
  state.ClearInputAlphaCombiners();
  state.ClearOutputColorCombiners();
  state.ClearOutputAlphaCombiners();

  state.SetCombinerControl(1);
  state.SetInputColorCombiner(0, PBKitPlusPlus::NV2AState::SRC_DIFFUSE, false,
                              PBKitPlusPlus::NV2AState::MAP_UNSIGNED_IDENTITY, PBKitPlusPlus::NV2AState::SRC_ZERO,
                              false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_INVERT);
  state.SetInputAlphaCombiner(0, PBKitPlusPlus::NV2AState::SRC_DIFFUSE, true,
                              PBKitPlusPlus::NV2AState::MAP_UNSIGNED_IDENTITY, PBKitPlusPlus::NV2AState::SRC_ZERO,
                              false, PBKitPlusPlus::NV2AState::MAP_UNSIGNED_INVERT);

  state.SetOutputColorCombiner(0, PBKitPlusPlus::NV2AState::DST_DISCARD, PBKitPlusPlus::NV2AState::DST_DISCARD,
                               PBKitPlusPlus::NV2AState::DST_R0);
  state.SetOutputAlphaCombiner(0, PBKitPlusPlus::NV2AState::DST_DISCARD, PBKitPlusPlus::NV2AState::DST_DISCARD,
                               PBKitPlusPlus::NV2AState::DST_R0);

  state.SetFinalCombiner0(PBKitPlusPlus::NV2AState::SRC_ZERO, false, false, PBKitPlusPlus::NV2AState::SRC_ZERO, false,
                          false, PBKitPlusPlus::NV2AState::SRC_ZERO, false, false, PBKitPlusPlus::NV2AState::SRC_R0);
  state.SetFinalCombiner1(PBKitPlusPlus::NV2AState::SRC_ZERO, false, false, PBKitPlusPlus::NV2AState::SRC_ZERO, false,
                          false, PBKitPlusPlus::NV2AState::SRC_R0, true, false, /*specular_add_invert_r0*/ false,
                          /* specular_add_invert_v1*/ false,
                          /* specular_clamp */ true);

  while (pb_busy()) {
    /* Wait for completion... */
  }

  matrix4_t identity_matrix;
  MatrixSetIdentity(identity_matrix);
  for (auto i = 0; i < 4; ++i) {
    auto &stage = state.GetTextureStage(i);
    stage.SetUWrap(PBKitPlusPlus::TextureStage::WRAP_CLAMP_TO_EDGE, false);
    stage.SetVWrap(PBKitPlusPlus::TextureStage::WRAP_CLAMP_TO_EDGE, false);
    stage.SetPWrap(PBKitPlusPlus::TextureStage::WRAP_CLAMP_TO_EDGE, false);
    stage.SetQWrap(false);

    stage.SetEnabled(false);
    stage.SetCubemapEnable(false);
    stage.SetFilter();
    stage.SetAlphaKillEnable(false);
    stage.SetColorKeyMode(PBKitPlusPlus::TextureStage::CKM_DISABLE);
    stage.SetLODClamp(0, 4095);

    stage.SetTextureMatrixEnable(false);
    stage.SetTextureMatrix(identity_matrix);

    stage.SetTexgenS(PBKitPlusPlus::TextureStage::TG_DISABLE);
    stage.SetTexgenT(PBKitPlusPlus::TextureStage::TG_DISABLE);
    stage.SetTexgenR(PBKitPlusPlus::TextureStage::TG_DISABLE);
    stage.SetTexgenQ(PBKitPlusPlus::TextureStage::TG_DISABLE);
  }

  // TODO: Set up with TextureStage instances in state.
  {
    PBKitPlusPlus::Pushbuffer::Begin();
    uint32_t address = NV097_SET_TEXTURE_ADDRESS;
    uint32_t control = NV097_SET_TEXTURE_CONTROL0;
    uint32_t filter = NV097_SET_TEXTURE_FILTER;
    PBKitPlusPlus::Pushbuffer::Push(address, 0x10101);
    PBKitPlusPlus::Pushbuffer::Push(control, 0x3ffc0);
    PBKitPlusPlus::Pushbuffer::Push(filter, 0x1012000);

    address += 0x40;
    control += 0x40;
    filter += 0x40;
    PBKitPlusPlus::Pushbuffer::Push(address, 0x10101);
    PBKitPlusPlus::Pushbuffer::Push(control, 0x3ffc0);
    PBKitPlusPlus::Pushbuffer::Push(filter, 0x1012000);

    address += 0x40;
    control += 0x40;
    filter += 0x40;
    PBKitPlusPlus::Pushbuffer::Push(address, 0x10101);
    PBKitPlusPlus::Pushbuffer::Push(control, 0x3ffc0);
    PBKitPlusPlus::Pushbuffer::Push(filter, 0x1012000);

    address += 0x40;
    control += 0x40;
    filter += 0x40;
    PBKitPlusPlus::Pushbuffer::Push(address, 0x10101);
    PBKitPlusPlus::Pushbuffer::Push(control, 0x3ffc0);
    PBKitPlusPlus::Pushbuffer::Push(filter, 0x1012000);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FOG_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_FOG_PLANE, 0.f, 0.f, 1.f, 0.f);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FOG_GEN_MODE, NV097_SET_FOG_GEN_MODE_V_PLANAR);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FOG_MODE, NV097_SET_FOG_MODE_V_LINEAR);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FOG_COLOR, 0xFFFFFFFF);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_TEXTURE_MATRIX_ENABLE, 0, 0, 0, 0);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_FRONT_FACE, NV097_SET_FRONT_FACE_V_CW);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_CULL_FACE, NV097_SET_CULL_FACE_V_BACK);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_CULL_FACE_ENABLE, true);

    PBKitPlusPlus::Pushbuffer::Push(
        NV097_SET_COLOR_MASK, NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE | NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE |
                                  NV097_SET_COLOR_MASK_RED_WRITE_ENABLE | NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_DEPTH_TEST_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_DEPTH_MASK, true);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_STENCIL_TEST_ENABLE, false);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_STENCIL_MASK, 0xFF);
    // If the stencil comparison fails, leave the value in the stencil buffer alone.
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_STENCIL_OP_FAIL, NV097_SET_STENCIL_OP_V_KEEP);
    // If the stencil comparison passes but the depth comparison fails, leave the stencil buffer alone.
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_STENCIL_OP_ZFAIL, NV097_SET_STENCIL_OP_V_KEEP);
    // If the stencil comparison passes and the depth comparison passes, leave the stencil buffer alone.
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_STENCIL_OP_ZPASS, NV097_SET_STENCIL_OP_V_KEEP);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_STENCIL_FUNC_REF, 0x7F);

    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_NORMALIZATION_ENABLE, false);

    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_WEIGHT4F, 0.f, 0.f, 0.f, 0.f);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_NORMAL3F, 0.f, 0.f, 0.f);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_DIFFUSE_COLOR4I, 0x00000000);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_SPECULAR_COLOR4I, 0x00000000);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_TEXCOORD0_4F, 0.f, 0.f, 0.f, 0.f);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_TEXCOORD1_4F, 0.f, 0.f, 0.f, 0.f);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_TEXCOORD2_4F, 0.f, 0.f, 0.f, 0.f);
    PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_TEXCOORD3_4F, 0.f, 0.f, 0.f, 0.f);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_VERTEX_DATA4UB + (4 * NV2A_VERTEX_ATTR_BACK_DIFFUSE), 0xFFFFFFFF);
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_VERTEX_DATA4UB + (4 * NV2A_VERTEX_ATTR_BACK_SPECULAR), 0);

    // Pow 16
    const float specular_params[]{-0.803673, -2.7813, 2.97762, -0.64766, -2.36199, 2.71433};
    for (uint32_t i = 0, offset = 0; i < 6; ++i, offset += 4) {
      PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_SPECULAR_PARAMS + offset, specular_params[i]);
      PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_SPECULAR_PARAMS_BACK + offset, 0);
    }
    PBKitPlusPlus::Pushbuffer::End();
  }

  state.SetDefaultViewportAndFixedFunctionMatrices();
  state.SetDepthBufferFloatMode(false);

  state.SetVertexShaderProgram(nullptr);

  const auto &texture_format = PBKitPlusPlus::GetTextureFormatInfo(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8);
  state.SetTextureFormat(texture_format, 0);
  state.SetDefaultTextureParams(0);
  state.SetTextureFormat(texture_format, 1);
  state.SetDefaultTextureParams(1);
  state.SetTextureFormat(texture_format, 2);
  state.SetDefaultTextureParams(2);
  state.SetTextureFormat(texture_format, 3);
  state.SetDefaultTextureParams(3);

  state.SetTextureStageEnabled(0, false);
  state.SetTextureStageEnabled(1, false);
  state.SetTextureStageEnabled(2, false);
  state.SetTextureStageEnabled(3, false);
  state.SetShaderStageProgram(PBKitPlusPlus::NV2AState::STAGE_NONE);
  state.SetShaderStageInput(0, 0);

  state.ClearAllVertexAttributeStrideOverrides();
}

int main() {
  automount_d_drive();

  debugPrint("Set video mode");
  if (!XVideoSetMode(kFramebufferWidth, kFramebufferHeight, kBitsPerPixel, REFRESH_DEFAULT)) {
    debugPrint("Failed to set video mode\n");
    Sleep(2000);
    return 1;
  }

  int status = pb_init();
  if (status) {
    debugPrint("pb_init Error %d\n", status);
    Sleep(2000);
    return 1;
  }

  debugPrint("Initializing...");
  pb_show_debug_screen();

  if (SDL_Init(SDL_INIT_GAMECONTROLLER)) {
    debugPrint("Failed to initialize SDL_GAMECONTROLLER.");
    debugPrint("%s", SDL_GetError());
    pb_show_debug_screen();
    Sleep(2000);
    return 1;
  }

  pb_show_front_screen();
  debugClearScreen();

  auto shader = std::make_shared<PBKitPlusPlus::PassthroughVertexShader>();

  PBKitPlusPlus::NV2AState state(kFramebufferWidth, kFramebufferHeight, kMaxTextureSize, kMaxTextureSize,
                                 kMaxTextureDepth);
  Initialize(state);

  state.SetVertexShaderProgram(nullptr);
  state.SetXDKDefaultViewportAndFixedFunctionMatrices();

  SetupTexture(state);

  std::shared_ptr<PBKitPlusPlus::VertexBuffer> torus_vb;
  {
    auto construct_model = [&state](PBKitPlusPlus::ModelBuilder &model,
                                    std::shared_ptr<PBKitPlusPlus::VertexBuffer> &vertex_buffer) {
      vertex_buffer = state.AllocateVertexBuffer(model.GetVertexCount());
      model.PopulateVertexBuffer(vertex_buffer);
    };

    vector_t diffuse{0.8f, 0.3f, 0.9f, 0.75f};
    vector_t specular{0.f, 0.9, 0.f, 0.25f};
    auto model = TorusModel(diffuse, specular);
    construct_model(model, torus_vb);
  }

  auto light = PBKitPlusPlus::DirectionalLight(0, kDirectionalLightDir);
  {
    light.SetAmbient(kDirectionalLightAmbientColor);
    light.SetDiffuse(kDirectionalLightDiffuseColor);
    light.SetSpecular(kDirectionalLightSpecularColor);

    vector_t eye{0.0f, 0.0f, -7.0f, 1.0f};
    vector_t at{0.0f, 0.0f, 0.0f, 1.0f};
    vector_t light_look_dir{0.f, 0.f, 0.f, 1.f};

    VectorSubtractVector(at, eye, light_look_dir);
    VectorNormalize(light_look_dir);

    light.Commit(state, light_look_dir);

    PBKitPlusPlus::Pushbuffer::Begin();
    PBKitPlusPlus::Pushbuffer::Push(NV097_SET_LIGHT_ENABLE_MASK, light.light_enable_mask());

    // Pow 4
    const float specular_params[]{-0.421539, -1.70592, 2.28438, -0.170208, -0.855842, 1.68563};
    for (uint32_t i = 0, offset = 0; i < 6; ++i, offset += 4) {
      PBKitPlusPlus::Pushbuffer::PushF(NV097_SET_SPECULAR_PARAMS + offset, specular_params[i]);
    }
    PBKitPlusPlus::Pushbuffer::End();
  }

  bool running = true;
  while (running) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED: {
          SDL_GameController *controller = SDL_GameControllerOpen(event.cdevice.which);
          if (!controller) {
            debugPrint("Failed to handle controller add event.");
            debugPrint("%s", SDL_GetError());
            running = false;
          }
        } break;

        case SDL_CONTROLLERDEVICEREMOVED: {
          SDL_GameController *controller = SDL_GameControllerFromInstanceID(event.cdevice.which);
          SDL_GameControllerClose(controller);
        } break;

        case SDL_CONTROLLERBUTTONUP:
          running = false;
          break;

        default:
          break;
      }
    }

    state.PrepareDraw(0xFE101010);

    DrawLitGeometry(state, light, torus_vb);
    DrawTexturedGeometry(state);
    DrawProgrammableShaderGeometry(state, shader);

    pb_print("Press any button to exit");

    PBKitPlusPlus::NV2AState::FinishDraw();
  }

  pb_kill();
  return 0;
}
