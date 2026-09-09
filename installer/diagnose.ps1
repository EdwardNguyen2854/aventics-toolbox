param(
    [string]$InstallRoot = "C:\aventics\creo\AventicsToolbox"
)

$ErrorActionPreference = "Continue"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $InstallRoot "logs"

try {
    New-Item -ItemType Directory -Path $logDir -Force -ErrorAction Stop | Out-Null
    $reportPath = Join-Path $logDir "team-diagnostics-$timestamp.txt"
}
catch {
    $reportPath = Join-Path (Get-Location) "team-diagnostics-$timestamp.txt"
}

$lines = New-Object System.Collections.Generic.List[string]

function Add-Line {
    param([string]$Text = "")
    $lines.Add($Text)
    Write-Host $Text
}

function Add-Section {
    param([string]$Title)
    Add-Line ""
    Add-Line ("==== " + $Title + " ====")
}

function Get-OwnerText {
    param($Process)
    try {
        $owner = Invoke-CimMethod -InputObject $Process -MethodName GetOwner -ErrorAction Stop
        if ($owner.ReturnValue -eq 0) { return "$($owner.Domain)\$($owner.User)" }
        return "<owner unavailable: return $($owner.ReturnValue)>"
    }
    catch {
        return "<owner unavailable: $($_.Exception.Message)>"
    }
}

Add-Line "Aventics Toolbox team diagnostics"
Add-Line "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
Add-Line "Current user: $env:USERDOMAIN\$env:USERNAME"
Add-Line "Computer: $env:COMPUTERNAME"
Add-Line "Install root: $InstallRoot"
Add-Line "PowerShell: $($PSVersionTable.PSVersion)"

Add-Section "Installation files"
$required = @(
    "protk.dat",
    "bin\aventics_toolbox.dll",
    "bin\electron\electron.exe",
    "bin\electron\app\package.json",
    "bin\electron\app\dist-electron\main.js",
    "bin\electron\app\dist-electron\preload.js"
)
foreach ($relative in $required) {
    $full = Join-Path $InstallRoot $relative
    $status = if (Test-Path $full) { "OK      " } else { "MISSING " }
    Add-Line ($status + $full)
}

$dll = Join-Path $InstallRoot "bin\aventics_toolbox.dll"
if (Test-Path $dll) {
    try { Add-Line "DLL SHA256: $((Get-FileHash $dll -Algorithm SHA256).Hash)" } catch {}
    try {
        $zone = Get-Item -LiteralPath $dll -Stream Zone.Identifier -ErrorAction Stop
        if ($zone) { Add-Line "WARNING: DLL has Zone.Identifier (Windows download mark). Consider Unblock-File." }
    } catch { Add-Line "DLL Windows download mark: none detected" }
}

$protk = Join-Path $InstallRoot "protk.dat"
if (Test-Path $protk) {
    Add-Line ""
    Add-Line "protk.dat:"
    try { Get-Content $protk | ForEach-Object { Add-Line "  $_" } } catch { Add-Line "  <could not read>" }
}

Add-Section "Creo processes"
$creoProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @("parametric.exe", "xtop.exe") })
if (-not $creoProcesses.Count) {
    Add-Line "No parametric.exe or xtop.exe process found."
}
foreach ($process in $creoProcesses) {
    Add-Line "$($process.Name) PID=$($process.ProcessId) Session=$($process.SessionId) Owner=$(Get-OwnerText $process)"
    Add-Line "  Executable: $($process.ExecutablePath)"
    Add-Line "  Command: $($process.CommandLine)"
}

Add-Section "Aventics named pipes"
$pipes = @(Get-ChildItem "\\.\pipe\" -ErrorAction SilentlyContinue | Where-Object Name -Like "*aventics*")
if (-not $pipes.Count) {
    Add-Line "No Aventics named pipes found."
}
foreach ($item in $pipes) { Add-Line $item.Name }

$xtops = @($creoProcesses | Where-Object Name -eq "xtop.exe")
foreach ($xtop in $xtops) {
    $mainPipe = "aventics-toolbox-$($xtop.ProcessId)"
    $similarPipe = "aventics-similar-cad-$($xtop.ProcessId)"
    Add-Line "xtop PID $($xtop.ProcessId): mainPipe=$([bool]($pipes.Name -contains $mainPipe)) similarPipe=$([bool]($pipes.Name -contains $similarPipe))"
}

Add-Section "Electron processes"
$electronProcesses = @(Get-CimInstance Win32_Process -Filter "Name='electron.exe'")
if (-not $electronProcesses.Count) {
    Add-Line "No electron.exe process found."
}
foreach ($process in $electronProcesses) {
    Add-Line "electron.exe PID=$($process.ProcessId) Session=$($process.SessionId) Owner=$(Get-OwnerText $process)"
    Add-Line "  Executable: $($process.ExecutablePath)"
    Add-Line "  Command: $($process.CommandLine)"
}

Add-Section "Bridge argument check"
$mainElectron = @($electronProcesses | Where-Object { $_.CommandLine -like "*--pipe*aventics-toolbox-*" })
if (-not $mainElectron.Count) {
    Add-Line "No Electron main process with --pipe aventics-toolbox-* was found."
}
foreach ($process in $mainElectron) {
    $pipeMatch = [regex]::Match([string]$process.CommandLine, 'aventics-toolbox-(\d+)')
    $pidMatch = [regex]::Match([string]$process.CommandLine, '--creo-pid\s+"?(\d+)')
    $pipePid = if ($pipeMatch.Success) { $pipeMatch.Groups[1].Value } else { "?" }
    $creoPid = if ($pidMatch.Success) { $pidMatch.Groups[1].Value } else { "?" }
    Add-Line "Electron PID $($process.ProcessId): pipeCreoPid=$pipePid argumentCreoPid=$creoPid match=$($pipePid -eq $creoPid)"
}

Add-Section "Shared logs"
$nativeLog = Join-Path $logDir "aventics_toolbox.log"
$electronLog = Join-Path $logDir "electron_bridge.log"
foreach ($log in @($nativeLog, $electronLog)) {
    if (Test-Path $log) {
        Add-Line ""
        Add-Line "--- $log (last 80 lines) ---"
        try { Get-Content $log -Tail 80 | ForEach-Object { Add-Line $_ } } catch { Add-Line "<could not read log>" }
    } else {
        Add-Line "Missing: $log"
    }
}

Add-Section "Summary hints"
if (-not (Test-Path $dll)) { Add-Line "FAIL: native DLL is missing." }
if (-not $xtops.Count) { Add-Line "INFO: start Creo before rerunning diagnostics." }
if ($xtops.Count -and -not $pipes.Count) { Add-Line "FAIL: Creo is running but no Aventics pipes exist; inspect native startup/logging." }
if ($pipes.Count -and -not $mainElectron.Count) { Add-Line "FAIL: native pipes exist but Electron main process has no matching --pipe argument." }
if ((Test-Path $electronLog)) { Add-Line "INFO: electron_bridge.log contains the exact named-pipe handshake/error sequence." }
Add-Line ""
Add-Line "Send this report plus both files from $logDir to the developer."

try {
    $lines | Set-Content -Path $reportPath -Encoding UTF8 -ErrorAction Stop
    Write-Host ""
    Write-Host "Diagnostic report saved to:"
    Write-Host "  $reportPath"
}
catch {
    Write-Warning "Could not save diagnostic report: $($_.Exception.Message)"
}
