param(
    [switch]$SkipUi,
    [switch]$RequireUi,
    [string]$Creo = $env:CREO_ROOT,
    [string]$Proxy = "",
    [string]$ElectronMirror = $env:ELECTRON_MIRROR,
    [string]$ElectronRuntimeDir = $env:ELECTRON_RUNTIME_DIR
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Test-CreoRoot {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }

    $Include = Join-Path $Path "Common Files\protoolkit\includes"
    $Lib = Join-Path $Path "Common Files\protoolkit\x86e_win64\obj"
    $Unlock = Join-Path $Path "Parametric\bin\protk_unlock.bat"

    return (Test-Path $Include) -and (Test-Path $Lib) -and (Test-Path $Unlock)
}

if ([string]::IsNullOrWhiteSpace($Creo)) {
    $CreoCandidates = @(
        (Join-Path $env:USERPROFILE "Creo Home\Creo 11.0-M050"),
        "C:\Program Files\PTC\Creo 9.0.2.0"
    )

    $AvailableCreo = @($CreoCandidates | Where-Object { Test-CreoRoot $_ })

    if ($AvailableCreo.Count -eq 0) {
        throw "No supported Creo installation was found. Use -Creo <path> or set CREO_ROOT. Checked: $($CreoCandidates -join '; ')"
    }

    $Creo = $AvailableCreo[0]

    if ($AvailableCreo.Count -gt 1) {
        Write-Host "Multiple Creo installations found; using the first match."
        Write-Host "Use -Creo <path> or CREO_ROOT to choose a specific installation."
    }
} elseif (-not (Test-CreoRoot $Creo)) {
    throw "Creo installation does not contain the required Pro/TOOLKIT files: $Creo"
}

$Creo = (Resolve-Path $Creo).Path
$ToolkitInclude = Join-Path $Creo "Common Files\protoolkit\includes"
$ToolkitLib = Join-Path $Creo "Common Files\protoolkit\x86e_win64\obj"
$UnlockBat = Join-Path $Creo "Parametric\bin\protk_unlock.bat"

Write-Host "Creo: $Creo"

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
