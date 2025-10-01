#ifndef PBKITPLUSPLUS_TEXTURE_GENERATOR_H
#define PBKITPLUSPLUS_TEXTURE_GENERATOR_H

#include <SDL.h>

#include <cstdint>

namespace PBKitPlusPlus {

// Inserts a rectangular checkerboard pattern into the given 32bpp texture buffer.
void GenerateRGBACheckerboard(void *buffer, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height,
                              uint32_t pitch, uint32_t first_color = 0xFF00FFFF, uint32_t second_color = 0xFF000000,
                              uint32_t checker_size = 8);

void GenerateColoredCheckerboard(void *buffer, uint32_t x_offset, uint32_t y_offset, uint32_t width, uint32_t height,
                                 uint32_t pitch, const uint32_t *colors, uint32_t num_colors,
                                 uint32_t checker_size = 8);

void GenerateSwizzledRGBACheckerboard(void *buffer, uint32_t x_offset, uint32_t y_offset, uint32_t width,
                                      uint32_t height, uint32_t pitch, uint32_t first_color = 0xFF00FFFF,
                                      uint32_t second_color = 0xFF000000, uint32_t checker_size = 8);

//! Generates a simple color ramp pattern. Alpha is set to the given value.
void GenerateRGBTestPattern(void *target, uint32_t width, uint32_t height, uint8_t alpha = 0xFF);

//! Generates a simple color ramp pattern. Alpha is set to the given value.
void GenerateSwizzledRGBTestPattern(void *target, uint32_t width, uint32_t height, uint8_t alpha = 0xFF);

//! Generates a simple color ramp pattern. Alpha varys from 0 at the upper left to max at the lower right.
void GenerateRGBATestPattern(void *target, uint32_t width, uint32_t height);

//! Generates a simple color ramp pattern. Alpha varys from 0 at the upper left to max at the lower right.
void GenerateSwizzledRGBATestPattern(void *target, uint32_t width, uint32_t height);

//! Generates 4 color ramp quadrants with alpha varying from 0 at the corners to max at the center.
void GenerateRGBRadialATestPattern(void *target, uint32_t width, uint32_t height);

//! Generates 4 color ramp quadrants with alpha varying from 0 at the corners to max at the center.
void GenerateSwizzledRGBRadialATestPattern(void *target, uint32_t width, uint32_t height);

int GenerateSurface(SDL_Surface **surface, int width, int height);
int GenerateCheckerboardSurface(SDL_Surface **surface, int width, int height, uint32_t first_color = 0xFF00FFFF,
                                uint32_t second_color = 0xFF000000, uint32_t checker_size = 8);
int GenerateColoredCheckerboardSurface(SDL_Surface **surface, int width, int height, uint32_t checker_size = 4);

//! Fills the target with diagonal lines where each consecutive pixel in the line is a different color.
void GenerateRGBDiagonalLinePattern(void *target, uint32_t width, uint32_t height, uint32_t line_spacing = 8,
                                    uint32_t background_color = 0);

//! Generates a radial gradient with brighter colors near edges.
//!
//! \param target Buffer to be populated
//! \param width  Width of the buffer in pixels
//! \param height Height of the buffer in pixels
//! \param color_mask Mask used to tint the gradient
//! \param alpha Alpha value that should be assigned to each pixel
//! \param linear If true, the gradient is linear from 0 at the center to 1 at edges. Otherwise ramp from 0 quickly.
void GenerateRGBRadialGradient(void *target, int width, int height, uint32_t color_mask = 0xFFFFFF,
                               uint8_t alpha = 0xFF, bool linear = true);

//! Generates a radial gradient with brighter colors near edges.
//!
//! \param target Buffer to be populated
//! \param width  Width of the buffer in pixels
//! \param height Height of the buffer in pixels
//! \param color_mask Mask used to tint the gradient
//! \param alpha Alpha value that should be assigned to each pixel
//! \param linear If true, the gradient is linear from 0 at the center to 1 at edges. Otherwise ramp from 0 quickly.
void GenerateSwizzledRGBRadialGradient(void *target, int width, int height, uint32_t color_mask = 0xFFFFFF,
                                       uint8_t alpha = 0xFF, bool linear = true);

//! Fills a 32-bit RGBA buffer to maximize local contrast.
//!
//! This function generates a deterministic pattern where each primary color channel (R, G, B)
//! is turned on or off based on different high-frequency patterns derived from the
//! pixel coordinates. This ensures that any two adjacent pixels have a large
//! difference in their color and brightness values.
//!
//! @param buffer A pointer to the 32-bit RGBA buffer to be filled.
//! @param width The width of the image in pixels.
//! @param height The height of the image in pixels.
//! @param alpha The alpha value set for every pixel in the region
void GenerateRGBMaxContrastNoisePattern(void *target, int width, int height, uint32_t seed_rgb = 0,
                                        uint8_t alpha = 0xFF);

//! Fills a 32-bit RGBA buffer to maximize local contrast.
//!
//! This function generates a deterministic pattern where each primary color channel (R, G, B)
//! is turned on or off based on different high-frequency patterns derived from the
//! pixel coordinates. This ensures that any two adjacent pixels have a large
//! difference in their color and brightness values.
//!
//! @param buffer A pointer to the 32-bit RGBA buffer to be filled.
//! @param width The width of the image in pixels.
//! @param height The height of the image in pixels.
//! @param alpha The alpha value set for every pixel in the region
void GenerateSwizzledRGBMaxContrastNoisePattern(void *target, int width, int height, uint32_t seed_rgb = 0,
                                                uint8_t alpha = 0xFF);

}  // namespace PBKitPlusPlus

#endif  // PBKITPLUSPLUS_TEXTURE_GENERATOR_H
