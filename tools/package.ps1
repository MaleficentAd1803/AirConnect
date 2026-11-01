param([string]$Toolchain='',[switch]$SkipBuild)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$Toolchain=Find-AirConnectToolchain $Toolchain
$root=Split-Path $PSScriptRoot -Parent
$env:PATH=(Join-Path $Toolchain 'bin')+';'+$env:PATH
if(-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Toolchain $Toolchain }
$version=((Get-Content (Join-Path $root 'src\version.h') | Where-Object { $_ -match '^#define AC_VERSION ' }) -split '"')[1]
$stage=Join-Path $root ('dist\AirConnect-'+$version+'-portable')
$work=Join-Path $root 'build\package'
New-Item -ItemType Directory -Force $stage,$work | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'build\release\AirConnect.exe') -Destination $stage
Get-ChildItem -LiteralPath (Join-Path $root 'dependencies\runtime') -Filter '*.dll' | Copy-Item -Destination $stage
foreach($file in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','docs\VALIDATION.md','docs\BUILD.md','docs\LIMITATIONS.md','docs\FIREWALL-REVIEW.md','docs\ARCHITECTURE.md')) { Copy-Item -LiteralPath (Join-Path $root $file) -Destination $stage }
$portableReadme=Join-Path $stage 'README.md'
(Get-Content -LiteralPath $portableReadme -Raw -Encoding UTF8).Replace('(docs/','(') | Set-Content -LiteralPath $portableReadme -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $root 'dependencies\runtime-manifest.json') -Destination $stage
$licenses=Join-Path $root 'licenses'
if(Test-Path -LiteralPath $licenses) { Get-ChildItem -LiteralPath $licenses -File | Copy-Item -Destination $stage }
$appManifest=(Join-Path $root 'src\app.manifest').Replace('\','/')
$icon=(Join-Path $root 'src\airconnect.ico').Replace('\','/')
$versionResource=(Join-Path $root 'src\installer-version.rc').Replace('\','/')
$resource=Join-Path $work 'uninstall.rc'
@("1 24 `"$appManifest`"","101 ICON `"$icon`"","#include `"$versionResource`"") | Set-Content -LiteralPath $resource -Encoding ascii
windres $resource -O coff -o (Join-Path $work 'uninstall-res.o')
if($LASTEXITCODE) { throw 'Uninstaller resources failed' }
$source=Join-Path $root 'src\installer.cpp'
$uninstaller=Join-Path $stage 'Uninstall.exe'
g++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DNOMINMAX -DAC_UNINSTALLER -municode -mwindows -static -static-libgcc -static-libstdc++ $source (Join-Path $work 'uninstall-res.o') -o $uninstaller -lcabinet -lole32 -lshell32 -luuid -lcomctl32 -ladvapi32
if($LASTEXITCODE) { throw 'Uninstaller compilation failed' }
$cab=Join-Path $work 'AirConnect.cab'
$ddf=Join-Path $work 'payload.ddf'
$lines=@('.OPTION EXPLICIT','.Set CabinetNameTemplate=AirConnect.cab',".Set DiskDirectory1=`"$work`"",'.Set Cabinet=on','.Set Compress=on','.Set CompressionType=LZX','.Set CompressionMemory=21','.Set MaxDiskSize=0')
foreach($file in Get-ChildItem -LiteralPath $stage -File) { $lines+='"'+$file.FullName+'" "'+$file.Name+'"' }
$lines | Set-Content -LiteralPath $ddf -Encoding ascii
Push-Location $work
try { makecab /F $ddf | Out-File -LiteralPath (Join-Path $work 'makecab.log') }
finally { Pop-Location }
if($LASTEXITCODE) { throw 'Cabinet creation failed' }
$cabResource=$cab.Replace('\','/')
$resource=Join-Path $work 'setup.rc'
@("1 24 `"$appManifest`"","101 ICON `"$icon`"","#include `"$versionResource`"","201 RCDATA `"$cabResource`"") | Set-Content -LiteralPath $resource -Encoding ascii
windres $resource -O coff -o (Join-Path $work 'setup-res.o')
if($LASTEXITCODE) { throw 'Installer resources failed' }
$installer=Join-Path $root ('dist\AirConnect-'+$version+'-Setup.exe')
g++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DNOMINMAX -municode -mwindows -static -static-libgcc -static-libstdc++ $source (Join-Path $work 'setup-res.o') -o $installer -lcabinet -lole32 -lshell32 -luuid -lcomctl32 -ladvapi32
if($LASTEXITCODE) { throw 'Installer compilation failed' }
$zip=Join-Path $root ('dist\AirConnect-'+$version+'-portable.zip')
Remove-Item -LiteralPath $uninstaller
Compress-Archive -LiteralPath $stage -DestinationPath $zip -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $installer,$zip | Format-Table Path,Hash -AutoSize
