#ifndef PL80E_RASTER_H
#define PL80E_RASTER_H
#include <stddef.h>
#include <stdint.h>

/* Coordinates refer to a top-down, MSB-first 1-bpp DIB: 0 black, 1 white.
 * stride is signed so bottom-up engine surfaces are supported too. */
typedef struct {
    const uint8_t *scan0;
    ptrdiff_t stride;
    uint32_t width, height;
} CleanRaster;

typedef struct {
    uint32_t width_tenths_mm, height_tenths_mm;
    uint32_t gap_tenths_mm, density, speed, copies;
    int landscape;
    uint32_t media; /* 0 gap, 1 black mark, 2 continuous */
} CleanOptions;

typedef int (*CleanWrite)(void *context, const uint8_t *data, size_t size);

uint32_t clean_dots(uint32_t tenths_mm);
int clean_valid_options(const CleanOptions *options);
int clean_pack_row(const CleanRaster *source, int landscape, uint32_t y,
                   uint8_t *out, size_t size);
int clean_emit_page(const CleanRaster *source, const CleanOptions *options,
                    CleanWrite write, void *context);
#endif
