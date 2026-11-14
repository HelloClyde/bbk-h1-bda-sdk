/* SPDX-License-Identifier: Apache-2.0
 * Small example-local text helpers. Only %s, %u, %lu, %ld are needed. */
#include <stddef.h>
#include <stdarg.h>
static size_t wifi_strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
static void *wifi_memcpy(void *d,const void *s,size_t n)
{unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
static int wifi_strcmp(const char *a,const char *b)
{while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
static void append(char *out,size_t cap,size_t *n,char c){if(*n+1<cap)out[*n]=c;(*n)++;}
static int wifi_snprintf(char *out,size_t cap,const char *format,...)
{
    size_t n=0;va_list ap;va_start(ap,format);
    while(*format){
        if(*format!='%'){append(out,cap,&n,*format++);continue;}
        format++;int wide=*format=='l';if(wide)format++;
        if(*format=='s'){const char *p=va_arg(ap,const char *);while(*p)append(out,cap,&n,*p++);}
        else{
            unsigned long v;
            if(*format=='d'){long x=wide?va_arg(ap,long):va_arg(ap,int);
                if(x<0){append(out,cap,&n,'-');v=0ul-(unsigned long)x;}else v=(unsigned long)x;
            }else v=wide?va_arg(ap,unsigned long):va_arg(ap,unsigned);
            char digits[24];unsigned k=0;do{digits[k++]=(char)('0'+v%10u);v/=10u;}while(v);
            while(k)append(out,cap,&n,digits[--k]);
        }if(*format)format++;
    }
    va_end(ap);if(cap)out[n<cap?n:cap-1]=0;return (int)n;
}
#define strlen wifi_strlen
#define memcpy wifi_memcpy
#define strcmp wifi_strcmp
#define snprintf wifi_snprintf
