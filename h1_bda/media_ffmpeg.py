"""Pinned, freestanding FFmpeg 0.6.7 codec build for the H1 frontend."""
from pathlib import Path
import re,shutil

LIBRARIES=('avutil','avcodec','avformat')
ARCHIVES=tuple('ff'+name for name in LIBRARIES)

def build(root,compile_group):
    source=root/'third_party/ffmpeg-0.6.7'
    shutil.copyfile(root/'config/ffmpeg.h',source/'config.h')
    # Upstream ignores this configure output; recreate it in a clean checkout.
    (source/'libavutil/avconfig.h').write_text('#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n#define AV_HAVE_BIGENDIAN 0\n#endif\n',encoding='ascii',newline='\n')
    config={k:('yes' if v=='1' else 'no') for k,v in re.findall(r'^#define (\w+) (\d+)$',(source/'config.h').read_text(),re.M)}
    for library in LIBRARIES:
        folder=source/('lib'+library)
        variables=dict(config)
        def expand(value):
            for _ in range(4):value=re.sub(r'\$\(([^()]*)\)',lambda m:variables.get(m[1],''),value)
            return value
        for line in (folder/'Makefile').read_text().replace('\\\n',' ').splitlines():
            match=re.match(r'^([\w$()\-]+)\s*(\+?=)\s*(.*)$',line)
            if match:
                key,operation,value=match.groups();key=expand(key);value=expand(value.split('#')[0])
                variables[key]=(variables.get(key,'')+' '+value) if operation=='+=' else value
        names=(variables.get('OBJS','')+' '+variables.get('OBJS-yes','')).split()
        sources=sorted({folder/(name[:-2]+'.c') for name in names if name.endswith('.o')})
        compile_group('ff'+library,sources,['-I'+str(source),'-DHAVE_AV_CONFIG_H=1','-DO_BINARY=0','-std=gnu99','-Wno-error=implicit-function-declaration','-Wno-error=incompatible-pointer-types'])
