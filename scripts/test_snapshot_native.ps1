param(
    [Parameter(Mandatory = $true)][string]$JavaHome,
    [string]$MinecraftHome = "$env:APPDATA/.minecraft",
    [string]$Version = '1.20.1-Forge_47.4.23',
    [string]$TestExe = "$PSScriptRoot/../build/msvc/tests/snapshot_jni_test.exe"
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -Raw "$MinecraftHome/versions/$Version/$Version.json" | ConvertFrom-Json
$paths = [System.Collections.Generic.List[string]]::new()
$paths.Add("$MinecraftHome/libraries/net/minecraft/client/1.20.1-20230612.114412/client-1.20.1-20230612.114412-srg.jar")
$paths.Add("$MinecraftHome/libraries/net/minecraft/client/1.20.1-20230612.114412/client-1.20.1-20230612.114412-extra.jar")
foreach ($lib in $manifest.libraries) {
    $artifact = $lib.downloads.artifact.path
    if ($artifact -and $artifact -notmatch '^(net/minecraftforge|cpw)/') {
        $path = "$MinecraftHome/libraries/$artifact"
        if (Test-Path -LiteralPath $path) { $paths.Add($path) }
    }
}
& $TestExe "$JavaHome/bin/server/jvm.dll" ($paths -join ';')
if ($LASTEXITCODE -ne 0) { throw "Native snapshot test failed: $LASTEXITCODE" }
