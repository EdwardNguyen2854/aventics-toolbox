param(
    [switch]$SkipInstall,
    [string]$Proxy = "",
    [string]$ElectronMirror = $env:ELECTRON_MIRROR,
    [string]$ElectronRuntimeDir = $env:ELECTRON_RUNTIME_DIR
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$UiRoot = Join-Path $Root "ui"
$RuntimeSource = Join-Path $UiRoot "node_modules\electron\dist"
$RuntimeDestination = Join-Path $Root "dist\x86e_win64\obj\electron"
$AppDestination = Join-Path $RuntimeDestination "app"

Write-Host ""
Write-Host "========================================"
Write-Host " Aventics Toolbox - Electron UI Build"
Write-Host "========================================"

if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw "npm is not installed or is not on PATH. Install Node.js LTS first."
}

function Get-UsableSetting {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return "" }
    $trimmed = $Value.Trim()
    if ($trimmed -eq "null" -or $trimmed -eq "undefined") { return "" }
    return $trimmed
}

function Resolve-ProxySetting {
    if (-not [string]::IsNullOrWhiteSpace($Proxy)) { return $Proxy.Trim() }

    foreach ($candidate in @(
        $env:HTTPS_PROXY,
        $env:https_proxy,
        $env:HTTP_PROXY,
        $env:http_proxy
    )) {
        $usable = Get-UsableSetting $candidate
        if ($usable) { return $usable }
    }

    try {
        $value = Get-UsableSetting ((& npm config get https-proxy 2>$null | Select-Object -Last 1) -as [string])
        if ($value) { return $value }
        $value = Get-UsableSetting ((& npm config get proxy 2>$null | Select-Object -Last 1) -as [string])
        if ($value) { return $value }
    } catch {}

    if (Get-Command git -ErrorAction SilentlyContinue) {
        try {
            $value = Get-UsableSetting ((& git config --get http.proxy 2>$null | Select-Object -Last 1) -as [string])
            if ($value) { return $value }
            $value = Get-UsableSetting ((& git config --global --get http.proxy 2>$null | Select-Object -Last 1) -as [string])
            if ($value) { return $value }
        } catch {}
    }

    return ""
}

function Configure-ElectronDownloadEnvironment {
    $resolvedProxy = Resolve-ProxySetting
    $env:ELECTRON_GET_USE_PROXY = "1"

    if ($resolvedProxy) {
        Write-Host "Electron download proxy: $resolvedProxy"
        $env:HTTPS_PROXY = $resolvedProxy
        $env:HTTP_PROXY = $resolvedProxy
        $env:https_proxy = $resolvedProxy
        $env:http_proxy = $resolvedProxy
        $env:GLOBAL_AGENT_HTTP_PROXY = $resolvedProxy
    } else {
        Write-Host "Electron download proxy: none discovered"
    }

    if (-not [string]::IsNullOrWhiteSpace($ElectronMirror)) {
        $env:ELECTRON_MIRROR = $ElectronMirror.TrimEnd('/') + "/"
        Write-Host "Electron mirror: $env:ELECTRON_MIRROR"
    }
}

function Find-ElectronRuntime {
    if (-not [string]::IsNullOrWhiteSpace($ElectronRuntimeDir)) {
        $candidate = $ElectronRuntimeDir.Trim('"')
        if (Test-Path (Join-Path $candidate "electron.exe")) { return (Resolve-Path $candidate).Path }
        throw "ElectronRuntimeDir does not contain electron.exe: $candidate"
    }

    if (Test-Path (Join-Path $RuntimeSource "electron.exe")) {
        return $RuntimeSource
    }

    try {
        $globalRoot = (& npm root -g 2>$null | Select-Object -Last 1) -as [string]
        if ($globalRoot) {
            $globalRuntime = Join-Path $globalRoot.Trim() "electron\dist"
            if (Test-Path (Join-Path $globalRuntime "electron.exe")) { return $globalRuntime }
        }
    } catch {}

    return ""
}

$resolvedRuntime = Find-ElectronRuntime

Push-Location $UiRoot
try {
    if (-not $SkipInstall) {
        Write-Host "Installing npm dependencies without running package install scripts..."
        npm install --ignore-scripts --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            throw "npm dependency install failed with exit code $LASTEXITCODE."
        }
    }

    if (-not $resolvedRuntime) {
        if ($SkipInstall) {
            throw "Electron runtime is unavailable while -SkipInstall is set. Use -ElectronRuntimeDir or rerun without -SkipInstall."
        }

        Configure-ElectronDownloadEnvironment
        $ElectronInstallScript = Join-Path $UiRoot "node_modules\electron\install.js"
        if (-not (Test-Path $ElectronInstallScript)) {
            throw "Electron install script is missing after npm install: $ElectronInstallScript"
        }

        Write-Host "Downloading/staging the Electron runtime..."
        & node $ElectronInstallScript
        if ($LASTEXITCODE -ne 0) {
            $hint = "Electron runtime download failed. The npm registry is reachable, but GitHub Releases may be blocked by your corporate network."
            $hint += " Configure npm/git proxy settings, rerun with -Proxy 'http://proxy:port', use -ElectronMirror '<approved mirror>/',"
            $hint += " or supply an extracted Electron runtime with -ElectronRuntimeDir 'C:\path\containing\electron.exe'."
            throw $hint
        }

        $resolvedRuntime = Find-ElectronRuntime
    }

    if (-not $resolvedRuntime) {
        throw "Electron runtime is unavailable. Use -ElectronRuntimeDir, configure a proxy/mirror, or rerun without -SkipInstall."
    }

    Write-Host "Electron runtime: $resolvedRuntime"
    npm run build
    if ($LASTEXITCODE -ne 0) {
        throw "Electron TypeScript build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

if (Test-Path $RuntimeDestination) {
    Remove-Item $RuntimeDestination -Recurse -Force
}
New-Item -ItemType Directory -Path $RuntimeDestination -Force | Out-Null
Copy-Item (Join-Path $resolvedRuntime "*") $RuntimeDestination -Recurse -Force

New-Item -ItemType Directory -Path $AppDestination -Force | Out-Null
Copy-Item (Join-Path $UiRoot "package.json") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "index.html") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "styles.css") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "similar-cad.css") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "electron-shim.js") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "dist") $AppDestination -Recurse -Force
Copy-Item (Join-Path $UiRoot "dist-electron") $AppDestination -Recurse -Force

Write-Host ""
Write-Host "Electron UI staged:"
Write-Host "  $(Join-Path $RuntimeDestination 'electron.exe')"
Write-Host "  $AppDestination"
Write-Host ""
Write-Host "Preview without Creo:"
Write-Host "  cd ui"
Write-Host "  npm run dev"
