param([string]$Toolchain='',[switch]$Publish)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$gitRoot=$root.Replace('\','/')
Push-Location $root
try {
 & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Toolchain $Toolchain
 & (Join-Path $PSScriptRoot 'package.ps1') -SkipBuild -Toolchain $Toolchain
 & (Join-Path $PSScriptRoot 'source-package.ps1')
 python tools/release-artifacts.py
 if($LASTEXITCODE) { throw 'Release asset creation failed' }
 if($Publish) {
  $version=((Get-Content src/version.h | Where-Object { $_ -match '^#define AC_VERSION ' }) -split '"')[1]
  $changes=git -c "safe.directory=$gitRoot" status --porcelain
  if($changes) { throw 'Commit the reviewed source and version before publishing' }
  git -c "safe.directory=$gitRoot" tag "v$version"
  if($LASTEXITCODE) { throw 'Release tag creation failed' }
  git -c "safe.directory=$gitRoot" -c credential.helper= -c 'credential.helper=!gh auth git-credential' push origin HEAD:main "v$version"
  if($LASTEXITCODE) { throw 'Git push failed' }
  $assets=@(Get-ChildItem -LiteralPath dist -File | Where-Object { $_.Name -like "AirConnect-$version-*.zip" -or $_.Name -eq "AirConnect-$version-Setup.exe" -or $_.Name -eq 'SHA256SUMS.txt' } | Select-Object -ExpandProperty FullName)
  gh release create "v$version" @assets --repo MaleficentAd1803/AirConnect --title "AirConnect $version" --notes-file docs/RELEASE.md --verify-tag
  if($LASTEXITCODE) { throw 'GitHub release creation failed' }
 }
} finally { Pop-Location }
