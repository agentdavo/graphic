param(
    [ValidateSet('ucrt64','clang64')][string]$Toolchain = 'ucrt64',
    [switch]$Rebuild,
    [switch]$Headless,
    [int]$Frame = 750,
    [string]$Out = 'build/omega/hero.png'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$suffix = if ($Toolchain -eq 'ucrt64') { 'ucrt' } else { 'clang' }
$buildDir = "build/omega-$suffix"
if ($Headless) { $buildDir += '-headless' }
$exe = Join-Path $buildDir 'ex_21_omega.exe'
if ($Rebuild -or -not (Test-Path -LiteralPath $exe)) {
    & "$PSScriptRoot/build-msys2.ps1" -Toolchain $Toolchain -Target omega -Headless:$Headless
}
$savedPath = $env:PATH
try {
    $env:PATH = "C:/msys64/$Toolchain/bin;" + $savedPath
    if ($Headless) {
        $outParent = Split-Path $Out -Parent
        if ($outParent) { New-Item -ItemType Directory -Path $outParent -Force | Out-Null }
        & $exe --headless --frame $Frame --out $Out
    } else { & $exe }
    $result = $LASTEXITCODE
} finally { $env:PATH = $savedPath }
exit $result
