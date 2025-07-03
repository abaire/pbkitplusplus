#include "passthrough_vertex_shader.h"

#include <pbkit/pbkit.h>

namespace PBKitPlusPlus {

// clang-format off
static const uint32_t kPassthroughVsh[] = {
#include "passthrough.vshinc"
};
// clang-format on

PassthroughVertexShader::PassthroughVertexShader() : VertexShaderProgram(kPassthroughVsh, sizeof(kPassthroughVsh)) {}

}  // namespace PBKitPlusPlus
