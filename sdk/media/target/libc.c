/* H1-specific libc system boundary. Formatting, conversion and mathematics use musl. */
#include "h1_sdk.h"
#include "platform.h"
#include "../third_party/musl-1.2.5/src/include/features.h"
#include "stdio_impl.h"
#undef syscall
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <locale.h>
#include <limits.h>
#include <stdint.h>
#include <setjmp.h>
static int error_number;
extern const struct __locale_struct __c_dot_utf8_locale;
locale_t h1_global_locale=(locale_t)&__c_dot_utf8_locale;
int *__errno_location(void){return &error_number;}
int *___errno_location(void){return &error_number;}
volatile uint32_t h1ns_log_size;
char h1ns_log[32768];
volatile uint32_t h1ns_diag_state[8]={0x31444e48,0};
volatile uint32_t h1ns_js_error_version;
char h1ns_js_error[721];
void h1ns_diag_event(unsigned stage,const char *source,int result,unsigned bytes)
{
    if(!h1ns_diag_state[1])return;
    h1ns_diag_state[2]=stage;++h1ns_diag_state[3];h1ns_diag_state[4]=(uint32_t)result;
    h1ns_diag_state[5]=bytes;h1ns_diag_state[6]=h1ns_time_ms();
    if(stage==14 || stage==15 || stage==16 || stage==18)++h1ns_diag_state[7];
    if(stage==16 || stage==18){++h1ns_js_error_version;strncpy(h1ns_js_error,source?source:"",720);h1ns_js_error[720]=0;++h1ns_js_error_version;}
    fprintf(stderr,"H1DIAG ms=%u stage=%u result=%d bytes=%u %.720s\n",
            (unsigned)h1ns_diag_state[6],stage,result,bytes,source?source:"");
}
void *malloc(size_t n){return h1ns_malloc(n);}
void free(void *p){h1ns_free(p);}
void *calloc(size_t n,size_t size){return h1ns_calloc(n,size);}
void *realloc(void *p,size_t n){return h1ns_realloc(p,n);}
static h1_file *handles[32];
static int path_convert(const char *input,char output[512])
{
    unsigned n=0;if(input[0]=='/' && input[1] && input[2]==':')++input;
    if(!(input[0] && input[1]==':')){output[n++]='A';output[n++]=':';if(*input!='/')output[n++]='\\';}
    while(*input && n<510){output[n++]=*input=='/'?'\\':*input;++input;}output[n]=0;
    if(*input){errno=ENAMETOOLONG;return 0;}return 1;
}
int open(const char *path,int flags,...)
{
    char name[512];if(!path_convert(path,name))return -1;
    const char *mode=(flags&O_APPEND)?"ab":(flags&O_TRUNC)?"wb":(flags&O_RDWR)?"r+b":"rb";
    int fd;for(fd=3;fd<32 && handles[fd];++fd){}if(fd==32){errno=EMFILE;return -1;}
    handles[fd]=h1_fopen(name,mode);if(!handles[fd] && (flags&O_CREAT))handles[fd]=h1_fopen(name,"w+b");
    if(!handles[fd]){errno=ENOENT;return -1;}return fd;
}
int close(int fd){if(fd<3 || fd>=32 || !handles[fd]){errno=EBADF;return -1;}int r=h1_fclose(handles[fd]);handles[fd]=NULL;return r;}
ssize_t read(int fd,void *data,size_t n){if(fd<3 || fd>=32 || !handles[fd]){errno=EBADF;return -1;}return h1_fread(data,1,n,handles[fd]);}
ssize_t write(int fd,const void *data,size_t n)
{
    if(fd==1 || fd==2){size_t left=sizeof(h1ns_log)-1-h1ns_log_size;size_t count=n<left?n:left;memcpy(h1ns_log+h1ns_log_size,data,count);h1ns_log_size+=count;h1ns_log[h1ns_log_size]=0;return n;}
    if(fd<3 || fd>=32 || !handles[fd]){errno=EBADF;return -1;}return h1_fwrite(data,1,n,handles[fd]);
}
off_t lseek(int fd,off_t offset,int whence){if(fd<3 || fd>=32 || !handles[fd] || offset>LONG_MAX || offset<LONG_MIN){errno=EINVAL;return -1;}if(h1_fseek(handles[fd],offset,whence)){errno=EIO;return -1;}return h1_ftell(handles[fd]);}
size_t __stdio_read(FILE *f,unsigned char *data,size_t n){ssize_t r=read(f->fd,data,n);if(r<0){f->flags|=F_ERR;return 0;}if(!r)f->flags|=F_EOF;return r;}
size_t __stdio_write(FILE *f,const unsigned char *data,size_t n)
{
    size_t buffered=f->wbase?(size_t)(f->wpos-f->wbase):0;
    if(buffered && write(f->fd,f->wbase,buffered)!=(ssize_t)buffered){f->flags|=F_ERR;return 0;}
    if(n && write(f->fd,data,n)!=(ssize_t)n){f->flags|=F_ERR;return 0;}
    f->wbase=f->wpos=f->buf;f->wend=f->buf+f->buf_size;return n;
}
size_t __stdout_write(FILE *f,const unsigned char *p,size_t n){return __stdio_write(f,p,n);}
off_t __stdio_seek(FILE *f,off_t n,int whence){return lseek(f->fd,n,whence);}
int __stdio_close(FILE *f){return close(f->fd);}
int __lockfile(FILE *f){(void)f;return 0;}void __unlockfile(FILE *f){(void)f;}
static FILE *ofl;
FILE **__ofl_lock(void){return &ofl;}void __ofl_unlock(void){}
static unsigned char outbuf[256],errbuf[256];
FILE __stdout_FILE={.flags=F_PERM|F_NORD,.fd=1,.lock=-1,.write=__stdio_write,.seek=__stdio_seek,.buf=outbuf,.buf_size=sizeof(outbuf),.lbf='\n'};
FILE __stderr_FILE={.flags=F_PERM|F_NORD,.fd=2,.lock=-1,.write=__stdio_write,.seek=__stdio_seek,.buf=errbuf,.buf_size=sizeof(errbuf),.lbf='\n'};
FILE __stdin_FILE={.flags=F_PERM|F_NOWR,.fd=0,.lock=-1,.read=__stdio_read};
FILE *const stdout=&__stdout_FILE,*const stderr=&__stderr_FILE,*const stdin=&__stdin_FILE;
FILE *volatile __stdout_used=&__stdout_FILE,*volatile __stderr_used=&__stderr_FILE,*volatile __stdin_used=&__stdin_FILE;
FILE *fopen(const char *path,const char *mode)
{
    int flags=strchr(mode,'w')?(O_WRONLY|O_CREAT|O_TRUNC):strchr(mode,'a')?(O_WRONLY|O_CREAT|O_APPEND):O_RDONLY;if(strchr(mode,'+'))flags=(flags&~O_WRONLY)|O_RDWR;
    int fd=open(path,flags,0);if(fd<0)return NULL;FILE *f=calloc(1,sizeof(*f)+UNGET+512);if(!f){close(fd);return NULL;}
    f->fd=fd;f->lock=-1;f->read=__stdio_read;f->write=__stdio_write;f->seek=__stdio_seek;f->close=__stdio_close;f->buf=(unsigned char *)(f+1)+UNGET;f->buf_size=512;f->lbf=-1;return f;
}
int fclose(FILE *f){if(!f)return EOF;int r=fflush(f);if(f->fd>=3 && close(f->fd))r=EOF;if(!(f->flags&F_PERM))free(f);return r;}
int fileno(FILE *f){return f->fd;}
void h1ns_io_release(void){for(int fd=3;fd<32;++fd)if(handles[fd])close(fd);}
void h1ns_io_init(void){}
int fstat(int fd,struct stat *s){off_t current=lseek(fd,0,SEEK_CUR),end=lseek(fd,0,SEEK_END);if(current<0 || end<0)return -1;lseek(fd,current,SEEK_SET);memset(s,0,sizeof(*s));s->st_mode=S_IFREG|0600;s->st_size=end;return 0;}
int stat(const char *p,struct stat *s){int fd=open(p,O_RDONLY);if(fd<0)return -1;int r=fstat(fd,s);close(fd);return r;}
int lstat(const char *p,struct stat *s){return stat(p,s);}
int remove(const char *p){char path[512];return path_convert(p,path)?h1_remove(path):-1;}
int unlink(const char *p){return remove(p);}
int rename(const char *a,const char *b){(void)a;(void)b;errno=ENOSYS;return -1;}
int mkdir(const char *p,mode_t m){(void)p;(void)m;errno=ENOSYS;return -1;}
DIR *opendir(const char *p){(void)p;errno=ENOSYS;return NULL;}struct dirent *readdir(DIR *d){(void)d;return NULL;}int closedir(DIR *d){(void)d;return 0;}
char *getcwd(char *s,size_t n){if(!s)return strdup("A:/");if(n<4){errno=ERANGE;return NULL;}memcpy(s,"A:/",4);return s;}
char *realpath(const char *s,char *out){if(!out)return strdup(s);strcpy(out,s);return out;}
int access(const char *p,int mode){(void)mode;struct stat s;return stat(p,&s);}
char *getenv(const char *s){(void)s;return NULL;}
int uname(struct utsname *s){memset(s,0,sizeof(*s));strcpy(s->sysname,"BBK H1");strcpy(s->release,"V1.41");strcpy(s->machine,"MIPS32");return 0;}
static const time_t epoch=1794268800LL;
time_t time(time_t *p){time_t t=epoch+h1ns_time_ms()/1000;if(p)*p=t;return t;}
int gettimeofday(struct timeval *t,void *zone){(void)zone;uint32_t ms=h1ns_time_ms();t->tv_sec=epoch+ms/1000;t->tv_usec=(ms%1000)*1000;return 0;}
int clock_gettime(clockid_t id,struct timespec *t){(void)id;struct timeval v;gettimeofday(&v,NULL);t->tv_sec=v.tv_sec;t->tv_nsec=v.tv_usec*1000;return 0;}
extern int __secs_to_tm(long long,struct tm *);extern long long __tm_to_secs(const struct tm *);
struct tm *gmtime_r(const time_t *t,struct tm *out){if(__secs_to_tm(*t,out))return NULL;out->tm_gmtoff=0;out->tm_zone="UTC";return out;}
struct tm *localtime_r(const time_t *t,struct tm *out){return gmtime_r(t,out);}
struct tm *gmtime(const time_t *t){static struct tm value;return gmtime_r(t,&value);}
struct tm *localtime(const time_t *t){return gmtime(t);}
const char *__tm_to_tzname(const struct tm *t){(void)t;return "UTC";}
time_t mktime(struct tm *t){time_t v=__tm_to_secs(t);gmtime_r(&v,t);return v;}
char *setlocale(int category,const char *name){(void)category;(void)name;return "C.UTF-8";}
struct lconv *localeconv(void){static struct lconv locale={.decimal_point=".",.thousands_sep="",.grouping="",.positive_sign="",.negative_sign="-"};return &locale;}
char *strerror(int e){(void)e;return "H1 system operation failed";}
void abort(void){h1ns_panic(1001);for(;;){}}
void exit(int code){h1ns_panic(code?code:1002);for(;;){}}
void _Exit(int code){exit(code);}
void __assert_fail(const char *condition,const char *file,int line,const char *func){char detail[640];snprintf(detail,sizeof detail,"ASSERT %.200s %.300s:%d %.80s",condition,file,line,func);h1ns_diag_event(18,detail,-1,0);fprintf(stderr,"%s\n",detail);abort();}
