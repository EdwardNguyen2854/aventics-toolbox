param(
    [string]$WebView2Sdk = $env:WEBVIEW2_SDK_DIR,
    [string]$WebView2Version = "1.0.4191.47"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$Creo = "C:\Program Files\PTC\Creo 9.0.2.0"
$ToolkitInclude = Join-Path $Creo "Common Files\protoolkit\includes"
$ToolkitLib = Join-Path $Creo "Common Files\protoolkit\x86e_win64\obj"
$UnlockBat = Join-Path $Creo "Parametric\bin\protk_unlock.bat"

& (Join-Path $Root "build-and-unlock.ps1") `
    -ToolkitInclude $ToolkitInclude `
    -ToolkitLib $ToolkitLib `
    -CreoCommonLib $ToolkitLib `
    -UnlockBat $UnlockBat `
    -WebView2Sdk $WebView2Sdk `
    -WebView2Version $WebView2Version
