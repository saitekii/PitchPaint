# End-to-end check: PitchPaint's MIDI output as REAPER delivers it to the next plugin in the chain.
# Uses an isolated REAPER config (-cfgfile) under build/, so your normal REAPER settings are untouched,
# and REAPER's Dummy Audio device (audioconfig mode=4), so a busy or missing audio interface can't block it.
# REAPER is never asked to quit (that can raise a "save project?" prompt): this script waits until REAPER's
# work is done and then ends the process itself.
# Usage: powershell -File Tests/reaper/run.ps1 [-Reaper path\to\reaper.exe] [-Config Release]
param(
    [string]$Reaper = "",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."

if (-not $Reaper) {
    $install = Get-ItemProperty HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\* -ErrorAction SilentlyContinue |
        Where-Object DisplayName -match "^REAPER" | Select-Object -First 1
    $Reaper = if ($install -and $install.InstallLocation) { Join-Path $install.InstallLocation "reaper.exe" } else { "C:\Program Files\REAPER (x64)\reaper.exe" }
}
if (-not (Test-Path $Reaper)) { throw "REAPER not found at $Reaper (pass -Reaper)" }

$vst3Dir = "$root\build\PitchPaint_artefacts\$Config\VST3"
if (-not (Test-Path "$vst3Dir\PitchPaint.vst3")) { throw "Build the plugin first: $vst3Dir\PitchPaint.vst3 not found" }

$work = "$root\build\reaper-test"
New-Item -ItemType Directory -Force "$work\Effects" | Out-Null
Copy-Item "$PSScriptRoot\pp_midimon.jsfx" "$work\Effects\" -Force
Remove-Item "$work\*.wav", "$work\*.rpp", "$work\make_projects.log" -ErrorAction SilentlyContinue
@"
[REAPER]
vstpath64=$vst3Dir
showsplash=0
[audioconfig]
mode=4
dummy_srate=48000
dummy_blocksize=128
"@ | Set-Content "$work\reaper-test.ini" -Encoding ascii

# Starts REAPER and returns once it exits by itself, or once $isDone reports its work finished (then ends it).
function Invoke-Reaper([string[]]$reaperArgs, [scriptblock]$isDone) {
    $p = Start-Process $Reaper -ArgumentList (@("-newinst", "-nosplash", "-cfgfile", "`"$work\reaper-test.ini`"") + $reaperArgs) -PassThru
    $deadline = (Get-Date).AddSeconds(120)
    while (-not $p.HasExited) {
        if (& $isDone) { Start-Sleep -Milliseconds 500; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; return }
        if ((Get-Date) -gt $deadline) { Stop-Process -Id $p.Id -Force; throw "REAPER timed out: $reaperArgs" }
        Start-Sleep -Milliseconds 250
    }
}

# A rendered WAV is complete once it exists and its size has stopped changing.
function Test-FileSettled([string]$path) {
    if (-not (Test-Path $path)) { return $false }
    $a = (Get-Item $path).Length; Start-Sleep -Milliseconds 700; $b = (Get-Item $path).Length
    return $a -gt 0 -and $a -eq $b
}

$env:PITCHPAINT_TEST_DIR = $work
Invoke-Reaper @("`"$PSScriptRoot\make_projects.lua`"") { Test-Path "$work\make_projects.log" }
$log = Get-Content "$work\make_projects.log" -ErrorAction SilentlyContinue
if ($log -ne "ok") { throw "Project setup failed: $log" }

foreach ($name in "mpe", "mono") {
    Invoke-Reaper @("-renderproject", "`"$work\$name.rpp`"") { Test-FileSettled "$work\$name.wav" }
}

python "$PSScriptRoot\check.py" $work
exit $LASTEXITCODE
