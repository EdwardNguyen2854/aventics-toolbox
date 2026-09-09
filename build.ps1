param(
    [Parameter(Mandatory=$true)]
    [string]$ToolkitInclude,

    [Parameter(Mandatory=$true)]
    [string]$ToolkitLib,

    [Parameter(Mandatory=$true)]
    [string]$CreoCommonLib,

    [string]$WebView2Sdk = $env:WEBVIEW2_SDK_DIR
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build\vs2022"
$Dll = Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll"

Write-Host ""
Write-Host "========================================"
Write-Host " Aventics Toolbox - TypeScript UI Build"
Write-Host "========================================"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is not installed or is not on PATH. Install CMake and Visual Studio 2022 C++ tools, then rerun build-local.ps1."
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw "npm is not installed or is not on PATH. Install Node.js LTS to build the TypeScript UI."
}
if ([string]::IsNullOrWhiteSpace($WebView2Sdk)) {
    throw "Set WEBVIEW2_SDK_DIR, or pass -WebView2Sdk, pointing to the extracted Microsoft.Web.WebView2 NuGet package root."
}
$WebViewHeader = Join-Path $WebView2Sdk "build\native\include\WebView2.h"
if (-not (Test-Path $WebViewHeader)) {
    throw "WebView2.h was not found: $WebViewHeader"
}

cmake -S $Root -B $BuildDir `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DPROTOOLKIT_INCLUDE_DIR="$ToolkitInclude" `
    -DPROTOOLKIT_LIB_DIR="$ToolkitLib" `
    -DCREO_COMMON_LIB_DIR="$CreoCommonLib" `
    -DAVENTICS_TYPESCRIPT_UI=ON `
    -DWEBVIEW2_SDK_DIR="$WebView2Sdk"

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
Write-Host "  Web UI: $(Join-Path $Root 'dist\x86e_win64\obj\ui\index.html')"
