"""Build the optional SDK media link probe. No hardware access is performed."""
from pathlib import Path
import os,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from h1_bda import media
from h1_bda.header import HeaderFields,encode_header
from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES,build_diagnostic_resources
from h1_bda.validate import validate_bda
def main():
    libs=media.libraries();out=ROOT/'build/media-probe';out.mkdir(parents=True,exist_ok=True)
    archive=media.compile_group('probe',[ROOT/'tests/media_smoke.c',*media.sources()],
        ['-I'+str(media.ROOT/'target'),'-Wno-unused-parameter'])
    elf=out/'probe.elf';raw=out/'probe.bin';rsp=out/'link.rsp'
    args=[*media.FLAGS,'-nostdlib','-Wl,--gc-sections','-Wl,--build-id=none','-Wl,-T,'+str(media.ROOT/'runtime/h1.ld'),
          '-o',elf,'-Wl,--start-group',archive,*libs,'-lgcc','-Wl,--end-group']
    rsp.write_text('\n'.join('"'+str(x).replace('\\','/')+'"' for x in args))
    media.run([media.CC,'@'+str(rsp)])
    media.run([media.TC/('mipsel-none-elf-objcopy.exe' if os.name=='nt' else 'mipsel-none-elf-objcopy'),'-O','binary',elf,raw])
    payload=raw.read_bytes();size=(PAYLOAD_OFFSET+len(payload)+3)&~3
    fields=HeaderFields(category=0x47,file_size_minus_4=size-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
    data=encode_header(fields,title='MediaProbe',build_time='2026-11-14 00:00:00')+build_diagnostic_resources()+payload
    bda=out/'H1MediaProbe.bda';bda.write_bytes(data+bytes(size-len(data)))
    assert validate_bda(bda)['ok'];print('PASS optional media GNU/MIPS link and BDA structure (not hardware execution)')
if __name__=='__main__':main()
