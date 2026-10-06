#ifndef H1_FILE_PICKER_H
#define H1_FILE_PICKER_H
#include "h1_runtime.h"
#define H1_FILE_PICKER_PATH_BYTES 264u
/* Explicit V1.41 contract: GBK byte paths, ';' separated bare extensions,
 * at most 10 extensions of at most 10 bytes each; '*' means all files.
 * The firmware ABI has NO capacity argument. Scratch space is defensive,
 * not a claim that arbitrary firmware writes can be made memory-safe. */
static inline int h1_file_pick(const char *directory, const char *filter,
                              char *out, h1_size_t capacity)
{
    typedef void (*fn_type)(const char *,const char *,char *);
    char scratch[512]={0}; unsigned n, part=0, count=1;
    fn_type fn;
    if (!out || !capacity) return -1;
    out[0]=0;
    if (!directory || !filter || !filter[0]) return -1;
    if (!(filter[0]=='*' && !filter[1])) {
        for (n=0;filter[n];++n) {
            unsigned c=(h1_u8)filter[n];
            if (c==';') { if (!part || ++count>10) return -1;part=0; }
            else if (c<33 || c>126 || c=='.' || c=='*' || c=='/' || c=='\\' || ++part>10) return -1;
        }
        if (!part) return -1;
    }
    fn=(fn_type)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x9ECu);
    if (!fn) return -1;
    fn(directory,filter,scratch); /* vendor return value is not established */
    for (n=0;n<sizeof scratch && scratch[n];++n) {}
    if (!n) return 0;
    if (n>=H1_FILE_PICKER_PATH_BYTES || n>=capacity) return -1;
    for (part=0;part<=n;++part) out[part]=scratch[part];
    return 1;
}
/* Extract the parent without interpreting a GBK trail byte 0x5c as '\\'. */
static inline int h1_path_directory_gbk(const char *path, char *out, h1_size_t capacity)
{
    unsigned i,end=0;
    if (!path || !out || !capacity) return 0;
    for (i=0;path[i];++i) {
        unsigned c=(h1_u8)path[i], next=(h1_u8)path[i+1];
        if (c>=0x81 && c<=0xfe && next>=0x40 && next<=0xfe && next!=0x7f) { ++i;continue; }
        if (c=='\\' || c=='/') end=i+1;
    }
    if (!end || end>=capacity) { out[0]=0;return 0; }
    for (i=0;i<end;++i) out[i]=path[i];
    out[end]=0;return 1;
}
#endif
