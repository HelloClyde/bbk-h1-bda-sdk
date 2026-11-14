"""Optional H1 media library builder (GNU MIPS, explicit output/toolchain)."""
from pathlib import Path
from .media_ffmpeg import build as build_ffmpeg,ARCHIVES as FFLIBS
import concurrent.futures,hashlib,json,os,re,shutil,subprocess
SDK=Path(__file__).resolve().parents[1]
ROOT=SDK/'sdk/media'
MUSL=ROOT/'third_party/musl-1.2.5'
BUILD=Path(os.environ.get('H1_MEDIA_BUILD',SDK/'build/media')).resolve();GEN=BUILD/'generated';GEN.mkdir(parents=True,exist_ok=True)
TC=Path(os.environ.get('H1_GNU_BIN',SDK/'.tools/toolchain/bin')).resolve()
CC=TC/('mipsel-none-elf-gcc.exe' if os.name=='nt' else 'mipsel-none-elf-gcc')
ENV=dict(os.environ);ENV.setdefault('SOURCE_DATE_EPOCH','1794614400')
def run(args,cwd=ROOT):
    r=subprocess.run([str(x) for x in args],cwd=cwd,env=ENV,capture_output=True,text=True,errors='replace',encoding='utf-8')
    if r.returncode:raise RuntimeError(' '.join(str(x) for x in args[:4])+'\n'+r.stdout+r.stderr)
    return r.stdout
def headers():
    inc=GEN/'include';(inc/'bits').mkdir(parents=True,exist_ok=True)
    source=(MUSL/'arch/mips/bits/alltypes.h.in').read_text()+'\n'+(MUSL/'include/alltypes.h.in').read_text()
    def convert(m):
        kind,body=m.groups()
        if kind=='TYPEDEF':
            typ,name=body.rsplit(' ',1);name=name.strip();decl='typedef '+typ+' '+name+';';key=name
        else:
            name,rest=body.split(' ',1);decl=kind.lower()+' '+name+' '+rest+';';key=kind.lower()+'_'+name
        return '#if defined(__NEED_'+key+') && !defined(__DEFINED_'+key+')\n'+decl+'\n#define __DEFINED_'+key+'\n#endif'
    source=re.sub(r'^(TYPEDEF|STRUCT|UNION) (.*);$',convert,source,flags=re.M)
    (inc/'bits/alltypes.h').write_text(source)

FLAGS=['-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-Os','-std=c11','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-D_GNU_SOURCE','-D_POSIX_C_SOURCE=200809L','-DH1_BDA=1','-DSTMTEXPR=1','-D_ALIGNED=__attribute__((aligned))']
def compile_group(name,sources,extra=(),source_options=None):
    out=BUILD/name;out.mkdir(exist_ok=True)
    includes=[GEN/'include',MUSL/'arch/mips',MUSL/'arch/generic',MUSL/'include']
    if name=='libc':includes.insert(0,MUSL/'src/include')
    includes += [ROOT/'third_party/zlib-1.3.1',ROOT/'third_party/libpng-1.6.43',ROOT/'third_party/jpeg-9f',MUSL/'src/internal',SDK/'sdk/include',SDK/'sdk/esp32/h1',SDK/'sdk/esp32/common',ROOT/'target']
    flags=FLAGS+['-I'+str(p) for p in includes]+[str(p) for p in extra]
    hash_cache={}
    def digest(p):
        key=str(p)
        if key not in hash_cache:hash_cache[key]=hashlib.sha256(p.read_bytes()).hexdigest()
        return hash_cache[key]
    def build(p):
        source_flags=flags+(['-O3'] if p.name in {'h264dsp.c','h264idct.c','h264pred.c','video_pixels.c'} else [])
        if source_options:source_flags+=source_options.get(p.name,[])
        obj=out/(hashlib.sha256(Path(os.path.relpath(p,ROOT)).as_posix().encode()).hexdigest()[:12]+'.o');stamp=obj.with_suffix('.json')
        previous=None
        if stamp.exists():
            try:previous=json.loads(stamp.read_text())
            except ValueError:pass
        matches=isinstance(previous,dict) and previous['flags']==source_flags and all(Path(k).is_file() and digest(Path(k))==v for k,v in previous['inputs'].items())
        if not obj.exists() or not matches:
            dep=obj.with_suffix('.d');run([CC,*source_flags,'-ffile-prefix-map='+str(ROOT)+'=.','-MMD','-MF',dep,'-MT','h1deps','-c',p,'-o',obj])
            data=dep.read_text().replace('\\\n',' ').split(':',1)[1]
            names=re.findall(r'(?:\\[ \t#\\]|[^ \t\r\n])+',data)
            paths=[Path(re.sub(r'\\([ \t#\\])',r'\1',name)) for name in names]
            stamp.write_text(json.dumps(dict(flags=source_flags,inputs={str(path):digest(path) for path in paths}),sort_keys=True))
        return obj
    objects=[];errors=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        jobs={pool.submit(build,p):p for p in sources}
        for job in concurrent.futures.as_completed(jobs):
            try:objects.append((Path(os.path.relpath(jobs[job],ROOT)).as_posix(),job.result()))
            except Exception as e:errors.append(str(e))
    if errors:
        (out/'errors.log').write_text('\n\n'.join(errors),encoding='utf-8')
        raise RuntimeError(name+': '+str(len(errors))+' failed sources; '+str(out/'errors.log')+'\n'+errors[0][-1800:])
    archive=BUILD/(name+'.a');temporary=BUILD/(name+'.tmp.a');response=out/'archive.rsp'
    response.write_text('\n'.join('"'+p.as_posix()+'"' for _,p in sorted(objects)))
    run([TC/('mipsel-none-elf-ar.exe' if os.name=='nt' else 'mipsel-none-elf-ar'),'rcsD',temporary,'@'+str(response)]);temporary.replace(archive)
    print('PASS MIPS',name,len(objects),'sources',archive.stat().st_size,'archive bytes',flush=True)
    return archive

def images():
    z=ROOT/'third_party/zlib-1.3.1'
    compile_group('zlib',[z/(p+'.c') for p in ('adler32','compress','crc32','deflate','gzclose','gzlib','gzread','gzwrite','infback','inffast','inflate','inftrees','trees','uncompr','zutil')],['-DHAVE_UNISTD_H=1'])
    png=ROOT/'third_party/libpng-1.6.43'
    shutil.copyfile(png/'scripts/pnglibconf.h.prebuilt',GEN/'include/pnglibconf.h')
    compile_group('png',sorted(png.glob('png*.c')),['-DPNG_ARM_NEON_OPT=0','-DPNG_MIPS_MSA_OPT=0','-DPNG_POWERPC_VSX_OPT=0'])
    jpeg=ROOT/'third_party/jpeg-9f'
    shutil.copyfile(jpeg/'jconfig.txt',GEN/'include/jconfig.h')
    sources=[jpeg/(p+'.c') for p in ('jaricom','jcapimin','jcapistd','jcarith','jccoefct','jccolor','jcdctmgr','jchuff','jcinit','jcmainct','jcmarker','jcmaster','jcomapi','jcparam','jcprepct','jcsample','jctrans','jdapimin','jdapistd','jdarith','jdatadst','jdatasrc','jdcoefct','jdcolor','jddctmgr','jdhuff','jdinput','jdmainct','jdmarker','jdmaster','jdmerge','jdpostct','jdsample','jdtrans','jerror','jfdctflt','jfdctfst','jfdctint','jidctflt','jidctfst','jidctint','jquant1','jquant2','jutils','jmemmgr','jmemnobs')]
    compile_group('jpeg',sources)


def libc():
    syscalls=(MUSL/'arch/mips/bits/syscall.h.in').read_text()
    syscalls+=re.sub(r'__NR_','SYS_',syscalls)
    (GEN/'include/bits/syscall.h').write_text(syscalls)
    sources=[]
    for folder in ('math','ctype','stdlib','string','regex','multibyte'):
        sources.extend((MUSL/'src'/folder).glob('*.c'))
    sources=[p for p in sources if p.name not in ('strerror.c','strerror_r.c','strsignal.c')]
    for name in ('floatscan','intscan','shgetc'):
        sources.append(MUSL/'src/internal'/(name+'.c'))
    for name in ('__towrite','__toread','__overflow','__uflow','__fwritex','clearerr','feof','ferror','fgetc','fgets','fputc','fputs','fread','fwrite','fprintf','printf','snprintf','sprintf','sscanf','vfprintf','vsnprintf','vsprintf','vsscanf','vfscanf','ungetc','puts','rewind','fseek','ftell','fseeko','ftello','fflush','setvbuf','fgetpos','fsetpos','getdelim','getline'):
        p=MUSL/'src/stdio'/(name+'.c')
        if p.exists():sources.append(p)
    sources+=list((MUSL/'src/setjmp/mips').glob('*.S'))
    for name in ('__secs_to_tm','__tm_to_secs','__year_to_secs','__month_to_secs','asctime_r','asctime','difftime','ctime','strftime'):
        sources.append(MUSL/'src/time'/(name+'.c'))
    for name in ('c_locale','langinfo','__lctrans','__mo_lookup','iconv','iconv_close'):
        sources.append(MUSL/'src/locale'/(name+'.c'))
    sources.append(MUSL/'src/prng/rand.c')
    sources.extend(MUSL/'src/network'/(name+'.c') for name in ('inet_aton','inet_pton'))
    compile_group('libc',sources,['-D__LONG_DOUBLE_64__','-DH1_GLOBAL_LOCALE=1'])


# Select these explicit sources only when using the optional freestanding runtime.
def sources():
    return [ROOT/p for p in ('images.c','aac.c','draw.c','font16.c','input.c',
        'target/libc.c','target/heap.c','third_party/tlsf/tlsf.c','target/audio.c',
        'target/transport.c','runtime/startup.c','runtime/entry.S')] + [SDK/p for p in
        ('sdk/esp32/h1/firmware.c','sdk/esp32/h1/usb.c',
         'sdk/esp32/common/h1net_wire.c','sdk/esp32/common/h1net_client.c')]

def libraries():
    headers();libc();images();build_ffmpeg(ROOT,compile_group)
    return [BUILD/(name+'.a') for name in ('libc','jpeg','png','zlib',*FFLIBS)]
