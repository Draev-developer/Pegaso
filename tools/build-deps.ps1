# Pegaso - builds the third-party static libraries the application links against.
# Usage:  pwsh tools\build-deps.ps1 [-Configuration Release|Debug] [-Only zlib,mbedtls,...]
# Libraries are expected one level above srchybrid (see readme.txt):
#   cryptopp, id3lib, mbedtls, miniupnpc, ResizableLib, zlib
# libtorrent (BitTorrent) comes with Boost and OpenSSL from vcpkg, see the end of this script.
# Outputs are copied to the paths emule.vcxproj links from.
param(
    [ValidateSet('Release', 'Debug')] [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$Toolset = 'v145',
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
# "-File" passes "a,b,c" as one string; accept both forms.
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLower() } | Where-Object { $_ })
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.ATLMFC -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the MFC component was found.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
$generator = 'Visual Studio 18 2026'
$crt = if ($Configuration -eq 'Debug') { 'MultiThreadedDebug' } else { 'MultiThreaded' }

function Want($name) { return ($Only.Count -eq 0) -or ($Only -contains $name) }
function Run($exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}
function MsBuildLib($project, $config, [string[]]$extra = @()) {
    Run $msbuild (@($project, '/m', '/nologo', '/v:minimal', "/p:Configuration=$config", "/p:Platform=$Platform",
            "/p:PlatformToolset=$Toolset", '/p:WindowsTargetPlatformVersion=10.0') + $extra)
}
function CMakeLib($source, $build, [string[]]$options) {
    Run 'cmake' (@('-S', $source, '-B', $build, '-G', $generator, '-A', $Platform, "-T", $Toolset,
            '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW', "-DCMAKE_MSVC_RUNTIME_LIBRARY=$crt") + $options)
    Run 'cmake' @('--build', $build, '--config', $Configuration, '--parallel')
}
function Publish($from, $to) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
    Copy-Item $from $to -Force
    Write-Host "  -> $to"
}

if (Want 'cryptopp') {
    Write-Host '== Crypto++'
    MsBuildLib "$root\cryptopp\cryptlib.vcxproj" $Configuration
    Publish "$root\cryptopp\$Platform\Output\$Configuration\cryptlib.lib" "$root\cryptopp\$Platform\$Configuration\cryptlib.lib"
}

if (Want 'id3lib') {
    Write-Host '== id3lib'
    # id3lib's Windows instructions: config.h.win32 must be provided as config.h.
    if (-not (Test-Path "$root\id3lib\config.h")) { Copy-Item "$root\id3lib\config.h.win32" "$root\id3lib\config.h" }
    # Compile against the same zlib headers the application uses, not the copy bundled with id3lib.
    $env:CL = "/I`"$root\zlib`""
    try { MsBuildLib "$root\id3lib\libprj\id3lib.vcxproj" $Configuration } finally { $env:CL = $null }
}

if (Want 'resizablelib') {
    Write-Host '== ResizableLib'
    $cfg = "$Configuration Static Unicode"
    $out = "$root\ResizableLib\_out\$Platform\$Configuration\"
    MsBuildLib "$root\ResizableLib\ResizableLib.vcxproj" $cfg @("/p:OutDir=$out")
    $lib = Get-ChildItem $out -Filter '*.lib' | Select-Object -First 1
    Publish $lib.FullName "$root\ResizableLib\$Platform\$Configuration\resizablelib.lib"
}

if (Want 'zlib') {
    Write-Host '== zlib'
    $build = "$root\zlib\_build"
    CMakeLib "$root\zlib" $build @('-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_TESTING=OFF', '-DZLIB_INSTALL=OFF')
    $lib = Get-ChildItem "$build\$Configuration" -Filter '*.lib' | Select-Object -First 1
    Publish $lib.FullName "$root\zlib\contrib\vstudio\vc\$Platform\$Configuration\zlib.lib"
}

if (Want 'miniupnpc') {
    Write-Host '== miniupnpc'
    $build = "$root\miniupnpc\_build"
    CMakeLib "$root\miniupnpc" $build @('-DUPNPC_BUILD_SHARED=OFF', '-DUPNPC_BUILD_STATIC=ON', '-DUPNPC_BUILD_TESTS=OFF',
        '-DUPNPC_BUILD_SAMPLE=OFF', '-DUPNPC_NO_INSTALL=ON')
    $lib = Get-ChildItem "$build\$Configuration" -Filter '*.lib' | Select-Object -First 1
    Publish $lib.FullName "$root\miniupnpc\msvc\$Platform\$Configuration\miniupnpc.lib"
}

if (Want 'mbedtls') {
    Write-Host '== Mbed TLS'
    # The application supplies its own mutexes (srchybrid/TLSthreading.cpp), so the
    # threading layer must be enabled in the shared configuration header.
    $cryptoInc = "$root\mbedtls\tf-psa-crypto\include"
    Copy-Item "$PSScriptRoot\threading_alt.h" "$cryptoInc\threading_alt.h" -Force
    $cfgFile = "$cryptoInc\psa\crypto_config.h"
    $text = [IO.File]::ReadAllText($cfgFile)
    $text = $text -replace '(?m)^//#define MBEDTLS_THREADING_ALT\s*$', '#define MBEDTLS_THREADING_ALT'
    $text = $text -replace '(?m)^//#define MBEDTLS_THREADING_C\s*$', '#define MBEDTLS_THREADING_C'
    [IO.File]::WriteAllText($cfgFile, $text)

    $build = "$root\mbedtls\_build"
    CMakeLib "$root\mbedtls" $build @('-DENABLE_PROGRAMS=OFF', '-DENABLE_TESTING=OFF', '-DUSE_STATIC_MBEDTLS_LIBRARY=ON',
        '-DUSE_SHARED_MBEDTLS_LIBRARY=OFF', '-DGEN_FILES=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF')
    # emule.vcxproj links a single mbedtls.lib: merge TLS, X.509 and crypto archives.
    $libs = Get-ChildItem $build -Recurse -Filter '*.lib' | Where-Object { $_.DirectoryName -like "*\$Configuration" } |
        Select-Object -ExpandProperty FullName
    $libexe = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1 |
        ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\lib.exe' }
    $target = "$root\mbedtls\visualc\vs2017\$Platform\$Configuration\mbedtls.lib"
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    Run $libexe (@('/NOLOGO', "/OUT:$target") + $libs)
    Write-Host "  -> $target (merged from $($libs.Count) archives)"
}

if (Want 'libtorrent') {
    Write-Host '== libtorrent, with Boost and OpenSSL (through vcpkg; the first build takes a long time)'
    # Kept outside of the source tree: three levels above srchybrid, where emule.vcxproj looks for it
    # (override with the PegasoVcpkg MSBuild property). vcpkg builds both Release and Debug.
    $vcpkg = Join-Path (Split-Path -Parent (Split-Path -Parent $root)) 'vcpkg'
    if (-not (Test-Path "$vcpkg\vcpkg.exe")) {
        if (-not (Test-Path $vcpkg)) { Run 'git' @('clone', '--depth', '1', 'https://github.com/microsoft/vcpkg', $vcpkg) }
        Run "$vcpkg\bootstrap-vcpkg.bat" @('-disableMetrics')
    }
    Run "$vcpkg\vcpkg.exe" @('install', "libtorrent[core]:$Platform-windows-static", '--clean-buildtrees-after-build')
    # QUIC for connections between two low IDs (PegasoQuic.cpp)
    Run "$vcpkg\vcpkg.exe" @('install', "ngtcp2[openssl]:$Platform-windows-static", '--clean-buildtrees-after-build')
}

Write-Host 'Dependencies built.'
