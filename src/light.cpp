#include "light.h"

#include <pbkit/pbkit.h>

#include <cmath>

#include "nv2astate.h"
#include "nxdk_ext.h"
#include "pushbuffer.h"
#include "xbox_math_matrix.h"

namespace PBKitPlusPlus {

static constexpr float PI_OVER_180 = (float)M_PI / 180.0f;
#define DEG2RAD(c) ((float)(c) * PI_OVER_180)

Light::Light(uint32_t light_index, uint32_t enable_mask)
    : light_index_(light_index), light_enable_mask_(LIGHT_MODE(light_index, enable_mask)) {}

void Light::Commit(NV2AState& host) const {
  Pushbuffer::Begin();
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_AMBIENT_COLOR), ambient_);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_DIFFUSE_COLOR), diffuse_);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_SPECULAR_COLOR), specular_);

  Pushbuffer::Push3F(SET_BACK_LIGHT(light_index_, NV097_SET_BACK_LIGHT_AMBIENT_COLOR), back_ambient_);
  Pushbuffer::Push3F(SET_BACK_LIGHT(light_index_, NV097_SET_BACK_LIGHT_DIFFUSE_COLOR), back_diffuse_);
  Pushbuffer::Push3F(SET_BACK_LIGHT(light_index_, NV097_SET_BACK_LIGHT_SPECULAR_COLOR), back_specular_);
  Pushbuffer::End();
}

void Light::Commit(NV2AState& host, const XboxMath::vector_t& look_direction) const { Commit(host); }

static constexpr float kFalloffFactors[][3] = {
    {-0.f, 1.f, 0.f},                     // falloff = 0.f
    {-0.000244f, 0.500122f, 0.499634f},   // falloff=0.5
    {0.f, -0.494592f, 1.494592f},         // falloff = 1.f (default)
    {-0.170208f, -0.855843f, 1.685635f},  // falloff=2.0
    {-0.706496f, -2.507095f, 2.800600f},  // falloff=10.0
    {-0.932112f, -3.097628f, 3.165516f},  // falloff=50.0
};

static float GetFalloffFactorOne(Spotlight::FalloffFactor factor) {
  return kFalloffFactors[static_cast<unsigned>(factor)][0];
}
static float GetFalloffFactorTwo(Spotlight::FalloffFactor factor) {
  return kFalloffFactors[static_cast<unsigned>(factor)][1];
}
static float GetFalloffFactorThree(Spotlight::FalloffFactor factor) {
  return kFalloffFactors[static_cast<unsigned>(factor)][2];
}

Spotlight::Spotlight(uint32_t light_index, const XboxMath::vector_t& position, const XboxMath::vector_t& direction,
                     float range, float phi, float theta, float attenuation_constant, float attenuation_linear,
                     float attenuation_quadratic, FalloffFactor falloff)
    : Spotlight(light_index, position, direction, range, phi, theta, attenuation_constant, attenuation_linear,
                attenuation_quadratic, GetFalloffFactorOne(falloff), GetFalloffFactorTwo(falloff),
                GetFalloffFactorThree(falloff)) {}

Spotlight::Spotlight(uint32_t light_index, const XboxMath::vector_t& position, const XboxMath::vector_t& direction,
                     float range, float phi, float theta, float attenuation_constant, float attenuation_linear,
                     float attenuation_quadratic, float falloff_1, float falloff_2, float falloff_3)
    : Light(light_index, NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_SPOT), range_(range), phi_(phi), theta_(theta) {
  memcpy(position_, position, sizeof(position_));
  memcpy(direction_, direction, sizeof(direction_));

  attenuation_[0] = attenuation_constant;
  attenuation_[1] = attenuation_linear;
  attenuation_[2] = attenuation_quadratic;

  falloff_[0] = falloff_1;
  falloff_[1] = falloff_2;
  falloff_[2] = falloff_3;
}

void Spotlight::Commit(NV2AState& host) const {
  Light::Commit(host);

  XboxMath::vector_t transformed_position;
  XboxMath::VectorMultMatrix(position_, host.GetFixedFunctionModelViewMatrix(), transformed_position);
  transformed_position[3] = 1.f;

  // TODO: Direction should probably be transformed as well.
  vector_t normalized_direction{0.f, 0.f, 0.f, 1.f};
  VectorNormalize(direction_, normalized_direction);

  float cos_half_theta = cosf(0.5f * DEG2RAD(theta_));
  float cos_half_phi = cosf(0.5f * DEG2RAD(phi_));

  float inv_scale = -1.f / (cos_half_theta - cos_half_phi);
  ScalarMultVector(normalized_direction, inv_scale);
  normalized_direction[3] = cos_half_phi * inv_scale;

  Pushbuffer::Begin();
  Pushbuffer::PushF(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_RANGE), range_);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_POSITION), transformed_position);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_ATTENUATION), attenuation_);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_SPOT_FALLOFF), falloff_);
  Pushbuffer::Push4F(SET_LIGHT(light_index_, NV097_SET_LIGHT_SPOT_DIRECTION), normalized_direction);
  Pushbuffer::End();
}

DirectionalLight::DirectionalLight(uint32_t light_index, const XboxMath::vector_t& direction)
    : Light(light_index, NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_INFINITE) {
  memcpy(direction_, direction, sizeof(direction_));
}

void DirectionalLight::Commit(NV2AState& host) const {
  PBKPP_ASSERT(!"DirectionLight must use the look_dir Commit variant");
}

void DirectionalLight::Commit(NV2AState& host, const vector_t& look_dir) const {
  Light::Commit(host);
  // Calculate the Blinn half vector.
  // https://paroj.github.io/gltut/Illumination/Tut11%20BlinnPhong%20Model.html

  vector_t half_angle_vector{0.f, 0.f, 0.f, 1.f};
  VectorAddVector(look_dir, direction_, half_angle_vector);
  VectorNormalize(half_angle_vector);
  ScalarMultVector(half_angle_vector, -1.f);

  // Infinite direction is the direction towards the light source.
  vector_t infinite_direction;
  ScalarMultVector(direction_, -1.f, infinite_direction);

  Pushbuffer::Begin();
  Pushbuffer::PushF(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_RANGE), 1e30f);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_INFINITE_HALF_VECTOR), half_angle_vector);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_INFINITE_DIRECTION), infinite_direction);
  Pushbuffer::End();
}

PointLight::PointLight(uint32_t light_index, const XboxMath::vector_t& position, float range,
                       float attenuation_constant, float attenuation_linear, float attenuation_quadratic)
    : Light(light_index, NV097_SET_LIGHT_ENABLE_MASK_LIGHT0_LOCAL), range_(range) {
  memcpy(position_, position, sizeof(position_));

  attenuation_[0] = attenuation_constant;
  attenuation_[1] = attenuation_linear;
  attenuation_[2] = attenuation_quadratic;
}

void PointLight::Commit(NV2AState& host) const {
  Light::Commit(host);

  vector_t transformed_position;
  const matrix4_t& view_matrix = host.GetFixedFunctionModelViewMatrix();
  VectorMultMatrix(position_, view_matrix, transformed_position);
  transformed_position[3] = 1.f;

  Pushbuffer::Begin();
  Pushbuffer::PushF(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_RANGE), range_);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_POSITION), transformed_position);
  Pushbuffer::Push3F(SET_LIGHT(light_index_, NV097_SET_LIGHT_LOCAL_ATTENUATION), attenuation_);
  Pushbuffer::End();
}

}  // namespace PBKitPlusPlus
