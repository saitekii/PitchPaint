# End-to-end check: PitchPaint's MIDI output as REAPER delivers it to the next plugin in the chain.
# Uses an isolated REAPER config (-cfgfile) under build/, so your normal REAPER settings are untouched.
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
"@ | Set-Content "$work\reaper-test.ini" -Encoding ascii

function Invoke-Reaper([string[]]$reaperArgs) {
    $p = Start-Process $Reaper -ArgumentList (@("-newinst", "-nosplash", "-cfgfile", "`"$work\reaper-test.ini`"") + $reaperArgs) -PassThru
    if (-not $p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; throw "REAPER timed out: $reaperArgs" }
}

$env:PITCHPAINT_TEST_DIR = $work
Invoke-Reaper @("`"$PSScriptRoot\make_projects.lua`"")
$log = Get-Content "$work\make_projects.log" -ErrorAction SilentlyContinue
if ($log -ne "ok") { throw "Project setup failed: $log" }

foreach ($name in "mpe", "mono") { Invoke-Reaper @("-renderproject", "`"$work\$name.rpp`"") }

python "$PSScriptRoot\check.py" $work
exit $LASTEXITCODE
