param(
    [ValidateSet('ucrt64','clang64')][string]$Toolchain = 'ucrt64',
    [string]$Target = 'all',
    [switch]$Headless,
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not $env:VULKAN_SDK) { throw 'Set VULKAN_SDK to the installed Vulkan SDK.' }
$compilerBin = "C:/msys64/$Toolchain/bin"
$make = Join-Path $compilerBin 'mingw32-make.exe'
if (-not (Test-Path $make)) { throw "MSYS2 toolchain not found: $compilerBin" }
$suffix = if ($Toolchain -eq 'ucrt64') { 'ucrt' } else { 'clang' }
$buildDir = "build/omega-$suffix"
if ($Headless) { $buildDir += '-headless' }
$glfw = 'build/deps/glfw/glfw-3.5.1.bin.WIN64'
if (-not $Headless -and -not (Test-Path "$glfw/include/GLFW/glfw3.h")) {
    New-Item -ItemType Directory -Path 'build/deps' -Force | Out-Null
    Invoke-WebRequest -Uri 'https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.bin.WIN64.zip' -OutFile 'build/deps/glfw.zip'
    Expand-Archive -LiteralPath 'build/deps/glfw.zip' -DestinationPath 'build/deps/glfw' -Force
}
$savedPath = $env:PATH
try {
    $env:PATH = $compilerBin + ';C:/msys64/usr/bin;' + (Join-Path $env:VULKAN_SDK 'Bin') + ';' + $savedPath
    $buildArgs = @('SHELL=C:/msys64/usr/bin/sh.exe', "BUILD=$buildDir", "SND_BUILD=$buildDir/sndmin", "-j$Jobs", $Target)
    if ($Toolchain -eq 'clang64') { $buildArgs += @('CC=clang', 'AR=llvm-ar', 'ANALYZER=') }
    else { $buildArgs += @('CC=gcc', 'AR=ar') }
    if ($Headless) { $buildArgs += 'HEADLESS=1' }
    else {
        $buildArgs += "GLFW_CFLAGS=-I$glfw/include"
        $buildArgs += "GLFW_LIBS=$glfw/lib-mingw-w64/libglfw3.a -lgdi32"
    }
    # Tests write expected diagnostics to stderr. Under 'Stop', Windows
    # PowerShell 5.1 turns a native command's stderr into a terminating
    # error, so the exit code is the only verdict that counts here.
    $ErrorActionPreference = 'Continue'
    & $make @buildArgs
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw "$Toolchain build failed ($LASTEXITCODE)." }
    # GCC runs -fanalyzer in the build. Clang has a separate analyzer driver.
    if ($Toolchain -eq 'clang64' -and $Target -in @('all','omega')) {
        $analysisArgs = @('--analyze', '-std=c11', '-Xanalyzer', '-analyzer-output=text', '-Isrc', '-Idemo', '-Ithird_party', "-I$buildDir", 'examples/21_omega.c')
        if ($Headless) { $analysisArgs += '-DVKMIN_NO_PLATFORM' }
        & (Join-Path $compilerBin 'clang.exe') @analysisArgs
        if ($LASTEXITCODE -ne 0) { throw 'Clang scene analysis failed.' }
    }
    Write-Host "Built $Target with $Toolchain in $buildDir"
} finally { $env:PATH = $savedPath }
