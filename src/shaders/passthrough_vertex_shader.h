#ifndef NXDK_PGRAPH_TESTS_SHADERS_PASSTHROUGH_VERTEX_SHADER_H_
#define NXDK_PGRAPH_TESTS_SHADERS_PASSTHROUGH_VERTEX_SHADER_H_

#include <cstdint>

#include "vertex_shader_program.h"
#include "xbox_math_types.h"

using namespace XboxMath;

namespace PBKitPlusPlus {

//! A vertex shader that passes through all inputs without modifications. It performs no lighting.
class PassthroughVertexShader : public VertexShaderProgram {
 public:
  PassthroughVertexShader();
};

}  // namespace PBKitPlusPlus

#endif  // NXDK_PGRAPH_TESTS_SHADERS_PASSTHROUGH_VERTEX_SHADER_H_
