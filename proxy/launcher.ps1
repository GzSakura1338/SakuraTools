
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'Stop'

$Here      = $PSScriptRoot
$Dll       = Join-Path $Here 'MinecraftProxy_msvc.dll'
$Injector  = Join-Path $Here 'reflective_injector.exe'
$InjectPs1 = Join-Path $Here 'inject.ps1'
$LogPath   = Join-Path $env:TEMP 'MinecraftProxy.log'
$ProxyPort = 25565
$BujiIsland = [char]0x5e03 + [char]0x5409 + [char]0x5c9b

function Write-Kv([string]$k, $v, [ConsoleColor]$vcolor = 'White') {
    Write-Host ("  {0,-18}: " -f $k) -NoNewline -ForegroundColor DarkGray
    Write-Host $v -ForegroundColor $vcolor
}

function Find-McProcess {
    Add-Type -Namespace Native -Name W32 -MemberDefinition @'
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, int lParam);
        public delegate bool EnumWindowsProc(System.IntPtr hWnd, int lParam);
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern bool IsWindowVisible(System.IntPtr hWnd);
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode)]
        public static extern int GetWindowText(System.IntPtr hWnd, System.Text.StringBuilder text, int count);
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern int GetWindowThreadProcessId(System.IntPtr hWnd, out int lpdwProcessId);
'@ -ErrorAction SilentlyContinue

    $bujiPid = 0
    $sb = New-Object Text.StringBuilder 512
    $cb = [Native.W32+EnumWindowsProc]{
        param($h, $l)
        if (-not [Native.W32]::IsWindowVisible($h)) { return $true }
        $sb.Length = 0
        [void][Native.W32]::GetWindowText($h, $sb, $sb.Capacity)
        if ($sb.ToString() -match [regex]::Escape($BujiIsland)) {
            $p = 0
            [void][Native.W32]::GetWindowThreadProcessId($h, [ref]$p)
            if ($p -gt 0) {
                try {
                    $proc = Get-Process -Id $p -ErrorAction Stop
                    if ($proc.ProcessName -match '^javaw?$') { $script:bujiPid = $p; return $false }
                } catch {}
            }
        }
        $true
    }
    [void][Native.W32]::EnumWindows($cb, 0)
    if ($script:bujiPid -gt 0) {
        try { return Get-Process -Id $script:bujiPid -ErrorAction Stop } catch {}
    }

    Get-CimInstance Win32_Process -Filter "Name='java.exe' OR Name='javaw.exe'" `
        -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match 'BootstrapLauncher|forgeclient|net\.minecraft|OptiFine' } |
        ForEach-Object {
            try { return Get-Process -Id $_.ProcessId -ErrorAction Stop } catch {}
        } | Select-Object -First 1
}

function Get-ProxyListener {
    Get-NetTCPConnection -LocalPort $ProxyPort -State Listen -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

function Get-BClient {
    Get-NetTCPConnection -LocalPort $ProxyPort -State Established -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

function Get-LanAddress {
    (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.InterfaceAlias -notmatch 'Loopback|vEthernet|VMware|VirtualBox|WSL' -and
                       $_.IPAddress -notlike '169.254.*' } |
        Select-Object -First 1 -ExpandProperty IPAddress) 2>$null
}

function Format-Size($bytes) {
    if ($bytes -lt 1KB)   { return "$bytes B" }
    if ($bytes -lt 1MB)   { return "{0:N1} KB" -f ($bytes / 1KB) }
    if ($bytes -lt 1GB)   { return "{0:N1} MB" -f ($bytes / 1MB) }
    return "{0:N1} GB" -f ($bytes / 1GB)
}

function Show-Header {
    Clear-Host
    Write-Host ""
    Write-Host "  ========================================================" -ForegroundColor DarkCyan
    Write-Host "     Minecraft Injected Proxy - Interactive Launcher"      -ForegroundColor Cyan
    Write-Host "  ========================================================" -ForegroundColor DarkCyan
    Write-Host ""
}

function Show-Status {
    Write-Host "  Artifacts" -ForegroundColor Yellow
    if (Test-Path $Dll) {
        $info = Get-Item $Dll
        Write-Kv "DLL" ("{0}  ({1}, {2:yyyy-MM-dd HH:mm})" -f $info.Name, (Format-Size $info.Length), $info.LastWriteTime) Green
    } else {
        Write-Kv "DLL" ("MISSING - expected {0}" -f $Dll) Red
    }
    if (Test-Path $Injector) {
        Write-Kv "Injector" ("reflective_injector.exe (auto-wait for `"{0}`" window)" -f $BujiIsland) Green
    } else {
        Write-Kv "Injector" 'MISSING - will fall back to inject.ps1' Yellow
    }
    Write-Host ""

    Write-Host "  Runtime" -ForegroundColor Yellow
    $mc = Find-McProcess
    if ($mc) {
        Write-Kv "MC process" ("PID {0}  ({1}.exe)" -f $mc.Id, $mc.ProcessName) Green
    } else {
        Write-Kv "MC process" 'not detected (start MC first)' DarkYellow
    }
    $listener = Get-ProxyListener
    if ($listener) {
        $owner = try { (Get-Process -Id $listener.OwningProcess -ErrorAction Stop).ProcessName } catch { '?' }
        $color = if ($mc -and $listener.OwningProcess -eq $mc.Id) { 'Green' } else { 'Yellow' }
        Write-Kv "Proxy port ${ProxyPort}" ("bound by PID {0} ({1})" -f $listener.OwningProcess, $owner) $color
    } else {
        Write-Kv "Proxy port ${ProxyPort}" 'NOT bound (proxy not injected yet)' DarkYellow
    }
    $b = Get-BClient
    if ($b) {
        Write-Kv "B client" ("connected from {0}:{1}" -f $b.RemoteAddress, $b.RemotePort) Green
    } else {
        Write-Kv "B client" 'none connected' DarkGray
    }
    $lan = Get-LanAddress
    if ($lan) {
        Write-Kv "Connect address" ("127.0.0.1:{0}   or   {1}:{0}" -f $ProxyPort, $lan) Cyan
    } else {
        Write-Kv "Connect address" ("127.0.0.1:{0}" -f $ProxyPort) Cyan
    }
    if (Test-Path $LogPath) {
        $lf = Get-Item $LogPath
        Write-Kv "Log file" ("{0}  ({1})" -f $LogPath, (Format-Size $lf.Length)) DarkGray
    } else {
        Write-Kv "Log file" ("{0}  (not created yet)" -f $LogPath) DarkGray
    }
    Write-Host ""
}

function Show-Menu {
    Write-Host "  Actions" -ForegroundColor Yellow
    Write-Host "    1) Inject proxy (auto-wait for MC window)"
    Write-Host "    2) Inject proxy into a specific PID"
    Write-Host "    3) Tail live log"
    Write-Host "    4) Tail log filtered (errors/exceptions/replay/skin)"
    Write-Host "    5) Show connect address for B client"
    Write-Host "    6) Kill Minecraft process"
    Write-Host "    7) Refresh"
    Write-Host "    q) Quit"
    Write-Host ""
}

function Do-Inject {
    if (-not (Test-Path $Dll)) {
        Write-Host "ERROR: DLL not found at $Dll" -ForegroundColor Red
        return
    }
    $listener = Get-ProxyListener
    if ($listener) {
        Write-Host ("Proxy port ${ProxyPort} is already bound by PID {0}. Injection skipped." -f $listener.OwningProcess) -ForegroundColor Yellow
        return
    }
    if (Test-Path $Injector) {
        Write-Host "Launching auto-injector (Ctrl+C to cancel)..." -ForegroundColor Cyan
        & $Injector $Dll
        $rc = $LASTEXITCODE
        if ($rc -eq 0) {
            Write-Host "Injection succeeded." -ForegroundColor Green
        } else {
            Write-Host "Injector exited with code $rc." -ForegroundColor Red
            Write-Host "If MC was already running, try option [2] (inject into specific PID)." -ForegroundColor Yellow
        }
    } else {
        Write-Host "reflective_injector.exe missing; falling back to PID prompt." -ForegroundColor Yellow
        Do-InjectPid
    }
}

function Do-InjectPid {
    if (-not (Test-Path $InjectPs1)) {
        Write-Host "ERROR: inject.ps1 not found next to launcher.ps1." -ForegroundColor Red
        return
    }
    $mc = Find-McProcess
    if ($mc) {
        Write-Host ("Detected MC PID {0} ({1}.exe). Press Enter to use it, or type a different PID:" -f $mc.Id, $mc.ProcessName) -ForegroundColor Cyan
    } else {
        Write-Host "Enter target PID:" -ForegroundColor Cyan
    }
    $entry = Read-Host "PID"
    $targetPid = if ([string]::IsNullOrWhiteSpace($entry) -and $mc) { $mc.Id } else { [int]$entry }
    Write-Host ("Injecting {0} into PID {1}..." -f $Dll, $targetPid) -ForegroundColor Cyan
    & powershell -NoProfile -ExecutionPolicy Bypass -File $InjectPs1 -ProcId $targetPid -Dll $Dll
}

function Do-TailLog {
    param([string]$FilterPattern = $null)
    if (-not (Test-Path $LogPath)) {
        Write-Host "Log file does not exist yet. Inject the proxy first." -ForegroundColor Yellow
        return
    }
    if ($FilterPattern) {
        Write-Host ("Tailing $LogPath (filter: /{0}/i, Ctrl+C to stop)..." -f $FilterPattern) -ForegroundColor Cyan
    } else {
        Write-Host ("Tailing $LogPath (Ctrl+C to stop)...") -ForegroundColor Cyan
    }
    Write-Host ""
    try {
        if ($FilterPattern) {
            Get-Content -LiteralPath $LogPath -Tail 20 -Wait | Where-Object { $_ -imatch $FilterPattern }
        } else {
            Get-Content -LiteralPath $LogPath -Tail 20 -Wait
        }
    } catch [System.Management.Automation.PipelineStoppedException] {
    }
}

function Do-ShowConnect {
    $lan = Get-LanAddress
    Write-Host ""
    Write-Host "  Point client B's 'Add Server' at:" -ForegroundColor Yellow
    Write-Host ("    Same machine   :  127.0.0.1:{0}" -f $ProxyPort) -ForegroundColor Cyan
    if ($lan) {
        Write-Host ("    Same LAN       :  {0}:{1}" -f $lan, $ProxyPort) -ForegroundColor Cyan
    }
    Write-Host ""
    Write-Host "  Server name can be anything (e.g. 'Proxy'). The proxy accepts an" -ForegroundColor DarkGray
    Write-Host "  offline login, so B's Minecraft account does not need to be online." -ForegroundColor DarkGray
    Write-Host ""
}

function Do-KillMc {
    $mc = Find-McProcess
    if (-not $mc) {
        Write-Host "No MC process detected." -ForegroundColor Yellow
        return
    }
    Write-Host ("About to kill PID {0} ({1}.exe). Continue? (y/N)" -f $mc.Id, $mc.ProcessName) -ForegroundColor Yellow -NoNewline
    $ans = Read-Host " "
    if ($ans -eq 'y' -or $ans -eq 'Y') {
        Stop-Process -Id $mc.Id -Force
        Write-Host "Killed." -ForegroundColor Green
    } else {
        Write-Host "Cancelled." -ForegroundColor DarkGray
    }
}

$quit = $false
while (-not $quit) {
    Show-Header
    Show-Status
    Show-Menu
    $choice = (Read-Host "  Choice").Trim().TrimStart([char]0xFEFF)
    Write-Host ""
    switch ($choice) {
        '1' { Do-Inject;      Write-Host ""; Read-Host "Press Enter to return" | Out-Null }
        '2' { Do-InjectPid;   Write-Host ""; Read-Host "Press Enter to return" | Out-Null }
        '3' { Do-TailLog }
        '4' { Do-TailLog -FilterPattern 'error|exception|threw|replay|skin|A-GATE|MAIN-GATE|BundlerInfo|expanded bundle' }
        '5' { Do-ShowConnect; Read-Host "Press Enter to return" | Out-Null }
        '6' { Do-KillMc;      Write-Host ""; Read-Host "Press Enter to return" | Out-Null }
        '7' { continue }
        { $_ -in 'q','Q','exit','quit' } {
            Write-Host "Bye." -ForegroundColor DarkGray; $quit = $true
        }
        default {
            if ([string]::IsNullOrWhiteSpace($choice)) { continue }
            Write-Host ("Unknown choice: '{0}'" -f $choice) -ForegroundColor Red
            Start-Sleep -Milliseconds 700
        }
    }
}
