param(
    [switch]$SkipUi,
    [switch]$RequireUi,
    [string]$Proxy = "",
    [string]$ElectronMirror = $env:ELECTRON_MIRROR,
    [string]$ElectronRuntimeDir = $env:ELECTRON_RUNTIME_DIR
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$Creo = "C:\Program Files\PTC\Creo 9.0.2.0"
$ToolkitInclude = Join-Path $Creo "Common Files\protoolkit\includes"
$ToolkitLib = Join-Path $Creo "Common Files\protoolkit\x86e_win64\obj"
$UnlockBat = Join-Path $Creo "Parametric\bin\protk_unlock.bat"

$UiReady = $false
if (-not $SkipUi) {
    try {
        & (Join-Path $Root "build-ui.ps1") `
            -Proxy $Proxy `
            -ElectronMirror $ElectronMirror `
            -ElectronRuntimeDir $ElectronRuntimeDir
        $UiReady = $true
    }
    catch {
        if ($RequireUi) { throw }
        Write-Warning "Electron UI could not be staged: $($_.Exception.Message)"
        Write-Warning "Continuing with the Creo DLL build. The native Creo UI remains the fallback."
        Write-Warning "To require Electron, rerun with -RequireUi. To skip the Electron attempt, use -SkipUi."
    }
}

& (Join-Path $Root "build-and-unlock.ps1") `
    -ToolkitInclude $ToolkitInclude `
    -ToolkitLib $ToolkitLib `
    -CreoCommonLib $ToolkitLib `
    -UnlockBat $UnlockBat

Write-Host ""
if ($SkipUi) {
    Write-Host "Electron UI: skipped"
} elseif ($UiReady) {
    Write-Host "Electron UI: staged and ready"
} else {
    Write-Host "Electron UI: unavailable; native Creo UI fallback will be used"
}
