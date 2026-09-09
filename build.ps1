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

if (-not (Test-WebView2SdkRoot $WebView2Sdk)) {
    if (-not [string]::IsNullOrWhiteSpace($WebView2Sdk)) {
        Write-Warning "The configured WebView2 SDK is invalid and will be ignored: $WebView2Sdk"
    }

    $ExternalDir = Join-Path $Root "external"
    $CachedSdk = Join-Path $ExternalDir "Microsoft.Web.WebView2.$WebView2Version"

    if (Test-WebView2SdkRoot $CachedSdk) {
        $WebView2Sdk = $CachedSdk
    }
    else {
        Write-Host "WebView2 SDK not found locally. Downloading Microsoft.Web.WebView2 $WebView2Version..."
        New-Item -ItemType Directory -Force -Path $ExternalDir | Out-Null

        $PackageArchive = Join-Path $ExternalDir "Microsoft.Web.WebView2.$WebView2Version.zip"
        $PackageUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$WebView2Version/microsoft.web.webview2.$WebView2Version.nupkg"

        try {
            Invoke-WebRequest -Uri $PackageUrl -OutFile $PackageArchive

            if (Test-Path $CachedSdk) {
                Remove-Item -Recurse -Force $CachedSdk
            }
            New-Item -ItemType Directory -Force -Path $CachedSdk | Out-Null

            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [System.IO.Compression.ZipFile]::ExtractToDirectory($PackageArchive, $CachedSdk)
        }
        catch {
            throw "Could not download/extract Microsoft.Web.WebView2 $WebView2Version. Check network access to api.nuget.org, or set WEBVIEW2_SDK_DIR / pass -WebView2Sdk to an extracted Microsoft.Web.WebView2 package root. Details: $($_.Exception.Message)"
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
}

$WebViewHeader = Join-Path $WebView2Sdk "build\native\include\WebView2.h"
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
