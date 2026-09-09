param(
    [string]$OutputDir = (Join-Path $PSScriptRoot "release"),
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Dll = if ($DllPath) { (Resolve-Path $DllPath).Path } else { Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll" }
$RuntimeDir = Split-Path -Parent $Dll
$ElectronRuntime = Join-Path $RuntimeDir "electron"
if (-not (Test-Path $Dll)) {
    throw "Build and unlock the DLL first: $Dll"
}
if (-not (Test-Path (Join-Path $ElectronRuntime "electron.exe"))) {
    throw "Electron runtime is missing. Run .\build-ui.ps1 first: $ElectronRuntime"
}
if (-not (Test-Path (Join-Path $ElectronRuntime "app\package.json"))) {
    throw "Electron application files are missing: $(Join-Path $ElectronRuntime 'app')"
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

$ReleaseRoot = Join-Path $OutputDir "AventicsToolbox_v0.5.2_electron-ui"
if (Test-Path $ReleaseRoot) { Remove-Item $ReleaseRoot -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $ReleaseRoot "bin") -Force | Out-Null

Copy-Item $Dll (Join-Path $ReleaseRoot "bin\aventics_toolbox.dll") -Force
Copy-Item $ElectronRuntime (Join-Path $ReleaseRoot "bin\electron") -Recurse -Force
Copy-Item (Join-Path $Root "text") (Join-Path $ReleaseRoot "text") -Recurse -Force
Copy-Item (Join-Path $Root "VERSION.txt") $ReleaseRoot -Force
Copy-Item (Join-Path $Root "installer") (Join-Path $ReleaseRoot "installer") -Recurse -Force

$Readme = @"
Aventics Toolbox v0.5.2 - Electron UI experiment

1. Run .\installer\install.ps1
2. Register the generated protk.dat in Creo -> Tools -> Auxiliary Applications.
3. Open Aventics Toolbox from its menu / ribbon TOOLKIT command.
4. Creo launches the Electron UI and communicates with it through a PID-specific Windows named pipe.
5. The existing native Creo UI remains a fallback if Electron cannot be launched.

No WebView2 SDK or WebView2Loader.dll is required.
The release DLL must already be unlocked by the developer before packaging.
"@
Set-Content (Join-Path $ReleaseRoot "README.txt") $Readme -Encoding utf8

$Zip = Join-Path $OutputDir "AventicsToolbox_v0.5.2_electron-ui.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$ReleaseRoot\*" -DestinationPath $Zip
Write-Host "Created release package: $Zip"
