"""Record actual local binary provenance. This performs no network requests."""
from pathlib import Path
import ctypes
import hashlib
import json
import os

root=Path(__file__).resolve().parents[1]
runtime=root/"dependencies"/"runtime"
handle=os.add_dll_directory(str(runtime))
def call(name,function):
    dll=ctypes.CDLL(str(runtime/name))
    fn=getattr(dll,function)
    fn.restype=ctypes.c_char_p
    return fn().decode("utf-8",errors="replace")
crypto=ctypes.CDLL(str(runtime/"libcrypto-3-x64.dll"))
crypto.OpenSSL_version.argtypes=[ctypes.c_int]
crypto.OpenSSL_version.restype=ctypes.c_char_p
record={
 "airplay_windows_commit":"f739810e35887cf572b69275a52dc6f32323d65d",
 "uxplay_reference_commit":"f40b121b6b932fb8ed138ce2e492b0a77cf284a3",
 "libplist_source":"2.4.0",
 "ffmpeg_version":call("avutil-59.dll","av_version_info"),
 "ffmpeg_configuration":call("avcodec-61.dll","avcodec_configuration"),
 "ffmpeg_license":call("avcodec-61.dll","avcodec_license"),
 "openssl_version":crypto.OpenSSL_version(0).decode(),
 "origin":"Existing local OBS and Git runtimes; Microsoft app-local runtimes from System32",
 "files":[{"name":p.name,"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(runtime.glob("*.dll"))]
}
(root/"dependencies"/"runtime-manifest.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
print(json.dumps({k:v for k,v in record.items() if k!="files"},indent=2))
