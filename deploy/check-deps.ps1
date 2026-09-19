<#
.SYNOPSIS
    Checks the Windows side of a StackChan development machine.
.DESCRIPTION
    Verifies the things the Linux checker cannot see from inside WSL: the WSL
    networking mode, the Hyper-V firewall, a Chromium browser for the flasher,
    and the robot's serial port. Reports only - it installs and changes nothing,
    because every item here is either a setting on someone's machine or a
    download they should read about first.

    Writes a log, and on failure writes an error file that CI or a scheduled
    task can read without parsing stdout. Exits with the number of hard
    failures, so a pipeline step can branch on it directly.
.PARAMETER Scope
    Which checks to run: All (default), Wsl, Flashing.
.PARAMETER LogFile
    Where to write the log. Defaults beside this script.
.EXAMPLE
    .\deploy\check-deps.ps1
.EXAMPLE
    .\deploy\check-deps.ps1 -Scope Wsl
.NOTES
    File Name  : check-deps.ps1
    Version    : 1.0
    Requires   : PowerShell V5
    Companion  : deploy/check-deps.sh runs inside WSL and checks the rest.

    THIS IS NOT A TRANSLATION OF THE BASH CHECKER. The two settings below are
    configured on Windows and are invisible from inside WSL - which is why the
    Linux script can report that networking is wrong but never why, and cannot
    see the firewall at all.
#>

param (
    [ValidateSet('All', 'Wsl', 'Flashing')]
    [string]$Scope = 'All',
    [string]$LogFile = "$PSScriptRoot\check-deps.log"
)

# Variables
$ErrorActionPreference = 'Continue'
$ErrorFile   = "$PSScriptRoot\check-deps-error.txt"
$WslConfig   = Join-Path $env:USERPROFILE '.wslconfig'
$Ports       = @(8000, 8003)
# The fixed VM creator GUID for WSL, used by New-NetFirewallHyperVRule. Not a
# secret and not machine-specific - it identifies WSL itself.
$WslVmCreator = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'

$script:Passed   = 0
$script:Warnings = 0
$script:Failures = 0

# ── Logging ──────────────────────────────────────────────────────────────────
# A local Write-Log rather than a module import: anyone cloning this repo has
# neither, and a checker that fails to start is worse than no checker.

function Write-Log {
    param (
        [Parameter(Mandatory)][string]$Message,
        [string]$LogFile = $script:LogFile,
        [ValidateSet('INFO', 'PASS', 'WARN', 'ERROR')][string]$Level = 'INFO'
    )
    $stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    Add-Content -Path $LogFile -Value "$stamp [$Level] $Message" -Encoding utf8

    switch ($Level) {
        'PASS'  { Write-Host "OK    $Message" -ForegroundColor Green }
        'WARN'  { Write-Host "WARN  $Message" -ForegroundColor Yellow }
        'ERROR' { Write-Host "FAIL  $Message" -ForegroundColor Red }
        default { Write-Host "      $Message" }
    }
}

function Write-Section {
    param ([Parameter(Mandatory)][string]$Message)
    Add-Content -Path $script:LogFile -Value "`r`n>>> $Message" -Encoding utf8
    Write-Host ''
    Write-Host ">>> $Message" -ForegroundColor White
}

function Add-Pass {
    param ([Parameter(Mandatory)][string]$Message)
    Write-Log -Message $Message -Level PASS
    $script:Passed++
}

# Missing, but the project may still work. Prints the fix and carries on.
function Add-Warning {
    param (
        [Parameter(Mandatory)][string]$Message,
        [Parameter(Mandatory)][string]$Fix
    )
    Write-Log -Message $Message -Level WARN
    Write-Host "      fix: $Fix"
    Add-Content -Path $script:LogFile -Value "      fix: $Fix" -Encoding utf8
    $script:Warnings++
}

# Missing, and nothing will work until it is not.
function Add-Failure {
    param (
        [Parameter(Mandatory)][string]$Message,
        [Parameter(Mandatory)][string]$Fix
    )
    Write-Log -Message $Message -Level ERROR
    Write-Host "      fix: $Fix"
    Add-Content -Path $script:LogFile -Value "      fix: $Fix" -Encoding utf8
    Add-Content -Path $script:ErrorFile -Value "$Message | fix: $Fix" -Encoding utf8
    $script:Failures++
}

# ── WSL related tasks ────────────────────────────────────────────────────────

function Test-WslDependencies {
    Write-Section 'WSL'

    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        Add-Failure -Message 'wsl.exe not found' `
                    -Fix 'The server runs in WSL2. Install it with: wsl --install'
        return
    }
    Add-Pass 'wsl.exe present'

    $build = [System.Environment]::OSVersion.Version.Build
    if ($build -ge 22000) {
        Add-Pass "Windows 11 (build $build)"
    } else {
        Add-Warning -Message "Windows build $build" `
                    -Fix 'Mirrored WSL networking needs Windows 11 22H2 or later. Without it the robot needs netsh portproxy rules by hand.'
    }

    # 🔴 THE SETTING THAT STOPS THE ROBOT CONNECTING, AND THE REASON THIS FILE
    #    EXISTS. With WSL's default NAT a robot on Wi-Fi cannot reach a server
    #    bound inside WSL. The service is up, the port is bound, and the packet
    #    dies a layer below where anyone is looking - so it presents as a broken
    #    client rather than as a network setting.
    if (Test-Path $WslConfig) {
        $text = Get-Content $WslConfig -Raw
        if ($text -match '(?im)^\s*networkingMode\s*=\s*mirrored') {
            Add-Pass 'networkingMode=mirrored in .wslconfig'
        } else {
            Add-Failure -Message 'networkingMode is not mirrored' `
                        -Fix "Add 'networkingMode=mirrored' under [wsl2] in $WslConfig, then run: wsl --shutdown"
        }
    } else {
        Add-Failure -Message "no $WslConfig" `
                    -Fix "Create it containing [wsl2] and networkingMode=mirrored, then run: wsl --shutdown"
    }

    # 🔴 THE HYPER-V FIREWALL IS NOT WINDOWS FIREWALL. It is separate, it is on
    #    by default for WSL traffic since WSL 2.0.9, and no rule anyone has
    #    already added to Windows Firewall affects it. Reachable from Windows is
    #    NOT reachable from the LAN.
    try {
        $hv = Get-NetFirewallHyperVVMSetting -PolicyStore ActiveStore -ErrorAction Stop
        $blocking = $hv | Where-Object { $_.DefaultInboundAction -eq 'Block' }

        if (-not $blocking) {
            Add-Pass 'Hyper-V firewall is not blocking inbound by default'
        } else {
            # ⚠️ BLOCKING BY DEFAULT IS NOT THE QUESTION. A working machine
            #    blocks by default AND carries an allow rule per port; warning on
            #    the default alone reports a fact rather than a problem, and a
            #    checker that warns about a working machine teaches people to
            #    skim past its warnings.
            $allowed = @()
            $rules = Get-NetFirewallHyperVRule -ErrorAction SilentlyContinue |
                     Where-Object { $_.Direction -eq 'Inbound' -and
                                    $_.Action -eq 'Allow' -and
                                    $_.Enabled -eq $true }
            foreach ($p in $Ports) {
                foreach ($r in $rules) {
                    if ($r.LocalPorts -contains "$p" -or $r.LocalPorts -eq 'Any') {
                        $allowed += $p
                        break
                    }
                }
            }

            $missing = $Ports | Where-Object { $allowed -notcontains $_ }
            if ($missing) {
                $fix = "New-NetFirewallHyperVRule -Name StackChan -DisplayName StackChan " +
                       "-Direction Inbound -VMCreatorId '$WslVmCreator' -Protocol TCP " +
                       "-LocalPorts $($missing -join ',') -Action Allow"
                Add-Warning -Message "the Hyper-V firewall has no inbound rule for port(s) $($missing -join ', ')" -Fix $fix
            } else {
                Add-Pass "Hyper-V firewall blocks by default, but port(s) $($Ports -join ', ') are allowed"
            }
        }
    } catch {
        Add-Warning -Message "could not read the Hyper-V firewall settings: $($_.Exception.Message)" `
                    -Fix 'Run this in an elevated PowerShell. If the robot cannot connect, check it by hand.'
    }
}

# ── Flashing related tasks ───────────────────────────────────────────────────

function Test-FlashingDependencies {
    Write-Section 'Flashing'

    # The browser flasher uses Web Serial - no esptool, no drivers, no Python.
    # That is the entire point of it, so the only dependency is the browser.
    $chromium = @(
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
        "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($chromium) {
        Add-Pass "Chromium browser found ($(Split-Path $chromium -Leaf))"
    } else {
        Add-Warning -Message 'no Chrome or Edge found' `
                    -Fix 'The flasher needs Web Serial, which Firefox and Safari do not implement.'
    }

    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports) {
        Add-Pass "serial port(s) present: $($ports -join ', ')"
        Write-Log -Message "If the flasher says the port is busy, close whatever else holds it - including another flasher tab. That reads like a driver fault and is not one."
    } else {
        # ⚠️ Measured on this hardware: a robot that has dropped off USB shows as
        #    VID_0000&PID_0002 with error 43, and REPLUGGING DOES NOT CLEAR IT.
        Add-Warning -Message 'no serial ports detected' `
                    -Fix 'Fine if the robot is unplugged. If it is plugged in and missing (error 43), hold its power button for about 6 seconds - replugging alone does not clear it.'
    }
}

# ── Main Execution ───────────────────────────────────────────────────────────

Try {
    if (Test-Path $ErrorFile) { Remove-Item -Path $ErrorFile -Force }
    Set-Content -Path $LogFile -Value "StackChan Windows dependency check - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding utf8
    $script:LogFile = $LogFile
    $script:ErrorFile = $ErrorFile

    Write-Log -Message "Scope: $Scope"
    Write-Log -Message "Log file: $LogFile"

    if ($Scope -in 'All', 'Wsl')      { Test-WslDependencies }
    if ($Scope -in 'All', 'Flashing') { Test-FlashingDependencies }

    Write-Section 'Summary'
    Write-Log -Message "$script:Passed passed, $script:Warnings warnings, $script:Failures failures"

    if ($script:Failures -gt 0) {
        Write-Host ''
        Write-Host 'Not ready. The failures above are things that will not work, not preferences.' -ForegroundColor Red
        Write-Host "Details also written to $ErrorFile"
    } elseif ($script:Warnings -gt 0) {
        Write-Host ''
        Write-Host 'Probably fine. Read the warnings - most are the traps that cost this project days.' -ForegroundColor Yellow
    } else {
        Write-Host ''
        Write-Host 'Ready. Now run deploy/check-deps.sh inside WSL for the rest.' -ForegroundColor Green
    }

    Exit $script:Failures
} Catch {
    Write-Log -Message "Error: $($_.Exception.Message) - Line Number: $($_.InvocationInfo.ScriptLineNumber)" -Level ERROR
    New-Item -ItemType File -Path $ErrorFile -Value "$($_.Exception.Message)" -Force | Out-Null
    Exit 1
}
