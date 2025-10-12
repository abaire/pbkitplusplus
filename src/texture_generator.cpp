#include "texture_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "pbkpp_assert.h"
#include "xbox-swizzle/swizzle.h"

namespace PBKitPlusPlus {

void GenerateRGBACheckerboard(void *target, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height,
                              uint32_t pitch, uint32_t first_color, uint32_t second_color, uint32_t checker_size) {
  auto buffer = reinterpret_cast<uint8_t *>(target);
  auto odd = first_color;
  auto even = second_color;
  buffer += y_offset * pitch;

  for (uint32_t y = 0; y < height; ++y) {
    auto pixel = reinterpret_cast<uint32_t *>(buffer);
    pixel += x_offset;
    buffer += pitch;

    if (!(y % checker_size)) {
      auto temp = odd;
      odd = even;
      even = temp;
    }

    for (uint32_t x = 0; x < width; ++x) {
      *pixel++ = ((x / checker_size) & 0x01) ? odd : even;
    }
  }
}

void GenerateSwizzledRGBACheckerboard(void *target, uint32_t x_offset, uint32_t y_offset, uint32_t width,
                                      uint32_t height, uint32_t pitch, uint32_t first_color, uint32_t second_color,
                                      uint32_t checker_size) {
  const uint32_t size = height * pitch;
  auto temp_buffer = new uint8_t[size];
  memcpy(temp_buffer, target, size);

  GenerateRGBACheckerboard(temp_buffer, x_offset, y_offset, width, height, pitch, first_color, second_color,
                           checker_size);
  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), pitch, 4);
  delete[] temp_buffer;
}

static void GenerateRGBTestPattern(void *target, uint32_t width, uint32_t height, uint8_t alpha, uint32_t pitch) {
  auto row = static_cast<uint8_t *>(target);

  const uint32_t alpha_channel = static_cast<uint32_t>(alpha) << 24;
  for (uint32_t y = 0; y < height; ++y) {
    auto y_normal = static_cast<uint32_t>(static_cast<float>(y) * 255.0f / static_cast<float>(height));

    auto pixels = reinterpret_cast<uint32_t *>(row);
    for (uint32_t x = 0; x < width; ++x, ++pixels) {
      auto x_normal = static_cast<uint32_t>(static_cast<float>(x) * 255.0f / static_cast<float>(width));
      *pixels = y_normal + (x_normal << 8) + ((255 - y_normal) << 16) + alpha_channel;
    }

    row += pitch;
  }
}

void GenerateRGBTestPattern(void *target, uint32_t width, uint32_t height, uint8_t alpha) {
  GenerateRGBTestPattern(target, width, height, alpha, width * 4);
}

void GenerateSwizzledRGBTestPattern(void *target, uint32_t width, uint32_t height, uint8_t alpha) {
  const uint32_t size = height * width * 4;
  auto temp_buffer = new uint8_t[size];

  GenerateRGBTestPattern(temp_buffer, width, height, alpha, width * 4);

  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), width * 4, 4);
  delete[] temp_buffer;
}

void GenerateRGBATestPattern(void *target, uint32_t width, uint32_t height) {
  auto pixels = static_cast<uint32_t *>(target);
  for (uint32_t y = 0; y < height; ++y) {
    auto y_normal = static_cast<uint32_t>(static_cast<float>(y) * 255.0f / static_cast<float>(height));

    for (uint32_t x = 0; x < width; ++x, ++pixels) {
      auto x_normal = static_cast<uint32_t>(static_cast<float>(x) * 255.0f / static_cast<float>(width));
      *pixels = y_normal + (x_normal << 8) + ((255 - y_normal) << 16) + ((x_normal + y_normal) << 24);
    }
  }
}

void GenerateSwizzledRGBATestPattern(void *target, uint32_t width, uint32_t height) {
  const uint32_t size = height * width * 4;
  auto temp_buffer = new uint8_t[size];

  GenerateRGBATestPattern(temp_buffer, width, height);

  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), width * 4, 4);
  delete[] temp_buffer;
}

void GenerateRGBRadialATestPattern(void *target, uint32_t width, uint32_t height) {
  PBKPP_ASSERT(!(width & 1) && "Width must be even");
  PBKPP_ASSERT(!(height & 1) && "Height must be even");

  auto pixels = static_cast<uint32_t *>(target);

  auto half_width = width >> 1;
  auto half_height = height >> 1;

  auto ur_pixels = pixels + half_width;

  GenerateRGBTestPattern(pixels, half_width, half_height, 0x00, width * 4);
  GenerateRGBTestPattern(ur_pixels, half_width, half_height, 0x00, width * 4);

  auto ll_pixels = pixels + half_height * width;
  memcpy(ll_pixels, pixels, width * half_height * 4);

  auto cx = static_cast<float>(half_width);
  auto cy = static_cast<float>(half_height);
  auto max_distance = cx * cx + cy * cy;

  for (uint32_t y = 0; y < height; ++y) {
    auto dy = static_cast<float>(y) - cx;

    for (uint32_t x = 0; x < width; ++x, ++pixels) {
      auto dx = static_cast<float>(x) - cx;
      auto distance = dx * dx + dy * dy;
      auto alpha = static_cast<uint32_t>(255.0f * (1.f - distance / max_distance));
      *pixels = (*pixels & 0x00FFFFFF) + (alpha << 24);
    }
  }
}

void GenerateSwizzledRGBRadialATestPattern(void *target, uint32_t width, uint32_t height) {
  const uint32_t size = height * width * 4;
  auto temp_buffer = new uint8_t[size];

  GenerateRGBRadialATestPattern(temp_buffer, width, height);

  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), width * 4, 4);
  delete[] temp_buffer;
}

void GenerateRGBRadialGradient(void *target, int width, int height, uint32_t color_mask, uint8_t alpha, bool linear) {
  PBKPP_ASSERT(target && "target must not be NULL");
  PBKPP_ASSERT(width && "width must be > 0");
  PBKPP_ASSERT(height && "height must be > 0");

  const float centerX = static_cast<float>(width - 1) / 2.0f;
  const float centerY = static_cast<float>(height - 1) / 2.0f;
  const float maxDist = sqrt(centerX * centerX + centerY * centerY);

  const bool r_enabled = (color_mask & 0x000000FF);
  const bool g_enabled = (color_mask & 0x0000FF00);
  const bool b_enabled = (color_mask & 0x00FF0000);
  const bool all_channels_enabled = r_enabled && g_enabled && b_enabled;

  auto pixel = reinterpret_cast<uint32_t *>(target);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x, ++pixel) {
      const float dx = static_cast<float>(x) - centerX;
      const float dy = static_cast<float>(y) - centerY;
      const float dist = sqrt(dx * dx + dy * dy);
      const float normDist = dist / maxDist;

      float r_f;
      float g_f;
      float b_f;

      if (linear) {
        r_f = (static_cast<float>(x) / static_cast<float>(width)) * normDist;
        g_f = (static_cast<float>(y) / static_cast<float>(height)) * normDist;
        b_f = 0.75f * normDist;
      } else {
        static constexpr float kSplitPoint = 0.25f;
        static constexpr float kBrightnessAtSplit = 0.5f;

        float brightness_mod;
        if (normDist < kSplitPoint) {
          // Ramp 1 (Fast): Linearly map distance [0, kSplitPoint] to brightness [0, kBrightnessAtSplit].
          brightness_mod = (normDist / kSplitPoint) * kBrightnessAtSplit;
        } else {
          // Ramp 2 (Slower): Linearly map distance [kSplitPoint, 1.0] to brightness [kBrightnessAtSplit, 1.0].
          const float remaining_dist_percent = (normDist - kSplitPoint) / (1.0f - kSplitPoint);
          const float remaining_brightness = 1.0f - kBrightnessAtSplit;
          brightness_mod = kBrightnessAtSplit + (remaining_dist_percent * remaining_brightness);
        }

        // Use the modified brightness for the color calculation instead of the original normDist.
        r_f = (static_cast<float>(x) / static_cast<float>(width)) * brightness_mod;
        g_f = (static_cast<float>(y) / static_cast<float>(height)) * brightness_mod;
        b_f = 0.75f * brightness_mod;
      }
      auto r_orig = static_cast<uint8_t>(r_f * 255);
      auto g_orig = static_cast<uint8_t>(g_f * 255);
      auto b_orig = static_cast<uint8_t>(b_f * 255);

      uint8_t r_final, g_final, b_final;

      if (all_channels_enabled) {
        r_final = r_orig;
        g_final = g_orig;
        b_final = b_orig;
      } else {
        uint8_t intensity = ::std::max({r_orig, g_orig, b_orig});

        r_final = r_enabled ? intensity : 0;
        g_final = g_enabled ? intensity : 0;
        b_final = b_enabled ? intensity : 0;
      }

      *pixel = (r_final) | (g_final << 8) | (b_final << 16) | (alpha << 24);
    }
  }
}

void GenerateSwizzledRGBRadialGradient(void *target, int width, int height, uint32_t color_mask, uint8_t alpha,
                                       bool linear) {
  const uint32_t size = height * width * 4;
  auto temp_buffer = new uint8_t[size];

  GenerateRGBRadialGradient(temp_buffer, width, height, color_mask, alpha, linear);

  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), width * 4, 4);
  delete[] temp_buffer;
}

int GenerateSurface(SDL_Surface **surface, int width, int height) {
  *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA8888);
  if (!(*surface)) {
    return 1;
  }

  if (SDL_LockSurface(*surface)) {
    SDL_FreeSurface(*surface);
    *surface = nullptr;
    return 2;
  }

  auto pixels = static_cast<uint32_t *>((*surface)->pixels);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x, ++pixels) {
      int x_normal = static_cast<int>(static_cast<float>(x) * 255.0f / static_cast<float>(width));
      int y_normal = static_cast<int>(static_cast<float>(y) * 255.0f / static_cast<float>(height));
      *pixels = SDL_MapRGBA((*surface)->format, y_normal, x_normal, 255 - y_normal, x_normal + y_normal);
    }
  }

  SDL_UnlockSurface(*surface);

  return 0;
}

int GenerateCheckboardSurface(SDL_Surface **surface, int width, int height) {
  *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA8888);
  if (!(*surface)) {
    return 1;
  }

  if (SDL_LockSurface(*surface)) {
    SDL_FreeSurface(*surface);
    *surface = nullptr;
    return 2;
  }

  auto pixels = static_cast<uint32_t *>((*surface)->pixels);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x, ++pixels) {
      int x_normal = static_cast<int>(static_cast<float>(x) * 255.0f / static_cast<float>(width));
      int y_normal = static_cast<int>(static_cast<float>(y) * 255.0f / static_cast<float>(height));
      *pixels = SDL_MapRGBA((*surface)->format, y_normal, x_normal, 255 - y_normal, x_normal + y_normal);
    }
  }

  SDL_UnlockSurface(*surface);

  return 0;
}

int GenerateCheckerboardSurface(SDL_Surface **surface, int width, int height, uint32_t first_color,
                                uint32_t second_color, uint32_t checker_size) {
  *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
  if (!(*surface)) {
    return 1;
  }

  if (SDL_LockSurface(*surface)) {
    SDL_FreeSurface(*surface);
    *surface = nullptr;
    return 2;
  }

  auto pixels = static_cast<uint32_t *>((*surface)->pixels);
  GenerateRGBACheckerboard(pixels, 0, 0, width, height, width * 4, first_color, second_color, checker_size);

  SDL_UnlockSurface(*surface);

  return 0;
}

void GenerateColoredCheckerboard(void *target, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height,
                                 uint32_t pitch, const uint32_t *colors, uint32_t num_colors, uint32_t checker_size) {
  auto buffer = reinterpret_cast<uint8_t *>(target);
  buffer += y_offset * pitch;

  uint32_t row_color_index = 0;

  for (uint32_t y = 0; y < height; ++y) {
    auto pixel = reinterpret_cast<uint32_t *>(buffer);
    pixel += x_offset;
    buffer += pitch;

    if (y && !(y % checker_size)) {
      row_color_index = ++row_color_index % num_colors;
    }

    auto color_index = row_color_index;
    auto color = colors[color_index];
    for (uint32_t x = 0; x < width; ++x) {
      if (x && !(x % checker_size)) {
        color_index = ++color_index % num_colors;
        color = colors[color_index];
      }
      *pixel++ = color;
    }
  }
}

int GenerateColoredCheckerboardSurface(SDL_Surface **gradient_surface, int width, int height, uint32_t checker_size) {
  *gradient_surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA8888);
  if (!(*gradient_surface)) {
    return 1;
  }

  if (SDL_LockSurface(*gradient_surface)) {
    SDL_FreeSurface(*gradient_surface);
    *gradient_surface = nullptr;
    return 2;
  }

  static const uint32_t kColors[] = {
      SDL_MapRGBA((*gradient_surface)->format, 0xFF, 0, 0, 0xFF),
      SDL_MapRGBA((*gradient_surface)->format, 0xFF, 0, 0, 0x66),
      SDL_MapRGBA((*gradient_surface)->format, 0, 0xFF, 0, 0xFF),
      SDL_MapRGBA((*gradient_surface)->format, 0, 0xFF, 0, 0x66),
      SDL_MapRGBA((*gradient_surface)->format, 0x44, 0x44, 0xFF, 0xFF),
      SDL_MapRGBA((*gradient_surface)->format, 0x44, 0x44, 0xFF, 0x66),
      SDL_MapRGBA((*gradient_surface)->format, 0xFF, 0xFF, 0xFF, 0xFF),
      SDL_MapRGBA((*gradient_surface)->format, 0xFF, 0xFF, 0xFF, 0x66),
  };
  static const uint32_t kNumColors = sizeof(kColors) / sizeof(kColors[0]);

  auto pixels = static_cast<uint8_t *>((*gradient_surface)->pixels);
  GenerateColoredCheckerboard(pixels, 0, 0, width, height, width * 4, kColors, kNumColors, checker_size);

  SDL_UnlockSurface(*gradient_surface);

  return 0;
}

void GenerateRGBDiagonalLinePattern(void *target, uint32_t width, uint32_t height, uint32_t line_spacing,
                                    uint32_t background_color) {
  static constexpr uint32_t kMinBrightness = 0x33;
  static constexpr uint32_t kColorRange = 0xFF - kMinBrightness;

  if (line_spacing == 0) {
    line_spacing = 16;
  }

  auto *pixel = reinterpret_cast<uint32_t *>(target);

  for (auto y = 0; y < height; ++y) {
    for (auto x = 0; x < width; ++x) {
      if ((y - x + width) % line_spacing != 0) {
        *pixel++ = background_color;
        continue;
      }

      uint8_t r = kMinBrightness + (x * 7) % kColorRange;
      uint8_t g = kMinBrightness + (y * 5) % kColorRange;
      uint8_t b = kMinBrightness + ((x - y) * 3) % kColorRange;
      uint8_t a = 0xFF;

      *pixel++ = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) |
                 static_cast<uint32_t>(r);
    }
  }
}

void GenerateMaxContrastNoisePattern(void *target, int width, int height, uint32_t seed_rgb, uint8_t alpha) {
  auto buffer = reinterpret_cast<uint32_t *>(target);

  uint8_t seed_r = (seed_rgb >> 16) & 0xFF;
  uint8_t seed_g = (seed_rgb >> 8) & 0xFF;
  uint8_t seed_b = seed_rgb & 0xFF;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Pattern for the Red channel: alternates every pixel horizontally (vertical stripes).
      uint8_t r = (x % 2) * 255;

      // Pattern for the Green channel: alternates every pixel vertically (horizontal stripes).
      uint8_t g = (y % 2) * 255;

      // Pattern for the Blue channel: a 2x2 checkerboard pattern.
      // Integer division (x/2, y/2) creates 2x2 blocks.
      uint8_t b = ((x / 2 + y / 2) % 2) * 255;

      r ^= seed_r;
      g ^= seed_g;
      b ^= seed_b;

      buffer[y * width + x] = (alpha << 24) | (b << 16) | (g << 8) | r;
    }
  }
}

void GenerateSwizzledRGBMaxContrastNoisePattern(void *target, int width, int height, uint32_t seed_rgb, uint8_t alpha) {
  const uint32_t size = height * width * 4;
  auto temp_buffer = new uint8_t[size];

  GenerateMaxContrastNoisePattern(temp_buffer, width, height, seed_rgb, alpha);

  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), width * 4, 4);
  delete[] temp_buffer;
}

void GenerateRGBA444RadialAlphaPattern(void *target, uint32_t width, uint32_t height) {
  PBKPP_ASSERT(target);
  PBKPP_ASSERT(width);
  PBKPP_ASSERT(height);

  auto buffer = static_cast<uint16_t *>(target);

  const auto width_minus_1 = static_cast<float>(width - 1);
  const auto height_minus_1 = static_cast<float>(height - 1);
  const auto cx = width_minus_1 / 2.0f;
  const auto cy = height_minus_1 / 2.0f;

  const float max_distance = cx * cx + cy * cy;
  for (int y = 0; y < height; ++y) {
    auto dy = static_cast<float>(y) - cx;

    for (int x = 0; x < width; ++x) {
      auto dx = static_cast<float>(x) - cx;
      auto distance = dx * dx + dy * dy;
      auto alpha = static_cast<uint16_t>(15.0f * (1.f - distance / max_distance));

      const float fx = (width > 1) ? static_cast<float>(x) / width_minus_1 : 0.0f;
      const float fy = (height > 1) ? static_cast<float>(y) / height_minus_1 : 0.0f;

      // Channel 1 (0xF00 component): Varies vertically from 0 at the top to 0xF at the bottom.
      const auto c1_val = static_cast<uint16_t>(15.0f * fy);

      // Channel 2 (0x0F0 component): Varies diagonally.
      const float c2_top = 15.0f * fx;
      const float c2_bottom = 15.0f * (1.0f - fx);
      const auto c2_val = static_cast<uint16_t>(c2_top * (1.0f - fy) + c2_bottom * fy);

      // Channel 3 (0x00F component): Varies horizontally from 0xF at the left to 0 at the right.
      const auto c3_val = static_cast<uint16_t>(15.0f * (1.0f - fx));

      *buffer++ = ((alpha & 0xF) << 12) | (c1_val << 8) | (c2_val << 4) | c3_val;
    }
  }
}

void GenerateSwizzledRGBA444RadialAlphaPattern(void *target, uint32_t width, uint32_t height) {
  const uint32_t size = height * width * 2;
  auto temp_buffer = new uint8_t[size];

  GenerateRGBA444RadialAlphaPattern(temp_buffer, width, height);

  swizzle_rect(temp_buffer, width, height, reinterpret_cast<uint8_t *>(target), width * 2, 2);
  delete[] temp_buffer;
}

}  // namespace PBKitPlusPlus
