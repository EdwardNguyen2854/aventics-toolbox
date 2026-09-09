param(
    [switch]$SkipUi
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$Creo = "C:\Program Files\PTC\Creo 9.0.2.0"
$ToolkitInclude = Join-Path $Creo "Common Files\protoolkit\includes"
$ToolkitLib = Join-Path $Creo "Common Files\protoolkit\x86e_win64\obj"
$UnlockBat = Join-Path $Creo "Parametric\bin\protk_unlock.bat"

if (-not $SkipUi) {
    & (Join-Path $Root "build-ui.ps1")
}

& (Join-Path $Root "build-and-unlock.ps1") `
    -ToolkitInclude $ToolkitInclude `
    -ToolkitLib $ToolkitLib `
    -CreoCommonLib $ToolkitLib `
    -UnlockBat $UnlockBat
