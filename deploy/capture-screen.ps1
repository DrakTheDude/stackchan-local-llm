<#
.SYNOPSIS
    Collects screen captures the robot prints to its serial console.
.DESCRIPTION
    The firmware, when built with kDumpScreenToSerial on, base64-encodes a JPEG
    of its own screen every few seconds and prints it between SCREENDUMP BEGIN
    and SCREENDUMP END markers. This reads them off the port and writes .jpg
    files.

    Serial rather than a network upload on purpose: the tool that did this over
    HTTP took a URL from its caller and was removed, because on a device driven
    by a microphone with no confirmation step that is exfiltration waiting for a
    misheard sentence. A cable somebody is holding has no such problem.
.PARAMETER Count
    How many frames to collect before stopping. Default 4.
.PARAMETER OutDir
    Where to write them. Default .\screens
.PARAMETER Seconds
    Overall cap, so this can never sit on the port forever. Default 180.
.NOTES
    File Name  : capture-screen.ps1
    Version    : 1.0
    Requires   : PowerShell V5

    🔴 IT ALWAYS CLOSES THE PORT. A long-lived or unbounded capture has wedged
       COM ports on this project before, needing a physical replug. Bounded by
       both a frame count and a time cap, and the close is in a finally.

    🔴 IT DOES NOT RESET THE DEVICE. DTR and RTS are deasserted before Open(),
       because those two lines are the ESP32's auto-reset circuit and .NET's
       SerialPort drives them by default.
#>

param (
    [int]$Count = 4,
    [string]$OutDir = "$PSScriptRoot\..\screens",
    [int]$Seconds = 180
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\stackchan-drax\deploy\find-stackchan-port.ps1" -ErrorAction SilentlyContinue

# Fall back to a plain scan if the helper is not beside this checkout.
if (-not (Get-Command Find-StackChanPort -ErrorAction SilentlyContinue)) {
    function Find-StackChanPort { [System.IO.Ports.SerialPort]::GetPortNames() | Select-Object -Last 1 }
}

$port = Find-StackChanPort
if (-not $port) { Write-Error 'No serial port found - is the robot plugged in?'; exit 1 }

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

Write-Output "port   : $port"
Write-Output "output : $OutDir"
Write-Output "waiting for $Count frame(s), up to ${Seconds}s..."

$sp = New-Object System.IO.Ports.SerialPort $port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 1000
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()

$got = 0
$buf = New-Object System.Text.StringBuilder
$collecting = $false
$deadline = (Get-Date).AddSeconds($Seconds)

try {
    while ($got -lt $Count -and (Get-Date) -lt $deadline) {
        try { $line = $sp.ReadLine() } catch [TimeoutException] { continue }
        $line = $line.TrimEnd("`r")

        if ($line -match 'SCREENDUMP BEGIN (\d+)') {
            $expected = [int]$Matches[1]
            [void]$buf.Clear()
            $collecting = $true
            continue
        }
        if ($line -eq 'SCREENDUMP END') {
            $collecting = $false
            try {
                $bytes = [Convert]::FromBase64String($buf.ToString())
            } catch {
                Write-Output "  frame dropped: $($_.Exception.Message)"
                continue
            }
            if ($bytes.Length -ne $expected) {
                Write-Output "  frame dropped: got $($bytes.Length) bytes, expected $expected"
                continue
            }
            $got++
            $name = Join-Path $OutDir ("screen-{0:yyyyMMdd-HHmmss}-{1}.jpg" -f (Get-Date), $got)
            [System.IO.File]::WriteAllBytes($name, $bytes)
            Write-Output "  $name  ($($bytes.Length) bytes)"
            continue
        }
        if ($collecting -and $line.StartsWith('SD:')) {
            [void]$buf.Append($line.Substring(3))
        }
    }
} finally {
    $sp.Close()
    $sp.Dispose()
}

if ($got -eq 0) {
    Write-Output 'No frames. Is this build made with kDumpScreenToSerial = true?'
    exit 1
}
Write-Output "collected $got frame(s)"
