"""Exercise real installer extraction, locked-file rollback and an old-version upgrade.
All writes stay inside this repository's build/upgrade-test fixture. No registration.
"""
from pathlib import Path
import ctypes
from ctypes import wintypes
import hashlib
import subprocess
import time

root=Path(__file__).resolve().parents[1]
target=root/'build/upgrade-test'
old=root/'dist/AirConnect-0.1.0-Setup.exe'
new=root/'dist/AirConnect-0.2.0-Setup.exe'
if target.exists():
    backup=target.with_name('upgrade-test-previous-'+str(time.time_ns()))
    assert target.resolve().is_relative_to(root/'build') and backup.parent.resolve()==(root/'build').resolve()
    target.rename(backup)
target.mkdir(parents=True)
subprocess.run([str(old),'--extract-only',str(target)],check=True)
sentinel=target/'keep-personal.txt';sentinel.write_text('User-owned file must survive upgrades.\n')
def digest(name):return hashlib.sha256((target/name).read_bytes()).hexdigest()
before={name:digest(name) for name in ['AirConnect.exe','avcodec-61.dll','avformat-61.dll']}
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
kernel.CreateFileW.restype=wintypes.HANDLE
kernel.CloseHandle.argtypes=[wintypes.HANDLE]
locked=kernel.CreateFileW(str(target/'avformat-61.dll'),0x80000000,1,None,3,0,None)
assert locked not in (None,ctypes.c_void_p(-1).value)
try:
    failed=subprocess.run([str(new),'--upgrade-test',str(target)])
    assert failed.returncode==1,'locked-file update should fail'
finally:kernel.CloseHandle(locked)
assert {name:digest(name) for name in before}==before,'previous files must be restored'
assert sentinel.read_text()=='User-owned file must survive upgrades.\n'
assert not list(target.glob('.update-*')),'failed transaction leaves no staging directory'
subprocess.run([str(new),'--upgrade-test',str(target)],check=True)
assert digest('AirConnect.exe')==hashlib.sha256((root/'build/release/AirConnect.exe').read_bytes()).hexdigest()
assert not (target/'libx264-164.dll').exists(),'obsolete owned DLL removed'
assert sentinel.read_text()=='User-owned file must survive upgrades.\n'
assert not list(target.glob('.update-*'))
print('Native installer: extraction, locked-file rollback, 0.1.0 upgrade, stale-file removal and personal-file preservation passed')
