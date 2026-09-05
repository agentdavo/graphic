param(
    [string]$Build = 'build/native',
    [string]$Mesa = 'build/deps/mesa/x64',
    [string]$Cppcheck = 'build/deps/cppcheck-src'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not $env:VULKAN_SDK) { throw 'Set VULKAN_SDK to an installed Vulkan SDK.' }
$compilerBin = (Resolve-Path 'w64devkit/bin').Path
$analysisBin = (Resolve-Path $Cppcheck).Path
$driverDll = (Resolve-Path (Join-Path $Mesa 'vulkan_lvp.dll')).Path
$manifest = Get-Content (Join-Path $Mesa 'lvp_icd.x86_64.json') -Raw | ConvertFrom-Json
# w64devkit's shell normalizes environment paths to forward slashes. The
# Windows loader fails to resolve Mesa's relative DLL path through that form.
# An absolute DLL name inside the generated manifest survives the conversion.
$manifest.ICD.library_path = $driverDll
New-Item -ItemType Directory -Path $Build -Force | Out-Null
$manifestPath = Join-Path (Resolve-Path $Build).Path 'lavapipe.json'
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding ascii
$savedPath = $env:PATH
$savedDriver = $env:VK_DRIVER_FILES
try {
    $env:PATH = $compilerBin + ';' + $analysisBin + ';' + (Join-Path $env:VULKAN_SDK 'Bin') + ';' + $savedPath
    $env:VK_DRIVER_FILES = $manifestPath
    & (Join-Path $compilerBin 'make.exe') HEADLESS=1 "BUILD=$Build" -j4 test
    $result = $LASTEXITCODE
} finally {
    $env:PATH = $savedPath
    $env:VK_DRIVER_FILES = $savedDriver
}
exit $result
