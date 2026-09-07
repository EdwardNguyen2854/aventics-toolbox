param(
    [Parameter(Mandatory=$true)]
    [string]$ToolkitInclude,

    [Parameter(Mandatory=$true)]
    [string]$ToolkitLib,

    [Parameter(Mandatory=$true)]
    [string]$CreoCommonLib
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build\vs2022"
$Dll = Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll"

Write-Host ""
Write-Host "========================================"
Write-Host " Aventics Toolbox v0.3.0 - Build"
Write-Host "========================================"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is not installed or is not on PATH. Install CMake and Visual Studio 2022 C++ tools, then rerun build-local.ps1."
}

cmake -S $Root -B $BuildDir `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DPROTOOLKIT_INCLUDE_DIR="$ToolkitInclude" `
    -DPROTOOLKIT_LIB_DIR="$ToolkitLib" `
    -DCREO_COMMON_LIB_DIR="$CreoCommonLib"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path $Dll)) {
    throw "Build completed but DLL was not found: $Dll"
}

Write-Host ""
Write-Host "Build successful:"
Write-Host "  $Dll"
