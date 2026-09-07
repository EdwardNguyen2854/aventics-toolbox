param(
    [string]$Destination = (Join-Path $env:LOCALAPPDATA "Aventics\AventicsToolbox")
)

$ErrorActionPreference = "Stop"
$PackageRoot = Split-Path -Parent $PSScriptRoot
$BinSource = Join-Path $PackageRoot "bin\aventics_toolbox.dll"
$TextSource = Join-Path $PackageRoot "text"
$VersionSource = Join-Path $PackageRoot "VERSION.txt"

if (-not (Test-Path $BinSource)) {
    throw "Release DLL not found: $BinSource"
}
if (-not (Test-Path $TextSource)) {
    throw "Release text directory not found: $TextSource"
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Destination "bin") -Force | Out-Null

Copy-Item $BinSource (Join-Path $Destination "bin\aventics_toolbox.dll") -Force
Copy-Item $TextSource (Join-Path $Destination "text") -Recurse -Force
if (Test-Path $VersionSource) { Copy-Item $VersionSource $Destination -Force }

$Dll = Join-Path $Destination "bin\aventics_toolbox.dll"
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
