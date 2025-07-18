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

}  // namespace PBKitPlusPlus

#endif  // PBKITPLUSPLUS_TEXTURE_FORMAT_H
