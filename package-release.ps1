param(
    [string]$OutputDir = (Join-Path $PSScriptRoot "release"),
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Dll = if ($DllPath) { (Resolve-Path $DllPath).Path } else { Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll" }
$RuntimeDir = Split-Path -Parent $Dll
$WebViewLoader = Join-Path $RuntimeDir "WebView2Loader.dll"
$WebUi = Join-Path $RuntimeDir "ui"
if (-not (Test-Path $Dll)) {
    throw "Build and unlock the DLL first: $Dll"
}
if (-not (Test-Path $WebViewLoader)) {
    throw "WebView2Loader.dll is missing beside the experimental DLL: $WebViewLoader"
}
if (-not (Test-Path (Join-Path $WebUi "index.html"))) {
    throw "TypeScript UI runtime is missing: $WebUi"
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

$ReleaseRoot = Join-Path $OutputDir "AventicsToolbox_v0.5.2_typescript-ui"
if (Test-Path $ReleaseRoot) { Remove-Item $ReleaseRoot -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $ReleaseRoot "bin") -Force | Out-Null
New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null

Copy-Item $Dll (Join-Path $ReleaseRoot "bin\aventics_toolbox.dll") -Force
Copy-Item $WebViewLoader (Join-Path $ReleaseRoot "bin\WebView2Loader.dll") -Force
Copy-Item $WebUi (Join-Path $ReleaseRoot "bin\ui") -Recurse -Force
Copy-Item (Join-Path $Root "text") (Join-Path $ReleaseRoot "text") -Recurse -Force
Copy-Item (Join-Path $Root "VERSION.txt") $ReleaseRoot -Force
Copy-Item (Join-Path $Root "installer") (Join-Path $ReleaseRoot "installer") -Recurse -Force

$Readme = @"
Aventics Toolbox v0.5.2 - TypeScript UI experiment

1. Ensure Microsoft Edge WebView2 Runtime is installed.
2. Run .\installer\install.ps1
3. Register the generated protk.dat in Creo -> Tools -> Auxiliary Applications.
4. Open Aventics Toolbox from its menu / ribbon TOOLKIT command.
5. The TypeScript/WebView2 UI opens first. The existing native Creo UI remains a fallback.

The release DLL must already be unlocked by the developer before packaging.
"@
Set-Content (Join-Path $ReleaseRoot "README.txt") $Readme -Encoding utf8

$Zip = Join-Path $OutputDir "AventicsToolbox_v0.5.2_typescript-ui.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path "$ReleaseRoot\*" -DestinationPath $Zip
Write-Host "Created release package: $Zip"
