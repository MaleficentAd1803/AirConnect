$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$version=((Get-Content (Join-Path $root 'src\version.h') | Where-Object { $_ -match '^#define AC_VERSION ' }) -split '"')[1]
$output=Join-Path $root ('dist\AirConnect-'+$version+'-source.zip')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Push-Location $root
try {
 $files=rg --files --hidden --no-ignore -g '!.git/**' -g '!build/**' -g '!dist/**' -g '!dependencies/runtime/**' -g '!dependencies/lib/*.a' -g '!test-data/**' -g '!media-test-output/**' -g '!*.log' -g '!*.key' -g '!*.dpapi' -g '!*.mp4' -g '!*.png' -g '!setup.inf' -g '!setup.rpt'
 if($LASTEXITCODE) { throw 'Source enumeration failed' }
 $stream=[IO.File]::Open($output,[IO.FileMode]::Create)
 $archive=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
 try {
  foreach($relative in $files) {
   $absolute=[IO.Path]::GetFullPath((Join-Path $root $relative))
   if(-not $absolute.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Source path escaped the project: $relative" }
   $entry=$archive.CreateEntry($relative.Replace('\','/'),[IO.Compression.CompressionLevel]::Optimal)
   $destination=$entry.Open();$source=[IO.File]::OpenRead($absolute)
   try { $source.CopyTo($destination) } finally { $source.Dispose();$destination.Dispose() }
  }
 } finally { $archive.Dispose();$stream.Dispose() }
 Write-Output "Source archive: $($files.Count) files"
 Get-FileHash -LiteralPath $output -Algorithm SHA256
} finally { Pop-Location }
