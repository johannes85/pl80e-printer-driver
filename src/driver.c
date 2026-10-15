/* PL80E Clean: native, user-mode Windows GDI printer renderer.
 * GDI draws onto one complete 1-bpp bitmap at 203 dpi. No proprietary
 * rendering DLL, raster rescaling, gap compression or row deletion is used. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winspool.h>
#include <winddi.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include "raster.h"
#include "settings.h"

#define EXPORT
#define PDEV_MAGIC 0x504c434eu

typedef struct {
    DWORD magic;
    HANDLE printer;              /* borrowed from GDI; never ClosePrinter */
    HDEV engine;
    HSURF surface;
    HPALETTE palette;
    HSURF patterns[6];
    BYTE pattern_bits[6][32];
    CleanOptions options;
    LONG width, height;
    BOOL page_started;
    BOOL failed;
} CleanPdev;

static CleanPdev *from_surface(SURFOBJ *s) {
    CleanPdev *p = s ? (CleanPdev *)s->dhpdev : NULL;
    if (!p || p->magic != PDEV_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
    return p;
}

static DWORD printer_setting(HANDLE printer, const WCHAR *name, DWORD fallback,
                             DWORD minimum, DWORD maximum) {
    DWORD type = 0, value = 0, needed = 0;
    if (!printer || GetPrinterDataW(printer, (LPWSTR)name, &type,
            (LPBYTE)&value, sizeof(value), &needed) != ERROR_SUCCESS ||
        type != REG_DWORD || needed != sizeof(value) || value < minimum || value > maximum)
        return fallback;
    return value;
}

static void release_surfaces(CleanPdev *p) {
    if (p->surface) { EngDeleteSurface(p->surface); p->surface = NULL; }
    for (unsigned i = 0; i < 6; ++i) {
        if (p->patterns[i]) { EngDeleteSurface(p->patterns[i]); p->patterns[i] = NULL; }
    }
}

EXPORT DHPDEV APIENTRY DrvEnablePDEV(DEVMODEW *dm, LPWSTR address,
    ULONG pattern_count, HSURF *patterns, ULONG caps_size, ULONG *caps,
    ULONG info_size, DEVINFO *info, HDEV device, LPWSTR name, HANDLE printer) {
    (void)address; (void)device; (void)name;
    if (!caps || !info || caps_size < offsetof(GDIINFO, ulHTOutputFormat)+sizeof(ULONG) ||
        info_size < offsetof(DEVINFO, hpalDefault)+sizeof(HPALETTE)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER); return NULL;
    }
    CleanPdev *p = (CleanPdev *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    p->magic = PDEV_MAGIC;
    p->printer = printer;
    p->options = (CleanOptions){1000, 1500, 30, 10, 5, 1, 0, 0};
    if (dm && dm->dmSize >= offsetof(DEVMODEW, dmYResolution)+sizeof(short)) {
        if ((dm->dmFields & DM_PAPERWIDTH) && dm->dmPaperWidth > 0)
            p->options.width_tenths_mm = (uint32_t)dm->dmPaperWidth;
        if ((dm->dmFields & DM_PAPERLENGTH) && dm->dmPaperLength > 0)
            p->options.height_tenths_mm = (uint32_t)dm->dmPaperLength;
        if (dm->dmFields & DM_ORIENTATION) {
            if (dm->dmOrientation != DMORIENT_PORTRAIT && dm->dmOrientation != DMORIENT_LANDSCAPE)
                goto invalid;
            p->options.landscape = dm->dmOrientation == DMORIENT_LANDSCAPE;
        }
        if (dm->dmFields & DM_COPIES) {
            if (dm->dmCopies < 1) goto invalid;
            p->options.copies = (uint32_t)dm->dmCopies;
        }
        if ((dm->dmFields & DM_PRINTQUALITY) && dm->dmPrintQuality > 0 && dm->dmPrintQuality != 203)
            goto invalid;
        if ((dm->dmFields & DM_YRESOLUTION) && dm->dmYResolution > 0 && dm->dmYResolution != 203)
            goto invalid;
    }
    p->options.gap_tenths_mm = printer_setting(printer,L"CleanGapTenthsMm",30,0,100);
    p->options.density = printer_setting(printer,L"CleanDensity",10,1,15);
    p->options.speed = printer_setting(printer,L"CleanSpeed",5,2,6);
    const CleanPrivate *settings=clean_private(dm);
    if(settings) p->options.speed=settings->speed;
    if(settings && dm->dmDriverExtra>=12) p->options.density=settings->density;
    if(settings && dm->dmDriverExtra==sizeof(CleanPrivate)) {
        p->options.media=settings->media; p->options.gap_tenths_mm=settings->gap;
    }
    if (!clean_valid_options(&p->options)) goto invalid;
    uint32_t physical_width = clean_dots(p->options.width_tenths_mm);
    uint32_t physical_height = clean_dots(p->options.height_tenths_mm);
    p->width = (LONG)(p->options.landscape ? physical_height : physical_width);
    p->height = (LONG)(p->options.landscape ? physical_width : physical_height);

    ULONG colors[2] = {0x00000000u, 0x00ffffffu};
    p->palette = EngCreatePalette(PAL_INDEXED, 2, colors, 0, 0, 0);
    if (!p->palette) goto failed;
    GDIINFO g = {0};
    g.ulVersion = 0x6001;
    g.ulTechnology = DT_RASPRINTER;
    /* Negative physical sizes express micrometres without integer-mm truncation. */
    g.ulHorzSize = (ULONG)-(LONG)((p->options.landscape ? p->options.height_tenths_mm :
                                p->options.width_tenths_mm) * 100u);
    g.ulVertSize = (ULONG)-(LONG)((p->options.landscape ? p->options.width_tenths_mm :
                                p->options.height_tenths_mm) * 100u);
    g.ulHorzRes = (ULONG)p->width; g.ulVertRes = (ULONG)p->height;
    g.cBitsPixel = 1; g.cPlanes = 1; g.ulNumColors = 2;
    g.ulLogPixelsX = 203; g.ulLogPixelsY = 203;
    g.ulAspectX = 36; g.ulAspectY = 36; g.ulAspectXY = 51;
    g.xStyleStep = 1; g.yStyleStep = 1; g.denStyleStep = 3;
    g.szlPhysSize.cx = p->width; g.szlPhysSize.cy = p->height;
    g.ulNumPalReg = 2;
    g.ciDevice.Red = (CIECHROMA){6700,3300,0};
    g.ciDevice.Green = (CIECHROMA){2100,7100,0};
    g.ciDevice.Blue = (CIECHROMA){1400,800,0};
    g.ciDevice.Cyan = (CIECHROMA){1750,3950,0};
    g.ciDevice.Magenta = (CIECHROMA){4050,2050,0};
    g.ciDevice.Yellow = (CIECHROMA){4400,5200,0};
    g.ciDevice.AlignmentWhite = (CIECHROMA){3127,3290,10000};
    g.ciDevice.RedGamma = g.ciDevice.GreenGamma = g.ciDevice.BlueGamma = 20000;
    g.ulDevicePelsDPI = 203;
    g.ulPrimaryOrder = PRIMARY_ORDER_ABC;
    g.ulHTPatternSize = HT_PATSIZE_2x2_M;
    g.ulHTOutputFormat = HT_FORMAT_1BPP;
    g.flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
    memcpy(caps, &g, caps_size < sizeof(g) ? caps_size : sizeof(g));

    DEVINFO d = {0};
    d.flGraphicsCaps = GCAPS_BEZIERS | GCAPS_GEOMETRICWIDE | GCAPS_ALTERNATEFILL |
        GCAPS_WINDINGFILL | GCAPS_OPAQUERECT | GCAPS_MONO_DITHER;
    d.lfDefaultFont.lfHeight = -28;
    d.lfDefaultFont.lfWeight = FW_NORMAL;
    d.lfDefaultFont.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(d.lfDefaultFont.lfFaceName, L"Arial");
    d.lfAnsiVarFont = d.lfDefaultFont;
    d.lfAnsiFixFont = d.lfDefaultFont;
    lstrcpyW(d.lfAnsiFixFont.lfFaceName, L"Consolas");
    d.iDitherFormat = BMF_1BPP;
    d.cxDither = 8; d.cyDither = 8;
    d.hpalDefault = p->palette;
    memcpy(info, &d, info_size < sizeof(d) ? info_size : sizeof(d));

    if (patterns) {
        for (ULONG i = 0; i < pattern_count && i < 6; ++i) {
            memset(p->pattern_bits[i], 0xff, 32);
            for (unsigned y=0; y<8; ++y) for(unsigned x=0; x<8; ++x) {
                int ink = (i==0 && y==4) || (i==1 && x==4) ||
                    (i==2 && x+y==7) || (i==3 && x==y) ||
                    (i==4 && (x==4 || y==4)) || (i==5 && (x==y || x+y==7));
                if(ink) p->pattern_bits[i][y*4] &= (BYTE)~(0x80u>>x);
            }
            SIZEL size = {8,8};
            p->patterns[i] = (HSURF)EngCreateBitmap(size,4,BMF_1BPP,BMF_TOPDOWN,p->pattern_bits[i]);
            if (!p->patterns[i]) goto failed;
            patterns[i] = p->patterns[i];
        }
    }
    return (DHPDEV)p;
invalid:
    SetLastError(ERROR_INVALID_PARAMETER);
failed:
    release_surfaces(p);
    if (p->palette) EngDeletePalette(p->palette);
    HeapFree(GetProcessHeap(),0,p);
    return NULL;
}

EXPORT VOID APIENTRY DrvCompletePDEV(DHPDEV handle, HDEV device) {
    CleanPdev *p = (CleanPdev *)handle;
    if (p && p->magic == PDEV_MAGIC) p->engine = device;
}

EXPORT VOID APIENTRY DrvDisableSurface(DHPDEV handle) {
    CleanPdev *p = (CleanPdev *)handle;
    if (p && p->magic == PDEV_MAGIC && p->surface) {
        EngDeleteSurface(p->surface); p->surface = NULL;
    }
}

EXPORT VOID APIENTRY DrvDisablePDEV(DHPDEV handle) {
    CleanPdev *p = (CleanPdev *)handle;
    if (!p || p->magic != PDEV_MAGIC) return;
    release_surfaces(p);
    if (p->palette) EngDeletePalette(p->palette);
    p->magic = 0;
    HeapFree(GetProcessHeap(),0,p);
}

EXPORT HSURF APIENTRY DrvEnableSurface(DHPDEV handle) {
    CleanPdev *p = (CleanPdev *)handle;
    if (!p || p->magic != PDEV_MAGIC || !p->engine || p->surface) return NULL;
    SIZEL size = {p->width,p->height};
    p->surface = (HSURF)EngCreateBitmap(size,0,BMF_1BPP,BMF_TOPDOWN,NULL);
    if (!p->surface) return NULL;
    /* No drawing hooks: Windows draws fonts/vectors directly at native DPI. */
    if (!EngAssociateSurface(p->surface,p->engine,0)) {
        EngDeleteSurface(p->surface); p->surface=NULL;
    }
    return p->surface;
}

EXPORT BOOL APIENTRY DrvStartDoc(SURFOBJ *s, LPWSTR name, DWORD job) {
    (void)name; (void)job;
    CleanPdev *p = from_surface(s);
    if (!p) return FALSE;
    p->failed = FALSE; p->page_started = FALSE;
    return TRUE;
}

EXPORT BOOL APIENTRY DrvStartPage(SURFOBJ *s) {
    CleanPdev *p = from_surface(s);
    if (!p || p->failed || !s->pvScan0 || s->iBitmapFormat != BMF_1BPP ||
        s->sizlBitmap.cx != p->width || s->sizlBitmap.cy != p->height) return FALSE;
    LONG stride = s->lDelta;
    LONG bytes = (p->width+7)/8;
    if (stride == LONG_MIN || (stride < 0 ? -stride : stride) < bytes) return FALSE;
    for (LONG y=0; y<p->height; ++y)
        memset((BYTE *)s->pvScan0+(ptrdiff_t)y*stride,0xff,(size_t)bytes);
    p->page_started = TRUE;
    return TRUE;
}

typedef struct { CleanPdev *p; SURFOBJ *surface; } SpoolContext;

static int spool_write(void *context, const uint8_t *bytes, size_t count) {
    SpoolContext *c = (SpoolContext *)context;
    if (c->p->failed || EngCheckAbort(c->surface)) {
        SetLastError(ERROR_PRINT_CANCELLED); c->p->failed=TRUE; return 0;
    }
    while (count) {
        DWORD written=0;
        DWORD chunk = count > 32768 ? 32768 : (DWORD)count;
        if (!WritePrinter(c->p->printer,(LPVOID)bytes,chunk,&written) ||
            written==0 || written>chunk) {
            c->p->failed=TRUE;
            if (written==0) SetLastError(ERROR_WRITE_FAULT);
            return 0;
        }
        bytes += written; count -= written;
    }
    return 1;
}

EXPORT BOOL APIENTRY DrvSendPage(SURFOBJ *s) {
    CleanPdev *p = from_surface(s);
    if (!p || !p->page_started || p->failed) return FALSE;
    CleanRaster raster = {(const uint8_t *)s->pvScan0, s->lDelta,
                           (uint32_t)s->sizlBitmap.cx,(uint32_t)s->sizlBitmap.cy};
    SpoolContext c = {p,s};
    BOOL success = clean_emit_page(&raster,&p->options,spool_write,&c) ? TRUE : FALSE;
    p->page_started=FALSE;
    if (!success) p->failed=TRUE;
    return success;
}

EXPORT BOOL APIENTRY DrvEndDoc(SURFOBJ *s, FLONG flags) {
    CleanPdev *p = from_surface(s);
    if (!p) return FALSE;
    p->page_started=FALSE;
    return (flags & ED_ABORTDOC) ? TRUE : !p->failed;
}

EXPORT BOOL APIENTRY DrvResetPDEV(DHPDEV old_handle, DHPDEV new_handle) {
    CleanPdev *old = (CleanPdev *)old_handle, *next = (CleanPdev *)new_handle;
    if (!old || !next || old->magic != PDEV_MAGIC || next->magic != PDEV_MAGIC) return FALSE;
    next->failed = old->failed;
    return TRUE;
}

static DRVFN callbacks[] = {
    {INDEX_DrvEnablePDEV,(PFN)DrvEnablePDEV},
    {INDEX_DrvCompletePDEV,(PFN)DrvCompletePDEV},
    {INDEX_DrvDisablePDEV,(PFN)DrvDisablePDEV},
    {INDEX_DrvEnableSurface,(PFN)DrvEnableSurface},
    {INDEX_DrvDisableSurface,(PFN)DrvDisableSurface},
    {INDEX_DrvResetPDEV,(PFN)DrvResetPDEV},
    {INDEX_DrvStartDoc,(PFN)DrvStartDoc},
    {INDEX_DrvSendPage,(PFN)DrvSendPage},
    {INDEX_DrvEndDoc,(PFN)DrvEndDoc},
    {INDEX_DrvStartPage,(PFN)DrvStartPage}
};

EXPORT BOOL APIENTRY DrvEnableDriver(ULONG version, ULONG size, DRVENABLEDATA *data) {
    if (!data || size < sizeof(*data) || version < DDI_DRIVER_VERSION_NT5) return FALSE;
    data->iDriverVersion = DDI_DRIVER_VERSION_NT5;
    data->c = sizeof(callbacks)/sizeof(callbacks[0]);
    data->pdrvfn = callbacks;
    return TRUE;
}
EXPORT VOID APIENTRY DrvDisableDriver(VOID) {}
EXPORT BOOL APIENTRY DrvQueryDriverInfo(DWORD mode, PVOID buffer, DWORD size, PDWORD needed) {
    if (needed) *needed = 0;
    if (mode != DRVQUERY_USERMODE) return FALSE;
    if (needed) *needed = sizeof(DWORD);
    if (!buffer || size < sizeof(DWORD)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    *(DWORD *)buffer=TRUE;
    return TRUE;
}
