"""Assemble corresponding dependency sources and SHA-256 sums for this version."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import zipfile
root=Path(__file__).resolve().parents[1]
version=re.search(r'^#define AC_VERSION "([^"]+)"',(root/'src/version.h').read_text(),re.M)[1]
dist=root/'dist';dist.mkdir(exist_ok=True)
subprocess.run([sys.executable,str(root/'tools/collect-dependency-sources.py')],check=True)
lock=json.loads((root/'dependencies/source-lock.json').read_text())
dependency_source=dist/f'AirConnect-{version}-dependency-sources.zip'
with zipfile.ZipFile(dependency_source,'w',compression=zipfile.ZIP_STORED) as out:
    for item in lock:out.write(root/'build/dependency-sources'/item['filename'],'sources/'+item['filename'])
    for name in ['dependencies/source-lock.json','dependencies/build-tools-lock.json','dependencies/runtime-manifest.json','tools/build-media-deps.sh','tools/prepare-media-build.ps1','tools/collect-dependency-sources.py','docs/BUILD.md','THIRD_PARTY_NOTICES.md','LICENSE']:
        out.write(root/name,name)
    for name in ['config.h','config_components.h','ffbuild/config.mak']:
        path=root/'build/media-src/ffmpeg'/name
        if path.exists():out.write(path,'build-configuration/ffmpeg/'+name)
    for name in ['config.h','config.mak','x264_config.h']:
        path=root/'build/media-src/x264'/name
        if path.exists():out.write(path,'build-configuration/x264/'+name)
files=[dist/f'AirConnect-{version}-Setup.exe',dist/f'AirConnect-{version}-portable.zip',dist/f'AirConnect-{version}-source.zip',dependency_source]
runtime=dist/f'AirConnect-{version}-runtime.zip'
if runtime.exists():files.append(runtime)
(dist/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n' for path in files),encoding='ascii')
for path in files:print(path.name,path.stat().st_size)
