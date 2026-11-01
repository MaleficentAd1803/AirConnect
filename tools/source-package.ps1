$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$gitRoot=$root.Replace('\','/')
$version=((Get-Content (Join-Path $root 'src\version.h') | Where-Object { $_ -match '^#define AC_VERSION ' }) -split '"')[1]
$output=Join-Path $root ('dist\AirConnect-'+$version+'-source.zip')
Push-Location $root
try {
 $changes=git -c "safe.directory=$gitRoot" status --porcelain
 if($LASTEXITCODE) { throw 'Cannot inspect the source repository' }
 if($changes) { throw 'Commit the reviewed source before creating a release source archive' }
 git -c "safe.directory=$gitRoot" archive --format=zip HEAD "--output=$output"
 if($LASTEXITCODE) { throw 'Committed-source archive creation failed' }
 Write-Output "Source archive: $output"
 Get-FileHash -LiteralPath $output -Algorithm SHA256
} finally { Pop-Location }
