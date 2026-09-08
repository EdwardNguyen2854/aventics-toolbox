param(
    [string]$OutputDir = (Join-Path $PSScriptRoot "release"),
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Dll = if ($DllPath) { (Resolve-Path $DllPath).Path } else { Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll" }
if (-not (Test-Path $Dll)) {
    throw "Build and unlock the DLL first: $Dll"
}

$PrimaryResource = Join-Path $Root "text\resource\aventics_toolbox.res"
$AsciiResource = Join-Path $Root "text\usascii\resource\aventics_toolbox.res"
if (-not (Test-Path $PrimaryResource) -or -not (Test-Path $AsciiResource)) {
    throw "Both native resource copies must exist before packaging."
}
if ((Get-FileHash $PrimaryResource -Algorithm SHA256).Hash -ne (Get-FileHash $AsciiResource -Algorithm SHA256).Hash) {
    throw "Native resource copies differ. Mirror text\resource\aventics_toolbox.res to text\usascii\resource before packaging."
}

$ReleaseRoot = Join-Path $OutputDir "AventicsToolbox_v0.4.0"
if (Test-Path $ReleaseRoot) { Remove-Item $ReleaseRoot -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $ReleaseRoot "bin") -Force | Out-Null
New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null

Copy-Item $Dll (Join-Path $ReleaseRoot "bin\aventics_toolbox.dll") -Force
Copy-Item (Join-Path $Root "text") (Join-Path $ReleaseRoot "text") -Recurse -Force
Copy-Item (Join-Path $Root "VERSION.txt") $ReleaseRoot -Force
Copy-Item (Join-Path $Root "installer") (Join-Path $ReleaseRoot "installer") -Recurse -Force

$Readme = @"
Aventics Toolbox v0.4.0

1. Run .\installer\install.ps1
2. Register the generated protk.dat in Creo -> Tools -> Auxiliary Applications.
3. Open Aventics Toolbox from its menu / ribbon TOOLKIT command.

The release DLL must already be unlocked by the developer before packaging.
"@
Set-Content (Join-Path $ReleaseRoot "README.txt") $Readme -Encoding utf8

$Zip = Join-Path $OutputDir "AventicsToolbox_v0.4.0_release.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$ReleaseRoot\*" -DestinationPath $Zip
Write-Host "Created release package: $Zip"
