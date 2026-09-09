param(
    [Parameter(Mandatory=$true)]
    [string]$ToolkitInclude,

    [Parameter(Mandatory=$true)]
    [string]$ToolkitLib,

    [Parameter(Mandatory=$true)]
    [string]$CreoCommonLib,

    [string]$WebView2Sdk = $env:WEBVIEW2_SDK_DIR,
    [string]$WebView2Version = "1.0.4191.47"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build\vs2022"
$Dll = Join-Path $Root "dist\x86e_win64\obj\aventics_toolbox.dll"

Write-Host ""
Write-Host "========================================"
Write-Host " Aventics Toolbox - TypeScript UI Build"
Write-Host "========================================"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is not installed or is not on PATH. Install CMake and Visual Studio 2022 C++ tools, then rerun build-local.ps1."
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw "npm is not installed or is not on PATH. Install Node.js LTS to build the TypeScript UI."
}

function Test-WebView2SdkRoot {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }

    return Test-Path (Join-Path $Path "build\native\include\WebView2.h")
}

function Find-WebView2InPackageRoot {
    param([string]$PackageRoot)

    if ([string]::IsNullOrWhiteSpace($PackageRoot) -or -not (Test-Path $PackageRoot)) {
        return $null
    }

    $PackageDir = Join-Path $PackageRoot "microsoft.web.webview2"
    if (-not (Test-Path $PackageDir)) {
        return $null
    }

    $Exact = Join-Path $PackageDir $WebView2Version
    if (Test-WebView2SdkRoot $Exact) {
        return (Resolve-Path $Exact).Path
    }

    $Latest = Get-ChildItem $PackageDir -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Where-Object { Test-WebView2SdkRoot $_.FullName } |
        Select-Object -First 1

    if ($Latest) {
        return $Latest.FullName
    }

    return $null
}

function Find-ExistingWebView2Sdk {
    $ExternalDir = Join-Path $Root "external"
    $ExactExternal = Join-Path $ExternalDir "Microsoft.Web.WebView2.$WebView2Version"
    if (Test-WebView2SdkRoot $ExactExternal) {
        return (Resolve-Path $ExactExternal).Path
    }

    if (Test-Path $ExternalDir) {
        $Extracted = Get-ChildItem $ExternalDir -Directory -Filter "Microsoft.Web.WebView2*" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Where-Object { Test-WebView2SdkRoot $_.FullName } |
            Select-Object -First 1
        if ($Extracted) {
            return $Extracted.FullName
        }
    }

    $PackageRoots = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($env:NUGET_PACKAGES)) {
        $PackageRoots.Add($env:NUGET_PACKAGES)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $PackageRoots.Add((Join-Path $env:USERPROFILE ".nuget\packages"))
    }

    foreach ($CommandName in @("dotnet", "nuget")) {
        if (-not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
            continue
        }
        try {
            if ($CommandName -eq "dotnet") {
                $Output = & dotnet nuget locals global-packages --list 2>$null
            }
            else {
                $Output = & nuget locals global-packages -list 2>$null
            }
            foreach ($Line in $Output) {
                if ($Line -match "global-packages:\s*(.+)$") {
                    $PackageRoots.Add($Matches[1].Trim())
                }
            }
        }
        catch {
            # Cache probing is best-effort only.
        }
    }

    foreach ($PackageRoot in ($PackageRoots | Select-Object -Unique)) {
        $Found = Find-WebView2InPackageRoot $PackageRoot
        if ($Found) {
            return $Found
        }
    }

    return $null
}

function Download-WebView2Sdk {
    param(
        [string]$PackageUrl,
        [string]$PackageArchive
    )

    $InvokeParams = @{
        Uri = $PackageUrl
        OutFile = $PackageArchive
    }

    $InvokeCommand = Get-Command Invoke-WebRequest
    if ($InvokeCommand.Parameters.ContainsKey("UseBasicParsing")) {
        $InvokeParams["UseBasicParsing"] = $true
    }

    # Corporate Windows environments often expose the proxy through WinINET/PAC.
    # Resolve it and reuse the current Windows credentials when possible.
    try {
        $TargetUri = [Uri]$PackageUrl
        $SystemProxy = [System.Net.WebRequest]::GetSystemWebProxy()
        if ($null -ne $SystemProxy) {
            $ProxyUri = $SystemProxy.GetProxy($TargetUri)
            if ($null -ne $ProxyUri -and $ProxyUri.AbsoluteUri -ne $TargetUri.AbsoluteUri) {
                Write-Host "Using system proxy: $($ProxyUri.Scheme)://$($ProxyUri.Host):$($ProxyUri.Port)"
                $InvokeParams["Proxy"] = $ProxyUri
                if ($InvokeCommand.Parameters.ContainsKey("ProxyUseDefaultCredentials")) {
                    $InvokeParams["ProxyUseDefaultCredentials"] = $true
                }
            }
        }
    }
    catch {
        Write-Verbose "Could not resolve the Windows system proxy: $($_.Exception.Message)"
    }

    Invoke-WebRequest @InvokeParams
}

if (-not (Test-WebView2SdkRoot $WebView2Sdk)) {
    if (-not [string]::IsNullOrWhiteSpace($WebView2Sdk)) {
        Write-Warning "The configured WebView2 SDK is invalid and will be ignored: $WebView2Sdk"
    }

    $WebView2Sdk = Find-ExistingWebView2Sdk
}

if (-not (Test-WebView2SdkRoot $WebView2Sdk)) {
    $ExternalDir = Join-Path $Root "external"
    $CachedSdk = Join-Path $ExternalDir "Microsoft.Web.WebView2.$WebView2Version"

    Write-Host "WebView2 SDK was not found in the repo or NuGet caches."
    Write-Host "Attempting to download Microsoft.Web.WebView2 $WebView2Version..."
    New-Item -ItemType Directory -Force -Path $ExternalDir | Out-Null

    $PackageArchive = Join-Path $ExternalDir "Microsoft.Web.WebView2.$WebView2Version.zip"
    $PackageUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$WebView2Version/microsoft.web.webview2.$WebView2Version.nupkg"

    try {
        Download-WebView2Sdk -PackageUrl $PackageUrl -PackageArchive $PackageArchive

        if (Test-Path $CachedSdk) {
            Remove-Item -Recurse -Force $CachedSdk
        }
        New-Item -ItemType Directory -Force -Path $CachedSdk | Out-Null

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($PackageArchive, $CachedSdk)
    }
    catch {
        $ManualHint = @"
Could not download/extract Microsoft.Web.WebView2 $WebView2Version.

This is usually caused by an authenticated corporate proxy. The build also checks your normal NuGet global cache automatically.

Manual fallback:
  1. Download Microsoft.Web.WebView2 $WebView2Version in a browser or from your corporate NuGet feed.
  2. Extract the .nupkg (it is a ZIP archive).
  3. Run:
       .\build-local.ps1 -WebView2Sdk "C:\path\to\extracted\Microsoft.Web.WebView2.$WebView2Version"

The selected folder must contain:
  build\native\include\WebView2.h

Details: $($_.Exception.Message)
"@
        throw $ManualHint
    }
    finally {
        if (Test-Path $PackageArchive) {
            Remove-Item -Force $PackageArchive
        }
    }

    if (-not (Test-WebView2SdkRoot $CachedSdk)) {
        throw "WebView2 SDK download completed, but WebView2.h was not found under: $CachedSdk"
    }

    $WebView2Sdk = $CachedSdk
}

Write-Host "WebView2 SDK: $WebView2Sdk"

cmake -S $Root -B $BuildDir `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DPROTOOLKIT_INCLUDE_DIR="$ToolkitInclude" `
    -DPROTOOLKIT_LIB_DIR="$ToolkitLib" `
    -DCREO_COMMON_LIB_DIR="$CreoCommonLib" `
    -DAVENTICS_TYPESCRIPT_UI=ON `
    -DWEBVIEW2_SDK_DIR="$WebView2Sdk"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path $Dll)) {
    throw "Build completed but DLL was not found: $Dll"
}

Write-Host ""
Write-Host "Build successful:"
Write-Host "  $Dll"
Write-Host "  Web UI: $(Join-Path $Root 'dist\x86e_win64\obj\ui\index.html')"
