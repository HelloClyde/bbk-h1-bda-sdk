/* Native 480x272 shapes: bounded integer geometry, RGB565, no runtime assets. */
#define UI_RGB(v) ((uint16_t)((((v)>>19)&31)<<11|(((v)>>10)&63)<<5|(((v)>>3)&31)))
#define UI_BG UI_RGB(0xf2f2f7)
#define UI_INK UI_RGB(0x1c1c1e)
#define UI_SUB UI_RGB(0x63636b)
#define UI_LINE UI_RGB(0xe5e5ea)
#define UI_BLUE UI_RGB(0x007aff)
#define UI_TINT UI_RGB(0xe9f2ff)
#define UI_GREEN UI_RGB(0x248a3d)
#define UI_RED UI_RGB(0xc93434)
#define UI_WHITE ((uint16_t)0xffff)
static void ui_round(int x,int y,int w,int h,int radius,uint16_t color)
{
    for(int row=0;row<h;row++){
        int d=row<radius?radius-1-row:row>=h-radius?row-h+radius:0,inset=0;
        if(d)while(inset<radius&&(radius-inset)*(radius-inset)+d*d>radius*radius)inset++;
        rect(x+inset,y+row,w-2*inset,1,color);
    }
}
static void ui_dot(int x,int y,int radius,uint16_t color)
{
    for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++)
        if(dx*dx+dy*dy<=radius*radius)rect(x+dx,y+dy,1,1,color);
}
static void ui_line(int x,int y,int x2,int y2,uint16_t color)
{
    int dx=x2>x?x2-x:x-x2,sx=x<x2?1:-1,dy=y2>y?y-y2:y2-y,sy=y<y2?1:-1,err=dx+dy;
    for(;;){rect(x,y,1,1,color);if(x==x2&&y==y2)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
static void ui_chevron(int x,int y,int back,uint16_t color)
{
    int end=back?x-4:x+4;ui_line(x,y,end,y+4,color);ui_line(end,y+4,x,y+8,color);
    ui_line(x+1,y,end+1,y+4,color);ui_line(end+1,y+4,x+1,y+8,color);
}
static void ui_check(int x,int y,uint16_t color)
{ui_line(x,y+3,x+3,y+6,color);ui_line(x+3,y+6,x+9,y,color);ui_line(x,y+4,x+3,y+7,color);ui_line(x+3,y+7,x+9,y+1,color);}
static void ui_wifi(int x,int y,int size,unsigned level,uint16_t color,uint16_t muted)
{
    int cx=x+size/2,cy=y+size-2,outer=size*5/8,thickness=size>=30?3:2;
    for(int py=y;py<y+size;py++)for(int px=x;px<x+size;px++){
        int dx=px-cx,dy=py-cy,d=dx*dx+dy*dy,ax=dx<0?-dx:dx;
        if(dy>=0||ax*3>-dy*4)continue;
        for(unsigned k=1;k<=3;k++){int r=outer*(int)k/3,inner=r-thickness;if(inner<0)inner=0;
            if(d<=r*r&&d>=inner*inner){rect(px,py,1,1,k<=level?color:muted);break;}}
    }
    ui_dot(cx,cy,size>=30?2:1,color);
}
static void ui_lock(int x,int y,uint16_t color)
{
    ui_round(x+1,y,6,7,3,color);rect(x+3,y+2,2,4,UI_WHITE);
    ui_round(x,y+5,8,7,2,color);rect(x+3,y+7,2,3,UI_WHITE);
}
static int ui_width(const char *s)
{int width=0;while(*s){unsigned cp=(unsigned)bu_utf8_string(&s);width+=(int)bu_advance(cp);}return width;}
static void ui_center(const char *s,int x,int y,int width,uint16_t color)
{int n=ui_width(s);text(s,x+(n<width?(width-n)/2:0),y,x+width,color);}
static void ui_bold(const char *s,int x,int y,int right,uint16_t color)
{text(s,x,y,right,color);text(s,x+1,y,right,color);}
static void ui_button(const char *label,int x,int width,uint16_t fill,uint16_t ink)
{ui_round(x,253,width,18,5,fill);ui_center(label,x,254,width,ink);}
