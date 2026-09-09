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

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$BinDestination = Join-Path $Destination "bin"
New-Item -ItemType Directory -Path $BinDestination -Force | Out-Null

Copy-Item (Join-Path $BinSource "*") $BinDestination -Recurse -Force
Copy-Item $TextSource (Join-Path $Destination "text") -Recurse -Force
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
