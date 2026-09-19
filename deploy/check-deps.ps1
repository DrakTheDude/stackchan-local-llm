# Is WINDOWS ready for StackChan?
#
# The bash checker runs inside WSL and can see that networking is wrong. It
# cannot see WHY, and it cannot see the firewall at all - both of those live on
# the Windows side. That is the whole reason this file exists; it is not a
# translation of the bash one.
#
# 🔴 THE TRAP THIS IS FOR. The robot talks to a server bound inside WSL. With
#    WSL's default NAT it cannot reach it, and with the Hyper-V firewall - which
#    is separate from Windows Firewall and ON by default since WSL 2.0.9 - it
#    cannot reach it either. In both cases the service is up, the port is bound,
#    and the packet dies one layer below where anyone is looking.
#
#    Reachable from Windows is NOT the same as reachable from the LAN.
#
# Reports. Does not change anything: these are settings on somebody's machine,
# and a script that silently rewrites .wslconfig has earned nobody's trust.
#
#   .\deploy\check-deps.ps1

$ErrorActionPreference = 'Continue'
$script:Pass = 0; $script:Warn = 0; $script:Fail = 0

function Section($t) { Write-Host "`n== $t ==" -ForegroundColor White }
function Ok($m)      { Write-Host "  [ok]   $m" -ForegroundColor Green; $script:Pass++ }
function Warn($m,$h) { Write-Host "  [warn] $m" -ForegroundColor Yellow; Write-Host "         $h"; $script:Warn++ }
function Bad($m,$h)  { Write-Host "  [FAIL] $m" -ForegroundColor Red;    Write-Host "         $h"; $script:Fail++ }

Section 'windows'

$build = [System.Environment]::OSVersion.Version.Build
if ($build -ge 22000) { Ok "Windows 11 (build $build)" }
else { Warn "Windows build $build" 'Mirrored WSL networking needs Windows 11 22H2 or later. Without it you need netsh portproxy rules by hand.' }

Section 'wsl'

$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
if (-not $wsl) {
    Bad 'wsl.exe not found' 'The server runs in WSL2. Install it with: wsl --install'
} else {
    Ok 'wsl.exe present'

    # --- networkingMode. The single most common cause of "the robot cannot
    #     connect" on this project.
    $cfg = Join-Path $env:USERPROFILE '.wslconfig'
    if (Test-Path $cfg) {
        $text = Get-Content $cfg -Raw
        if ($text -match '(?im)^\s*networkingMode\s*=\s*mirrored') {
            Ok 'networkingMode=mirrored in .wslconfig'
        } else {
            Bad 'networkingMode is not mirrored' "Add 'networkingMode=mirrored' under [wsl2] in $cfg, then run: wsl --shutdown. Default NAT means the robot cannot reach a server bound inside WSL."
        }
    } else {
        Bad "no $cfg" "Create it with:`n           [wsl2]`n           networkingMode=mirrored`n         then: wsl --shutdown"
    }

    # --- the Hyper-V firewall, which is not Windows Firewall and is separate
    #     from every rule anyone has already added.
    $hv = Get-NetFirewallHyperVVMSetting -PolicyStore ActiveStore -ErrorAction SilentlyContinue
    if ($null -eq $hv) {
        Warn 'could not read the Hyper-V firewall settings' 'Needs an elevated shell, or this build predates it. Check by hand if the robot cannot connect.'
    } else {
        $blocking = $hv | Where-Object { $_.DefaultInboundAction -eq 'Block' }
        if ($blocking) {
            Warn 'the Hyper-V firewall blocks inbound to WSL by default' "It is separate from Windows Firewall and on by default since WSL 2.0.9. If the robot cannot reach the server, allow the port:`n         New-NetFirewallHyperVRule -Name StackChan -DisplayName StackChan -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol TCP -LocalPorts 8000,8003 -Action Allow"
        } else {
            Ok 'Hyper-V firewall is not blocking inbound by default'
        }
    }
}

Section 'flashing'

# The browser flasher uses Web Serial, which is Chromium-only. No esptool, no
# drivers, no Python - that is the point of it.
$chromium = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "$env:ProgramFiles (x86)\Microsoft\Edge\Application\msedge.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($chromium) { Ok "a Chromium browser is installed ($(Split-Path $chromium -Leaf))" }
else { Warn 'no Chrome or Edge found' 'The browser flasher needs Web Serial, which Firefox and Safari do not implement.' }

$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($ports) {
    Ok "serial port(s) present: $($ports -join ', ')"
    Write-Host "         If the flasher says 'port busy', close whatever else has it open -"
    Write-Host "         including another flasher tab. That reads like a driver fault and is not one."
} else {
    Warn 'no serial ports' "Fine if the robot is unplugged. If it IS plugged in and missing (VID_0000&PID_0002, error 43), hold its power button for about 6 seconds - replugging alone does not clear it."
}

Write-Host ''
Write-Host "$script:Pass passed, $script:Warn warnings, $script:Fail failures"
if ($script:Fail -gt 0) {
    Write-Host 'Not ready. The failures above are things that will not work, not preferences.' -ForegroundColor Red
} elseif ($script:Warn -gt 0) {
    Write-Host 'Probably fine. Read the warnings - most are the traps that cost us days.' -ForegroundColor Yellow
} else {
    Write-Host 'Ready.' -ForegroundColor Green
}
exit $script:Fail
