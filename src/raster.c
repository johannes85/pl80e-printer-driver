#include "raster.h"
#include <stdio.h>
#include <string.h>

uint32_t clean_dots(uint32_t tenths_mm) {
    return (tenths_mm * 203u + 127u) / 254u;
}

int clean_valid_options(const CleanOptions *o) {
    return o && o->width_tenths_mm >= 200 && o->width_tenths_mm <= 1000 &&
        o->height_tenths_mm >= 100 && o->height_tenths_mm <= 3000 &&
        o->gap_tenths_mm <= 100 && o->density >= 1 && o->density <= 15 &&
        o->speed >= 2 && o->speed <= 6 && o->copies >= 1 && o->copies <= 99 &&
        (o->landscape == 0 || o->landscape == 1) && o->media<=2;
}

static int valid_raster(const CleanRaster *r) {
    if (!r || !r->scan0 || !r->width || !r->height ||
        r->width > 2400 || r->height > 2400) return 0;
    /* The bounds keep signed multiplication below PTRDIFF_MAX on Win64. */
    if (r->stride == PTRDIFF_MIN) return 0;
    ptrdiff_t abs_stride = r->stride < 0 ? -r->stride : r->stride;
    return abs_stride >= (ptrdiff_t)((r->width+7)/8)
        && abs_stride <= 1024;
}

int clean_pack_row(const CleanRaster *r, int landscape, uint32_t y,
                   uint8_t *out, size_t size) {
    if (!valid_raster(r) || !out || (landscape != 0 && landscape != 1)) return 0;
    uint32_t width = landscape ? r->height : r->width;
    uint32_t height = landscape ? r->width : r->height;
    size_t bytes = (width + 7u) / 8u;
    if (y >= height || size < bytes) return 0;
    /* Repacking removes DIB DWORD padding, never image rows. Trailing bits
     * outside the paper are white even if the DIB padding contains garbage. */
    memset(out, 0xff, bytes);
    for (uint32_t x = 0; x < width; ++x) {
        uint32_t sx = landscape ? y : x;
        uint32_t sy = landscape ? r->height - 1 - x : y;
        const uint8_t *row = r->scan0 + (ptrdiff_t)sy * r->stride;
        if (!(row[sx / 8] & (0x80u >> (sx % 8))))
            out[x / 8] &= (uint8_t)~(0x80u >> (x % 8));
    }
    return 1;
}

int clean_emit_page(const CleanRaster *r, const CleanOptions *o,
                    CleanWrite write, void *context) {
    if (!clean_valid_options(o) || !valid_raster(r) || !write) return 0;
    uint32_t width = clean_dots(o->width_tenths_mm);
    uint32_t height = clean_dots(o->height_tenths_mm);
    if (r->width != (o->landscape ? height : width) ||
        r->height != (o->landscape ? width : height)) return 0;
    size_t row_bytes = (width + 7u) / 8u;
    char header[512];
    /* Commands and 0=black polarity follow the installed PL80E driver.
     * No TSPL command unsupported by that observed driver is required. */
    int length = snprintf(header, sizeof(header),
        "SIZE %u.%u mm, %u.%u mm\r\n%s %u.%u mm,0 mm\r\n"
        "DIRECTION 0,0\r\nREFERENCE 0,0\r\nOFFSET 0 mm\r\n"
        "SET PEEL OFF\r\nSET CUTTER OFF\r\nSET TEAR ON\r\nSET RIBBON OFF\r\n"
        "DENSITY %u\r\nSPEED %u\r\nCLS\r\nBITMAP 0,0,%u,%u,1,",
        o->width_tenths_mm/10, o->width_tenths_mm%10,
        o->height_tenths_mm/10, o->height_tenths_mm%10,
        o->media==1?"BLINE":"GAP",
        o->media==2?0:o->gap_tenths_mm/10, o->media==2?0:o->gap_tenths_mm%10,
        o->density, o->speed, (unsigned)row_bytes, height);
    if (length < 0 || (size_t)length >= sizeof(header) ||
        !write(context, (const uint8_t *)header, (size_t)length)) return 0;
    uint8_t row[100]; /* maximum: 100 mm at 203 dpi -> 799 pixels */
    for (uint32_t y = 0; y < height; ++y) {
        if (!clean_pack_row(r, o->landscape, y, row, sizeof(row)) ||
            !write(context, row, row_bytes)) return 0;
    }
    length = snprintf(header, sizeof(header), "\r\nPRINT 1,%u\r\n", o->copies);
    return length > 0 && (size_t)length < sizeof(header) &&
        write(context, (const uint8_t *)header, (size_t)length);
}
