param(
    [string]$Destination = (Join-Path $env:LOCALAPPDATA "Aventics\AventicsToolbox")
)

$ErrorActionPreference = "Stop"
$PackageRoot = Split-Path -Parent $PSScriptRoot
$BinSource = Join-Path $PackageRoot "bin"
$DllSource = Join-Path $BinSource "aventics_toolbox.dll"
$WebViewLoaderSource = Join-Path $BinSource "WebView2Loader.dll"
$WebUiSource = Join-Path $BinSource "ui"
$TextSource = Join-Path $PackageRoot "text"
$VersionSource = Join-Path $PackageRoot "VERSION.txt"

if (-not (Test-Path $DllSource)) {
    throw "Release DLL not found: $DllSource"
}
if (-not (Test-Path $WebViewLoaderSource)) {
    throw "WebView2 loader not found: $WebViewLoaderSource"
}
if (-not (Test-Path (Join-Path $WebUiSource "index.html"))) {
    throw "TypeScript UI runtime not found: $WebUiSource"
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
allow_stop FALSE
fail_tol TRUE
end
"@
Set-Content (Join-Path $Destination "protk.dat") $Protk -Encoding ascii

Write-Host ""
Write-Host "Aventics Toolbox TypeScript UI experiment installed to:"
Write-Host "  $Destination"
Write-Host ""
Write-Host "Register this file in Creo -> Tools -> Auxiliary Applications:"
Write-Host "  $(Join-Path $Destination 'protk.dat')"
Write-Host ""
Write-Host "Hot stop/unload is disabled for the WebView2 test build. Restart Creo to reload the DLL safely."
