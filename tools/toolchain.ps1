function Find-AirConnectToolchain([string]$Requested) {
 if($Requested) { $candidate=$Requested }
 elseif($env:AIRCONNECT_TOOLCHAIN) { $candidate=$env:AIRCONNECT_TOOLCHAIN }
 else {
  $cache=Join-Path (Split-Path $PSScriptRoot -Parent) 'build\release\CMakeCache.txt'
  if(Test-Path -LiteralPath $cache) {
   $line=Get-Content -LiteralPath $cache | Where-Object { $_ -match '^CMAKE_C_COMPILER:[^=]+=' } | Select-Object -First 1
   if($line) { $candidate=Split-Path (Split-Path ($line -replace '^.*?=','') -Parent) -Parent }
  }
  if(-not $candidate) { $candidate=Split-Path (Split-Path (Get-Command gcc.exe -ErrorAction Stop).Source -Parent) -Parent }
 }
 foreach($name in @('gcc.exe','g++.exe','windres.exe','dlltool.exe')) {
  if(-not (Test-Path -LiteralPath (Join-Path $candidate "bin\$name"))) { throw "Missing native toolchain program: $name in $candidate" }
 }
 return $candidate
}
