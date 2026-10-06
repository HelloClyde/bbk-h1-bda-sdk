"""Install the SHA-256 pinned Windows MIPS GNU 15.2.0 toolchain."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
URL='https://static.grumpycoder.net/pixel/mips/g++-mipsel-none-elf-15.2.0.zip'
SHA256='8ba866e25c9826ee04ab4310365d264e3e73769e3738bb58ae38fd6740b7ee8d'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dest',type=Path,default=ROOT/'.tools/toolchain')
    args=parser.parse_args();dest=args.dest.resolve();dest.mkdir(parents=True,exist_ok=True)
    archive=dest/'g++-mipsel-none-elf-15.2.0.zip'
    if not archive.is_file():
        request=urllib.request.Request(URL,headers={'User-Agent':'BBK-H1-SDK/0.1'})
        partial=archive.with_suffix('.part')
        with urllib.request.urlopen(request,timeout=120) as response,partial.open('wb') as output:
            shutil.copyfileobj(response,output)
        partial.replace(archive)
    digest=hashlib.sha256()
    with archive.open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''):digest.update(block)
    if digest.hexdigest()!=SHA256:
        raise RuntimeError('Toolchain digest mismatch; archive was NOT extracted')
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            target=(dest/member.filename).resolve()
            if target!=dest and dest not in target.parents:
                raise RuntimeError('Toolchain archive path escapes destination')
        source.extractall(dest)
    for name in ['gcc','g++','ld','objcopy','nm','readelf']:
        if not (dest/f'bin/mipsel-none-elf-{name}.exe').is_file():
            raise RuntimeError(f'Missing {name}')
    version=subprocess.check_output([str(dest/'bin/mipsel-none-elf-gcc.exe'),'--version'],text=True).splitlines()[0]
    if not version.endswith('15.2.0'):raise RuntimeError(version)
    print(version);print('H1_GNU_BIN='+str(dest/'bin'))

if __name__=='__main__':main()
