param([string]$Toolchain='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$Toolchain=Find-AirConnectToolchain $Toolchain
$root=Split-Path $PSScriptRoot -Parent
$runtime=Join-Path $root 'dependencies\runtime'
$lib=Join-Path $root 'dependencies\lib'
New-Item -ItemType Directory -Force $lib | Out-Null
foreach($name in @('libcrypto-3-x64','avcodec-61','avformat-61','avutil-59','swscale-8')) {
 $dll=Join-Path $runtime ($name+'.dll')
 $listing=& (Join-Path $Toolchain 'bin\objdump.exe') -p $dll
 if($LASTEXITCODE) { throw "Cannot read exports: $dll" }
 $names=@();$exporting=$false
 foreach($line in $listing) {
  if($line -match '^\[Ordinal/Name Pointer\] Table') { $exporting=$true;continue }
  if($exporting -and $line -match '^\s+\[\s*\d+\]\s+(\S+)') { $names+=$Matches[1] }
 }
 if($names.Count -lt 10) { throw "No exports in $name" }
 $def=Join-Path $lib ($name+'.def')
 @('LIBRARY '+$name+'.dll','EXPORTS')+$names | Set-Content -LiteralPath $def -Encoding ascii
 & (Join-Path $Toolchain 'bin\dlltool.exe') -m i386:x86-64 -D ($name+'.dll') -d $def -l (Join-Path $lib ($name+'.a'))
 if($LASTEXITCODE) { throw "Cannot create import library for $name" }
 Write-Output "$name`: $($names.Count) exports"
}
$def=Join-Path $lib 'dnsservice.def'
@('LIBRARY DNSAPI.dll','EXPORTS','DnsServiceRegister','DnsServiceDeRegister','DnsServiceRegisterCancel','DnsServiceFreeInstance','DnsServiceResolve','DnsServiceResolveCancel') | Set-Content -LiteralPath $def -Encoding ascii
& (Join-Path $Toolchain 'bin\dlltool.exe') -m i386:x86-64 -D DNSAPI.dll -d $def -l (Join-Path $lib 'dnsservice.a')
if($LASTEXITCODE) { throw 'Cannot create DNS Service import library' }
