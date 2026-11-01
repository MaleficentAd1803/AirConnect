param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$Toolchain='',
 [switch]$SkipTests
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
$Toolchain=Find-AirConnectToolchain $Toolchain
$root=Split-Path $PSScriptRoot -Parent
$env:PATH=(Join-Path $Toolchain 'bin')+';'+$env:PATH
$build=Join-Path $root ('build\'+$Configuration.ToLowerInvariant())
$compiler=(Join-Path $Toolchain 'bin').Replace('\','/')
cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_C_COMPILER=$compiler/gcc.exe" "-DCMAKE_CXX_COMPILER=$compiler/g++.exe" "-DCMAKE_RC_COMPILER=$compiler/windres.exe"
if($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build $build --parallel 4
if($LASTEXITCODE) { throw 'Compilation failed' }
if(-not $SkipTests) {
 ctest --test-dir $build --output-on-failure
 if($LASTEXITCODE) { throw 'Tests failed; packaging is stopped' }
}
