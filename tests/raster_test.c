#include "../src/raster.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *bytes; size_t used, capacity; int fail_after; } Buffer;
static int collect(void *ctx,const uint8_t *data,size_t count) {
    Buffer *b=ctx;
    if(b->fail_after>=0 && b->used+count>(size_t)b->fail_after) return 0;
    assert(b->used+count <= b->capacity);
    memcpy(b->bytes+b->used,data,count); b->used+=count;
    return 1;
}
static void ink(uint8_t *r,size_t stride,unsigned x,unsigned y) {
    r[y*stride+x/8] &= (uint8_t)~(0x80u>>(x%8));
}
static size_t payload_offset(Buffer *b,const char *needle) {
    for(size_t i=0;i+strlen(needle)<=b->used;i++)
        if(!memcmp(b->bytes+i,needle,strlen(needle))) return i+strlen(needle);
    assert(!"bitmap header missing"); return 0;
}
int main(int argc,char **argv) {
    assert(clean_dots(1000)==799 && clean_dots(1500)==1199);
    CleanOptions o={1000,1500,30,10,5,2,0,0};
    uint8_t *dib=malloc(100*1199), *output=malloc(130000);
    assert(dib && output);
    memset(dib,0xff,100*1199);
    /* Different pixel on EVERY row detects duplicate, omitted or reordered
     * rows, including the old driver's >400-row correction and blank runs. */
    for(unsigned y=0;y<1199;y++) ink(dib,100,y%799,y);
    CleanRaster r={dib,100,799,1199};
    Buffer b={output,0,130000,-1};
    assert(clean_emit_page(&r,&o,collect,&b));
    size_t offset=payload_offset(&b,"BITMAP 0,0,100,1199,1,");
    assert(!memcmp(output+offset,dib,119900));
    assert(b.used==offset+119900+strlen("\r\nPRINT 1,2\r\n"));
    assert(!memcmp(output+offset+119900,"\r\nPRINT 1,2\r\n",13));
    if(argc>1) { FILE *f=fopen(argv[1],"wb"); assert(f); assert(fwrite(output,1,b.used,f)==b.used); fclose(f); }
    for(unsigned media=0;media<=2;media++) {
        o.media=media; o.gap_tenths_mm=45; b.used=0;
        assert(clean_emit_page(&r,&o,collect,&b));
        char *command=media==1?"BLINE 4.5 mm,0 mm\r\n":media==2?"GAP 0.0 mm,0 mm\r\n":"GAP 4.5 mm,0 mm\r\n";
        assert(payload_offset(&b,command)>0);
    }
    o.media=3; assert(!clean_valid_options(&o)); o.media=0; o.gap_tenths_mm=30;

    /* Long blank stretches are preserved, not vertically compacted. */
    memset(dib,0xff,100*1199); ink(dib,100,400,1100);
    b.used=0; assert(clean_emit_page(&r,&o,collect,&b));
    offset=payload_offset(&b,"BITMAP 0,0,100,1199,1,");
    assert(!memcmp(output+offset,dib,119900));

    /* 3x2 landscape source rotates clockwise to 2x3; no interpolation. */
    uint8_t tiny[]={0x5f,0xaa,0xe0,0xbb};
    CleanRaster t={tiny,2,3,2}; uint8_t packed[1];
    assert(clean_pack_row(&t,1,0,packed,1) && packed[0]==0xbf);
    assert(clean_pack_row(&t,1,1,packed,1) && packed[0]==0xff);
    assert(clean_pack_row(&t,1,2,packed,1) && packed[0]==0xbf);
    /* Signed DIB stride: pvScan0 always identifies the visual top row. */
    CleanRaster bottom_up={tiny+2,-2,3,2};
    assert(clean_pack_row(&bottom_up,0,0,packed,1) && packed[0]==0xff);
    assert(clean_pack_row(&bottom_up,0,1,packed,1) && packed[0]==0x5f);
    /* Bits beyond valid width are white regardless of source padding. */
    uint8_t edge[]={0,0}; CleanRaster odd={edge,2,9,1}; uint8_t row[2];
    assert(clean_pack_row(&odd,0,0,row,2) && row[0]==0 && row[1]==0x7f);

    CleanOptions bad=o; bad.copies=0; b.used=0;
    assert(!clean_emit_page(&r,&bad,collect,&b) && b.used==0);
    bad=o; bad.width_tenths_mm=1100;
    assert(!clean_emit_page(&r,&bad,collect,&b) && b.used==0);
    CleanRaster bad_r=r; bad_r.stride=PTRDIFF_MIN;
    assert(!clean_pack_row(&bad_r,0,0,row,2));
    bad_r=r; bad_r.height--;
    assert(!clean_emit_page(&bad_r,&o,collect,&b) && b.used==0);
    b.fail_after=1000;
    assert(!clean_emit_page(&r,&o,collect,&b));
    /* Failure must not be followed by PRINT, so no partial label is committed. */
    assert(b.used<1000);
    free(output); free(dib);
    puts("PASS: all 1199 rows, blank spacing, rotation, stride, padding, bounds, failed writes");
    return 0;
}
