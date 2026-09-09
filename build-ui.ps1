param(
    [switch]$SkipInstall
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

Push-Location $UiRoot
try {
    if (-not $SkipInstall -and -not (Test-Path (Join-Path $RuntimeSource "electron.exe"))) {
        Write-Host "Electron runtime not found locally. Running npm install..."
        npm install --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            throw "npm install failed with exit code $LASTEXITCODE. If your corporate proxy blocks npm, configure npm's proxy/registry or populate ui\node_modules from your approved registry."
        }
    }

    if (-not (Test-Path (Join-Path $RuntimeSource "electron.exe"))) {
        throw "Electron runtime is missing: $(Join-Path $RuntimeSource 'electron.exe')"
    }

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
Copy-Item (Join-Path $RuntimeSource "*") $RuntimeDestination -Recurse -Force

New-Item -ItemType Directory -Path $AppDestination -Force | Out-Null
Copy-Item (Join-Path $UiRoot "package.json") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "index.html") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "electron-shim.js") $AppDestination -Force
Copy-Item (Join-Path $UiRoot "styles.css") $AppDestination -Force
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
