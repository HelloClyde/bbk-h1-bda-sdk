/* Bounded still images: reduced JPEG IDCT, streamed PNG rows, no browser. */
#include "h1_image.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include "jpeglib.h"
#include "png.h"

static int dimensions(unsigned w,unsigned h,unsigned mw,unsigned mh,unsigned *ow,unsigned *oh)
{
    if(!w||!h||w>4096||h>4096||!mw||!mh||mw>480||mh>272)return 0;
    unsigned a=w,b=h;
    if(a>mw){b=(unsigned)((uint64_t)b*mw/a);a=mw;if(!b)b=1;}
    if(b>mh){a=(unsigned)((uint64_t)a*mh/b);b=mh;if(!a)a=1;}
    *ow=a;*oh=b;return 1;
}
static uint16_t rgb(unsigned r,unsigned g,unsigned b)
{return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}

typedef struct {struct jpeg_error_mgr base;jmp_buf jump;} jpg_error;
typedef struct {
    struct jpeg_decompress_struct codec;
    jpg_error error;
    unsigned char *row;
    uint16_t *pixels;
    unsigned created,w,h;
} jpg_state;
static void jpg_failed(j_common_ptr codec)
{jpg_error *error=(jpg_error *)codec->err;longjmp(error->jump,1);}
static void jpg_message(j_common_ptr codec,int level)
{if(level<0)jpg_failed(codec);}
static int jpeg_image(const unsigned char *data,unsigned size,uint16_t **out,unsigned *w,unsigned *h,unsigned mw,unsigned mh)
{
    jpg_state *s=calloc(1,sizeof *s);int ok=0;if(!s)return -1;
    s->codec.err=jpeg_std_error(&s->error.base);s->error.base.error_exit=jpg_failed;s->error.base.emit_message=jpg_message;
    if(setjmp(s->error.jump))goto done;
    s->created=1;jpeg_create_decompress(&s->codec);jpeg_mem_src(&s->codec,data,size);
    if(jpeg_read_header(&s->codec,TRUE)!=JPEG_HEADER_OK||!dimensions(s->codec.image_width,s->codec.image_height,mw,mh,&s->w,&s->h))goto done;
    s->codec.scale_num=1;s->codec.scale_denom=1;
    for(unsigned d=2;d<=8;d*=2)
        if((s->codec.image_width+d-1)/d>=s->w&&(s->codec.image_height+d-1)/d>=s->h)s->codec.scale_denom=d;
    s->codec.out_color_space=JCS_RGB;s->codec.dct_method=JDCT_IFAST;s->codec.do_fancy_upsampling=FALSE;
    if(!jpeg_start_decompress(&s->codec)||s->codec.output_components!=3)goto done;
    unsigned sw=s->codec.output_width,sh=s->codec.output_height;
    s->row=malloc(sw*3);s->pixels=malloc(s->w*s->h*2);if(!s->row||!s->pixels)goto done;
    unsigned next=0;
    while(s->codec.output_scanline<sh){
        unsigned y=s->codec.output_scanline;JSAMPROW row=s->row;
        if(jpeg_read_scanlines(&s->codec,&row,1)!=1)goto done;
        while(next<s->h&&next*sh/s->h==y){
            for(unsigned x=0;x<s->w;x++){unsigned sx=x*sw/s->w;unsigned char *p=s->row+sx*3;s->pixels[next*s->w+x]=rgb(p[0],p[1],p[2]);}
            next++;
        }
    }
    if(next!=s->h||!jpeg_finish_decompress(&s->codec))goto done;
    *out=s->pixels;*w=s->w;*h=s->h;s->pixels=NULL;ok=1;
done:
    if(s->created)jpeg_destroy_decompress(&s->codec);free(s->row);free(s->pixels);free(s);return ok?0:-1;
}

typedef struct {
    png_structp png;png_infop info;
    const unsigned char *data;unsigned size,used;
    unsigned char *selected,*scratch;
    uint16_t *pixels;
    unsigned w,h;
} png_state;
static void png_failed(png_structp png,const char *message)
{(void)message;png_longjmp(png,1);}
static void png_warning_ignored(png_structp png,const char *message)
{(void)png;(void)message;}
static void png_input(png_structp png,png_bytep out,png_size_t count)
{
    png_state *s=png_get_io_ptr(png);if(count>s->size-s->used)png_error(png,"short input");
    memcpy(out,s->data+s->used,count);s->used+=(unsigned)count;
}
static int decode_png(const unsigned char *data,unsigned size,uint16_t **out,unsigned *w,unsigned *h,unsigned mw,unsigned mh)
{
    png_state *s=calloc(1,sizeof *s);int ok=0;if(!s)return -1;s->data=data;s->size=size;
    s->png=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,png_failed,png_warning_ignored);if(!s->png)goto done;
    if(setjmp(png_jmpbuf(s->png)))goto done;
    s->info=png_create_info_struct(s->png);if(!s->info)goto done;
    png_set_read_fn(s->png,s,png_input);png_set_user_limits(s->png,4096,4096);
    png_set_chunk_malloc_max(s->png,262144);png_set_crc_action(s->png,PNG_CRC_ERROR_QUIT,PNG_CRC_ERROR_QUIT);
    png_read_info(s->png,s->info);
    unsigned sw=png_get_image_width(s->png,s->info),sh=png_get_image_height(s->png,s->info);
    if(!dimensions(sw,sh,mw,mh,&s->w,&s->h))goto done;
    int type=png_get_color_type(s->png,s->info),depth=png_get_bit_depth(s->png,s->info);
    if(depth==16)png_set_strip_16(s->png);
    if(type==PNG_COLOR_TYPE_PALETTE)png_set_palette_to_rgb(s->png);
    if(type==PNG_COLOR_TYPE_GRAY&&depth<8)png_set_expand_gray_1_2_4_to_8(s->png);
    if(png_get_valid(s->png,s->info,PNG_INFO_tRNS))png_set_tRNS_to_alpha(s->png);
    if(type==PNG_COLOR_TYPE_GRAY||type==PNG_COLOR_TYPE_GRAY_ALPHA)png_set_gray_to_rgb(s->png);
    if(!(type&PNG_COLOR_MASK_ALPHA)&&!png_get_valid(s->png,s->info,PNG_INFO_tRNS))png_set_add_alpha(s->png,255,PNG_FILLER_AFTER);
    int passes=png_set_interlace_handling(s->png);png_read_update_info(s->png,s->info);
    unsigned stride=(unsigned)png_get_rowbytes(s->png,s->info);if(stride!=sw*4)goto done;
    s->selected=calloc(s->h,stride);s->scratch=calloc(1,stride);s->pixels=malloc(s->w*s->h*2);
    if(!s->selected||!s->scratch||!s->pixels)goto done;
    /* Retain only rows needed by the scaled output, including Adam7 passes. */
    for(int pass=0;pass<passes;pass++){
        unsigned next=0;
        for(unsigned y=0;y<sh;y++){
            if(next<s->h&&next*sh/s->h==y){png_read_row(s->png,s->selected+next*stride,NULL);next++;}
            else {memset(s->scratch,0,stride);png_read_row(s->png,s->scratch,NULL);}
        }
    }
    png_read_end(s->png,s->info);
    for(unsigned y=0;y<s->h;y++)for(unsigned x=0;x<s->w;x++){
        unsigned char *p=s->selected+y*stride+(x*sw/s->w)*4;unsigned a=p[3];
        s->pixels[y*s->w+x]=rgb((p[0]*a+255*(255-a)+127)/255,(p[1]*a+255*(255-a)+127)/255,(p[2]*a+255*(255-a)+127)/255);
    }
    *out=s->pixels;*w=s->w;*h=s->h;s->pixels=NULL;ok=1;
done:
    if(s->png)png_destroy_read_struct(&s->png,s->info?&s->info:NULL,NULL);
    free(s->selected);free(s->scratch);free(s->pixels);free(s);return ok?0:-1;
}
int h1_image_decode_rgb565(const unsigned char *data,unsigned size,uint16_t **out,unsigned *w,unsigned *h,unsigned mw,unsigned mh)
{
    if(!out||!w||!h)return -1;*out=NULL;*w=*h=0;
    if(!data||size<8||size>262144||!mw||!mh||mw>480||mh>272)return -1;
    if(data[0]==255&&data[1]==216)return jpeg_image(data,size,out,w,h,mw,mh);
    if(!png_sig_cmp(data,0,8))return decode_png(data,size,out,w,h,mw,mh);return -1;
}
