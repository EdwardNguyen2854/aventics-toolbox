param(
    [Parameter(Mandatory=$true)]
    [string]$ToolkitInclude,

    [Parameter(Mandatory=$true)]
    [string]$ToolkitLib,

    [Parameter(Mandatory=$true)]
    [string]$CreoCommonLib,

    [Parameter(Mandatory=$true)]
    [string]$UnlockBat,

    [string]$WebView2Sdk = $env:WEBVIEW2_SDK_DIR
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Dll = Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll"

& (Join-Path $Root "build.ps1") `
    -ToolkitInclude $ToolkitInclude `
    -ToolkitLib $ToolkitLib `
    -CreoCommonLib $CreoCommonLib `
    -WebView2Sdk $WebView2Sdk

if (-not (Test-Path $UnlockBat)) {
    throw "protk_unlock.bat not found: $UnlockBat"
}
if (-not (Test-Path $Dll)) {
    throw "DLL not found before unlock: $Dll"
}

Write-Host ""
Write-Host "Unlocking DLL..."
& $UnlockBat $Dll
if ($LASTEXITCODE -ne 0) {
    throw "protk_unlock failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "========================================"
Write-Host " READY - Build + unlock successful"
Write-Host "========================================"
Write-Host "  $Dll"
