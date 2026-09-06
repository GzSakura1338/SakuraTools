param(
    [string]$BuildDir = "$PSScriptRoot/../build/msvc",
    [string]$VisualStudio,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$SkipTests,
    [switch]$WithoutJvmTests
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if (-not $VisualStudio) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $VisualStudio = & $vswhere -latest -prerelease -products '*' -property installationPath
    }
}
if (-not $VisualStudio) {
    throw 'Visual Studio not found. Pass -VisualStudio <installation directory>.'
}
$devShell = Join-Path $VisualStudio 'Common7/Tools/VsDevCmd.bat'
$cmakeDir = Join-Path $VisualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$cmake = Join-Path $cmakeDir 'cmake.exe'
$ctest = Join-Path $cmakeDir 'ctest.exe'
foreach ($tool in @($devShell, $cmake, $ctest)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Required build tool missing: $tool" }
}

# Import the x64 compiler environment into this PowerShell process.
$compilerEnvironment = & cmd.exe /d /c ('"{0}" -arch=x64 >nul && set' -f $devShell)
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed.' }
foreach ($line in $compilerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -LiteralPath "Env:$($Matches[1])" -Value $Matches[2]
    }
}
$jvmTests = if ($WithoutJvmTests) { 'OFF' } else { 'ON' }
# Capture /showIncludes with PowerShell's native decoding. Some localized CMake
# installations decode this prefix incorrectly and silently lose dependencies.
$probe = & cl.exe /nologo /showIncludes /Zs "$root/cmake/include_probe.c"
if ($LASTEXITCODE -ne 0) { throw 'Compiler include-prefix detection failed.' }
$includeLine = $probe | Where-Object { $_ -match 'stddef\.h\s*$' } | Select-Object -First 1
if (-not $includeLine -or $includeLine -notmatch '^(.*?)[A-Za-z]:[\\/]') {
    throw 'Could not detect the compiler include prefix.'
}
$includePrefix = $Matches[1]
& $cmake -S $root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DBUILD_TESTING=ON' "-DPROXY_JVM_TESTS=$jvmTests" "-DPROXY_MSVC_INCLUDE_PREFIX:STRING=$includePrefix"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if (-not $SkipTests) {
    & $ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
Write-Host "Build complete. Production binaries: $root/proxy"
