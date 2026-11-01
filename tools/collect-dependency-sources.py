"""Collect pinned corresponding sources and OBS build recipes for release assets.
No downloaded code is executed. All output stays under build/dependency-sources.
"""
from pathlib import Path
import concurrent.futures as futures
import hashlib
import json
import re
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/dependency-sources'
OUT.mkdir(parents=True,exist_ok=True)

def fetch(url,path):
    if path.exists() and path.stat().st_size>0:return path
    print('Fetching',path.name,flush=True)
    request=urllib.request.Request(url,headers={'User-Agent':'AirConnect-source-collector'})
    with urllib.request.urlopen(request,timeout=90) as response,path.with_suffix(path.suffix+'.part').open('wb') as output:
        total=0
        while data:=response.read(1024*1024):
            total+=len(data)
            if total>350*1024*1024:raise RuntimeError('Source download exceeds limit')
            output.write(data)
    path.with_suffix(path.suffix+'.part').replace(path)
    return path

recipes=fetch('https://api.github.com/repos/obsproject/obs-deps/zipball/2025-08-23',OUT/'obs-deps-2025-08-23.zip')
jobs=[]
with zipfile.ZipFile(recipes) as archive:
    for name in archive.namelist():
        if '/deps.ffmpeg/' not in name or not name.endswith('.ps1'):continue
        text=archive.read(name).decode('utf8')
        params={key:re.search(r'\$'+key+r'\s*=\s*[\'\"]([^\'\"]+)',text).group(1) for key in ('Name','Version','Uri','Hash')}
        component=params['Name']
        if component in ('gas-preprocessor','libdatachannel'):continue # Not linked on x64.
        uri,pin=params['Uri'],params['Hash']
        if uri.endswith('.git'):
            if uri.startswith('https://github.com/'):
                uri='https://api.github.com/repos/'+uri.removeprefix('https://github.com/').removesuffix('.git')+'/tarball/'+pin
            elif 'gitlab.xiph.org' in uri or 'gitlab.com' in uri or 'code.videolan.org' in uri:
                base=uri.removesuffix('.git');repo=base.rsplit('/',1)[1];uri=base+'/-/archive/'+pin+'/'+repo+'-'+pin+'.tar.gz'
            elif 'googlesource.com' in uri:uri=uri.removesuffix('.git')+'/+archive/'+pin+'.tar.gz'
            else:raise RuntimeError(uri)
            extension='.tar.gz'
        else:extension='.zip' if uri.endswith('.zip') else '.tar.gz'
        params['download_url']=uri
        params['filename']=component+'-'+params['Version']+extension
        if '${PSScriptRoot}' in pin:
            checksum=name.rsplit('/',1)[0]+pin.replace('${PSScriptRoot}','')
            params['expected_sha256']=re.search(r'[0-9a-fA-F]{64}',archive.read(checksum).decode()).group(0).lower()
        jobs.append(params)

jobs += [dict(Name='openssl',Version='3.5.5',Uri='https://github.com/openssl/openssl',Hash='openssl-3.5.5',download_url='https://github.com/openssl/openssl/releases/download/openssl-3.5.5/openssl-3.5.5.tar.gz',filename='openssl-3.5.5.tar.gz',expected_sha256='b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89')]

def collect(job):
    path=fetch(job['download_url'],OUT/job['filename'])
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    if job.get('expected_sha256',digest)!=digest:raise RuntimeError('Source checksum mismatch: '+path.name)
    job['sha256']=digest
    return job

results=[];errors=[]
with futures.ThreadPoolExecutor(max_workers=5) as pool:
    for task in futures.as_completed([pool.submit(collect,j) for j in jobs]):
        try:results.append(task.result())
        except Exception as error:errors.append(str(error));print('ERROR',error,flush=True)
(OUT/'sources.json').write_text(json.dumps(sorted(results,key=lambda r:r['Name']),indent=2),encoding='utf8')
if errors:raise SystemExit('\n'.join(errors))
print('Collected',len(results),'pinned source archives',flush=True)
