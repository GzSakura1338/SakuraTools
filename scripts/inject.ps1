param(
    [int]$ProcId,
    [string]$Dll
)

# Compatibility entry point; maintain injection logic in proxy/inject.ps1 only.
& "$PSScriptRoot/../proxy/inject.ps1" -ProcId $ProcId -Dll $Dll
exit $LASTEXITCODE
