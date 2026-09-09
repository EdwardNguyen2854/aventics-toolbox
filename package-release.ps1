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
if (-not (Test-Path (Join-Path $Root "installer\diagnose.ps1"))) {
    throw "Team diagnostic script is missing: $(Join-Path $Root 'installer\diagnose.ps1')"
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
Aventics Toolbox v0.5.2 - Electron UI experiment / team test package

Teammate install location:
  C:\aventics\creo\AventicsToolbox

INSTALL / REINSTALL
1. Close Creo and Aventics Toolbox before installing or replacing a test build.
2. Extract the ZIP to a local folder.
3. Run .\installer\install.ps1
4. Register this generated file in Creo -> Tools -> Auxiliary Applications:
   C:\aventics\creo\AventicsToolbox\protk.dat
5. Open Aventics Toolbox from its menu / ribbon TOOLKIT command.

Reinstall replaces the installed bin and text folders but preserves the logs folder,
so previous diagnostics remain available for comparison.

TEAM TESTING / DIAGNOSTICS
The installed package writes both native and Electron bridge diagnostics to:
  C:\aventics\creo\AventicsToolbox\logs

If the UI stays on "Connecting to Creo..." or another startup problem occurs, keep Creo and the Toolbox open and run:
  powershell -ExecutionPolicy Bypass -File "C:\aventics\creo\AventicsToolbox\diagnose.ps1"

No administrator rights are required to run the diagnostic script.
Send these files back to the developer:
  C:\aventics\creo\AventicsToolbox\logs\team-diagnostics-*.txt
  C:\aventics\creo\AventicsToolbox\logs\aventics_toolbox.log
  C:\aventics\creo\AventicsToolbox\logs\electron_bridge.log

The diagnostic report records the Creo xtop PID, process owners/sessions, named pipes,
Electron --pipe/--creo-pid arguments, required package files, and recent bridge logs.
It does not intentionally collect model contents or Similar CAD query images.

NOTES
- Creo launches the Electron UI and communicates with it through PID-specific local Windows named pipes.
- The existing native Creo UI remains a fallback if Electron cannot be launched.
- No WebView2 SDK or WebView2Loader.dll is required.
- The release DLL must already be unlocked by the developer before packaging.
- If Windows blocks creation of C:\aventics on first install, use an existing writable C:\aventics folder or ask IT to create/grant that folder. The diagnostic script itself does not require elevation.
"@
Set-Content (Join-Path $ReleaseRoot "README.txt") $Readme -Encoding utf8

$Zip = Join-Path $OutputDir "AventicsToolbox_v0.5.2_electron-ui.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$ReleaseRoot\*" -DestinationPath $Zip
Write-Host "Created release package: $Zip"
