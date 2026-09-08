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

$ResourceNames = @(
    "aventics_toolbox.res",
    "aventics_weak.res",
    "aventics_accuracy.res",
    "aventics_inspection.res",
    "aventics_instance_builder.res"
)
foreach ($ResourceName in $ResourceNames) {
    $PrimaryResource = Join-Path $Root ("text\resource\" + $ResourceName)
    $AsciiResource = Join-Path $Root ("text\usascii\resource\" + $ResourceName)
    if (-not (Test-Path $PrimaryResource) -or -not (Test-Path $AsciiResource)) {
        throw "Both native resource copies must exist before packaging: $ResourceName"
    }
    if ((Get-FileHash $PrimaryResource -Algorithm SHA256).Hash -ne (Get-FileHash $AsciiResource -Algorithm SHA256).Hash) {
        throw "Native resource copies differ: $ResourceName"
    }
}

$ReleaseRoot = Join-Path $OutputDir "AventicsToolbox_v0.5.2"
if (Test-Path $ReleaseRoot) { Remove-Item $ReleaseRoot -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $ReleaseRoot "bin") -Force | Out-Null
New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null

Copy-Item $Dll (Join-Path $ReleaseRoot "bin\aventics_toolbox.dll") -Force
Copy-Item (Join-Path $Root "text") (Join-Path $ReleaseRoot "text") -Recurse -Force
Copy-Item (Join-Path $Root "VERSION.txt") $ReleaseRoot -Force
Copy-Item (Join-Path $Root "installer") (Join-Path $ReleaseRoot "installer") -Recurse -Force

$Readme = @"
Aventics Toolbox v0.5.2

1. Run .\installer\install.ps1
2. Register the generated protk.dat in Creo -> Tools -> Auxiliary Applications.
3. Open Aventics Toolbox from its menu / ribbon TOOLKIT command.
4. Use the Control Panel to open each tool in its own native Creo GUI.

The release DLL must already be unlocked by the developer before packaging.
"@
Set-Content (Join-Path $ReleaseRoot "README.txt") $Readme -Encoding utf8

$Zip = Join-Path $OutputDir "AventicsToolbox_v0.5.2_release.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$ReleaseRoot\*" -DestinationPath $Zip
Write-Host "Created release package: $Zip"
