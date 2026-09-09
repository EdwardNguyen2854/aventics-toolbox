param(
    [string]$Destination = "C:\aventics\creo\AventicsToolbox"
)

$ErrorActionPreference = "Stop"
if (Test-Path $Destination) {
    Remove-Item $Destination -Recurse -Force
    Write-Host "Removed: $Destination"
} else {
    Write-Host "Aventics Toolbox is not installed at: $Destination"
}
Write-Host "If it is still registered in Creo Auxiliary Applications, unregister that entry manually."
