"""Host contracts for generic drawing/AAC and real bounded image decoders."""
from pathlib import Path
import ctypes,io,os,shutil,subprocess
import pytest
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
MEDIA=ROOT/'sdk/media'
def compiler():
    cc=os.environ.get('HOST_CC') or shutil.which('gcc')
    if not cc:pytest.skip('HOST_CC/gcc required for media host checks')
    return cc
def run(args):
    p=subprocess.run([str(x) for x in args],capture_output=True)
    assert p.returncode==0,p.stderr.decode('utf-8','replace')[-3000:]
def test_media_contract(tmp_path):
    exe=tmp_path/('contract.exe' if os.name=='nt' else 'contract')
    run([compiler(),'-std=c11','-I',ROOT/'sdk/include','-I',ROOT/'sdk/esp32/common',
         ROOT/'tests/media_contract.c',MEDIA/'draw.c',MEDIA/'aac.c',
         ROOT/'sdk/esp32/common/h1net_wire.c','-o',exe]);run([exe])
def test_images(tmp_path):
    # Compile the exact pinned SDK sources against host libc. No system codec library.
    z=MEDIA/'third_party/zlib-1.3.1';png=MEDIA/'third_party/libpng-1.6.43';jpeg=MEDIA/'third_party/jpeg-9f'
    shutil.copyfile(png/'scripts/pnglibconf.h.prebuilt',tmp_path/'pnglibconf.h')
    shutil.copyfile(jpeg/'jconfig.txt',tmp_path/'jconfig.h')
    z_names='adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil'.split()
    j_names='jaricom jcomapi jdapimin jdapistd jdarith jdatasrc jdcoefct jdcolor jddctmgr jdhuff jdinput jdmainct jdmarker jdmaster jdmerge jdpostct jdsample jdtrans jerror jidctflt jidctfst jidctint jquant1 jquant2 jutils jmemmgr jmemnobs'.split()
    sources=[MEDIA/'images.c']+[z/(x+'.c') for x in z_names]+[p for p in sorted(png.glob('png*.c')) if p.name!='pngtest.c']+[jpeg/(x+'.c') for x in j_names]
    dll=tmp_path/('images.dll' if os.name=='nt' else 'images.so')
    run([compiler(),'-shared','-fPIC','-O1','-DPNG_ARM_NEON_OPT=0','-DPNG_MIPS_MSA_OPT=0','-DPNG_POWERPC_VSX_OPT=0',
         '-I',tmp_path,'-I',ROOT/'sdk/include','-I',z,'-I',png,'-I',jpeg,*sources,'-lm','-o',dll])
    lib=ctypes.CDLL(str(dll));decode=lib.h1_image_decode_rgb565
    decode.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.c_uint),ctypes.c_uint,ctypes.c_uint]
    release=lib.h1_image_free;release.argtypes=[ctypes.c_void_p];release.restype=None
    def check(raw,ok,w=32,h=16):
        out=ctypes.c_void_p();ow=ctypes.c_uint();oh=ctypes.c_uint()
        e=decode(raw,len(raw),ctypes.byref(out),ctypes.byref(ow),ctypes.byref(oh),w,h)
        try:
            assert (e==0)==ok
            if ok:assert (ow.value,oh.value)==(32,16);assert ctypes.cast(out,ctypes.POINTER(ctypes.c_uint16))[0]==0xffff
            else:assert not out.value and not ow.value and not oh.value
        finally:release(out)
    for mode,fmt in [('RGB','JPEG'),('RGBA','PNG'),('P','PNG')]:
        image=Image.new(mode,(64,32),'white' if mode!='P' else 0)
        if mode=='P':image.putpalette([255,255,255]+[0]*765)
        stream=io.BytesIO();image.save(stream,fmt);raw=stream.getvalue();check(raw,True)
        check(raw[:20],False)
    check(b'not an image',False);check(raw,False,0,16)
