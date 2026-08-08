param(
    [string]$Dll = (Join-Path $PSScriptRoot 'MinecraftProxy_msvc.dll'),
    [string]$Injector = (Join-Path $PSScriptRoot 'reflective_injector.exe'),
    [int]$WaitSeconds = 8
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Injector)) {
    throw "Reflective injector not found: $Injector"
}
if (-not (Test-Path -LiteralPath $Dll)) {
    throw "DLL not found: $Dll"
}

$listener = Get-NetTCPConnection -LocalPort 25565 -State Listen `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if ($listener) {
    throw "Port 25565 is already owned by PID $($listener.OwningProcess)."
}

Write-Host "DLL: $Dll"
Write-Host "Injector: $Injector (reflective mapping)"
Write-Host 'Waiting for a Java window containing Buji Island (50 ms scan interval)...' `
    -ForegroundColor Cyan

& $Injector $Dll
if ($LASTEXITCODE -ne 0) {
    throw "Reflective injector failed with exit code $LASTEXITCODE."
}

Start-Sleep -Seconds $WaitSeconds
$listener = Get-NetTCPConnection -LocalPort 25565 -State Listen `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $listener) {
    throw 'Injection returned successfully, but 127.0.0.1:25565 is not listening.'
}

Write-Host "Injection complete: PID $($listener.OwningProcess), 127.0.0.1:25565" `
    -ForegroundColor Green
