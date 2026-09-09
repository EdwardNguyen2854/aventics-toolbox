param(
    [Parameter(Mandatory=$false)]
    [string]$InstallDir = $PSScriptRoot
)

$ErrorActionPreference = "Stop"
$InstallDir = (Resolve-Path $InstallDir).Path
$DevDll = Join-Path $InstallDir "dist\x86e_win64\obj\aventics_toolbox.dll"
$ReleaseDll = Join-Path $InstallDir "bin\aventics_toolbox.dll"

if (Test-Path $DevDll) { $Dll = $DevDll }
elseif (Test-Path $ReleaseDll) { $Dll = $ReleaseDll }
else { throw "aventics_toolbox.dll was not found under dist\x86e_win64\obj or bin." }

$Content = @"
name AventicsToolbox
startup dll
exec_file $Dll
text_dir $InstallDir
allow_stop FALSE
fail_tol TRUE
end
"@

$Out = Join-Path $InstallDir "protk.dat"
Set-Content -Path $Out -Value $Content -Encoding ascii
Write-Host "Created: $Out"
Write-Host "Hot stop/unload is disabled for the WebView2 test build. Restart Creo to reload the DLL."
