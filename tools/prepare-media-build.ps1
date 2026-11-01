param([string]$Toolchain='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$Toolchain=Find-AirConnectToolchain $Toolchain
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 python tools/collect-dependency-sources.py --prepare-media
 if($LASTEXITCODE) { throw 'Source verification failed' }
 tar -xf build/media-tools/make.pkg.tar.zst -C build/media-tools usr/bin/make.exe
 if($LASTEXITCODE) { throw 'GNU make extraction failed' }
 $env:AIRCONNECT_GCC_DIR=Join-Path $Toolchain 'bin'
 $env:AIRCONNECT_NASM_DIR=Split-Path (Get-Command nasm.exe -ErrorAction Stop).Source
 $git=Get-Command git.exe -ErrorAction Stop
 $bash=Join-Path (Split-Path (Split-Path $git.Source -Parent) -Parent) 'bin\bash.exe'
 if(-not (Test-Path -LiteralPath $bash)) { $bash=Join-Path $env:ProgramFiles 'Git\bin\bash.exe' }
 & $bash tools/build-media-deps.sh
 if($LASTEXITCODE) { throw 'Media dependency compilation failed' }
} finally { Pop-Location }
