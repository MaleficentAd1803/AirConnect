"""Prepare the exact native DLLs used by the published release, without installing software."""
from pathlib import Path
import hashlib
import json
import os
import urllib.request
import zipfile

root=Path(__file__).resolve().parents[1]
lock=json.loads((root/'dependencies/runtime-bundle.json').read_text())
archive=root/'build/vendor'/lock['filename']
archive.parent.mkdir(parents=True,exist_ok=True)
if not archive.exists():
    request=urllib.request.Request(lock['url'],headers={'User-Agent':'AirConnect-build'})
    with urllib.request.urlopen(request,timeout=90) as response,archive.with_suffix('.part').open('wb') as out:
        total=0
        while data:=response.read(1024*1024):
            total+=len(data)
            if total>64*1024*1024:raise RuntimeError('Runtime archive exceeds its size limit')
            out.write(data)
    archive.with_suffix('.part').replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=lock['sha256']:
    raise RuntimeError('Runtime archive checksum mismatch')
manifest=json.loads((root/'dependencies/runtime-manifest.json').read_text())
destination=root/'dependencies/runtime';destination.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(archive) as zipped:
    for entry in manifest['files']:
        name=entry['name']
        if Path(name).name!=name or not name.endswith('.dll'):raise RuntimeError('Invalid runtime filename')
        info=zipped.getinfo(name)
        if info.file_size!=entry['bytes'] or info.file_size>32*1024*1024:raise RuntimeError('Invalid DLL size')
        data=zipped.read(name)
        if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise RuntimeError('DLL checksum mismatch: '+name)
        temporary=destination/(name+'.tmp');temporary.write_bytes(data);os.replace(temporary,destination/name)
print('Prepared',len(manifest['files']),'verified native runtime DLLs')
