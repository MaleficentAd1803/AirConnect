"""Fetch hash-pinned corresponding sources; optionally prepare a media build.
Downloaded code is never executed by this script. Output stays under build/.
"""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import sys
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/dependency-sources'
OUT.mkdir(parents=True,exist_ok=True)
def fetch(entry,folder):
    path=folder/entry['filename'];folder.mkdir(parents=True,exist_ok=True)
    if not path.exists():
        print('Fetching',path.name,flush=True)
        request=urllib.request.Request(entry['download_url'],headers={'User-Agent':'AirConnect-source-collector'})
        with urllib.request.urlopen(request,timeout=90) as response,path.with_suffix('.part').open('wb') as output:
            total=0
            while data:=response.read(1024*1024):
                total+=len(data)
                if total>150*1024*1024:raise RuntimeError('Source download exceeds limit')
                output.write(data)
        path.with_suffix('.part').replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:
        raise RuntimeError('Source checksum mismatch: '+path.name)
    return path

lock=json.loads((ROOT/'dependencies/source-lock.json').read_text())
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(lambda entry:fetch(entry,OUT),lock))
print('Verified',len(lock),'pinned source archives',flush=True)
if '--prepare-media' in sys.argv:
    directories={'FFmpeg':'ffmpeg','x264':'x264','nv-codec-headers':'nv-codec-headers'}
    for entry in lock:
        if entry['Name'] not in directories:continue
        target=ROOT/'build/media-src'/directories[entry['Name']]
        if target.exists():continue
        target.mkdir(parents=True)
        with tarfile.open(OUT/entry['filename']) as archive:
            members=archive.getmembers();prefix=members[0].name.split('/')[0]+'/'
            for member in members:
                if not member.name.startswith(prefix):raise RuntimeError('Archive has an unexpected source root')
                member.name=member.name[len(prefix):]
            archive.extractall(target,members=[m for m in members if m.name],filter='data')
    for entry in json.loads((ROOT/'dependencies/build-tools-lock.json').read_text()):
        fetch(entry,ROOT/'build/media-tools')
