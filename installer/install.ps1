param(
    [string]$Destination = "C:\aventics\creo\AventicsToolbox"
)

$ErrorActionPreference = "Stop"
$PackageRoot = Split-Path -Parent $PSScriptRoot
$BinSource = Join-Path $PackageRoot "bin"
$DllSource = Join-Path $BinSource "aventics_toolbox.dll"
$ElectronSource = Join-Path $BinSource "electron"
$TextSource = Join-Path $PackageRoot "text"
$VersionSource = Join-Path $PackageRoot "VERSION.txt"
$DiagnosticSource = Join-Path $PSScriptRoot "diagnose.ps1"

if (-not (Test-Path $DllSource)) {
    throw "Release DLL not found: $DllSource"
}
if (-not (Test-Path (Join-Path $ElectronSource "electron.exe"))) {
    throw "Electron runtime not found: $ElectronSource"
}
if (-not (Test-Path (Join-Path $ElectronSource "app\package.json"))) {
    throw "Electron application files not found: $(Join-Path $ElectronSource 'app')"
}
if (-not (Test-Path $TextSource)) {
    throw "Release text directory not found: $TextSource"
}
if (-not (Test-Path $DiagnosticSource)) {
    throw "Team diagnostic script not found: $DiagnosticSource"
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$BinDestination = Join-Path $Destination "bin"
$LogDestination = Join-Path $Destination "logs"
New-Item -ItemType Directory -Path $BinDestination -Force | Out-Null
New-Item -ItemType Directory -Path $LogDestination -Force | Out-Null

Copy-Item (Join-Path $BinSource "*") $BinDestination -Recurse -Force
Copy-Item $TextSource (Join-Path $Destination "text") -Recurse -Force
Copy-Item $DiagnosticSource (Join-Path $Destination "diagnose.ps1") -Force
if (Test-Path $VersionSource) { Copy-Item $VersionSource $Destination -Force }

$Dll = Join-Path $BinDestination "aventics_toolbox.dll"
$Protk = @"
name AventicsToolbox
startup dll
exec_file $Dll
text_dir $Destination
allow_stop TRUE
fail_tol TRUE
end
"@
Set-Content (Join-Path $Destination "protk.dat") $Protk -Encoding ascii

Write-Host ""
Write-Host "Aventics Toolbox installed to:"
Write-Host "  $Destination"
Write-Host ""
Write-Host "Register this file in Creo -> Tools -> Auxiliary Applications:"
Write-Host "  $(Join-Path $Destination 'protk.dat')"
Write-Host ""
Write-Host "Team-test logs will be written to:"
Write-Host "  $LogDestination"
Write-Host ""
Write-Host "If the Toolbox stays on 'Connecting to Creo...', run this without admin rights:"
Write-Host "  powershell -ExecutionPolicy Bypass -File `"$(Join-Path $Destination 'diagnose.ps1')`""
Write-Host "Then send the generated team-diagnostics file and the logs folder to the developer."
