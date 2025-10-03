#ifndef PBKITPLUSPLUS_TEXTURE_FORMAT_H
#define PBKITPLUSPLUS_TEXTURE_FORMAT_H

#include <SDL.h>

#include <cstdint>

namespace PBKitPlusPlus {

typedef struct TextureFormatInfo {
  SDL_PixelFormatEnum sdl_format{SDL_PIXELFORMAT_ARGB8888};
  uint32_t xbox_format{0};
  uint32_t xbox_bpp{32};  // bits per pixel
  bool xbox_swizzled{false};
  bool xbox_linear{true};
  bool require_conversion{false};
  const char *name{nullptr};
} TextureFormatInfo;

extern const TextureFormatInfo kTextureFormats[];
extern const int kNumFormats;

const TextureFormatInfo &GetTextureFormatInfo(uint32_t nv_texture_format);

//! Attempts to map the given surface format to a similar texture format.
//!
//! This is primarily useful when re-rendering a previously rendered surface as a texture.
//!
//! \param nv_surface_format NV097_SET_SURFACE_FORMAT_COLOR_* constant to map.
//! \param use_linear Whether a linear or a swizzled texture format should be returned.
//! \return NV097_SET_TEXTURE_FORMAT_COLOR_* mapping for the surface.
uint32_t TextureFormatForSurfaceFormat(uint32_t nv_surface_format, bool use_linear = false);

}  // namespace PBKitPlusPlus

#endif  // PBKITPLUSPLUS_TEXTURE_FORMAT_H
