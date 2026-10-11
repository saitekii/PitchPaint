-- Run by run.ps1 inside REAPER: builds the test projects (PitchPaint -> MIDI monitor JSFX, and PitchPaint's own
-- preview sound). It does not quit REAPER (that can prompt to save); run.ps1 ends the process once the log is written.
local dir = os.getenv("PITCHPAINT_TEST_DIR")
local chunkFile = io.open(dir .. "/pattern_chunk.txt", "r")
local chunk = chunkFile:read("a")
chunkFile:close()

local function make(name, mode, range, preview)
  reaper.Main_OnCommand(40859, 0) -- new project tab
  reaper.SetCurrentBPM(0, 120, false)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true) -- match the render rate: no resampling
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
  reaper.InsertTrackAtIndex(0, true)
  local tr = reaper.GetTrack(0, 0)
  local pp = reaper.TrackFX_AddByName(tr, "VST3:PitchPaint", false, -1)
  reaper.TrackFX_Show(tr, pp, 2) -- close any auto-floated FX window
  local loaded = reaper.TrackFX_SetNamedConfigParm(tr, pp, "vst_chunk", chunk) -- the test pattern (check.py --make-chunk)
  reaper.TrackFX_SetParam(tr, pp, 0, mode)
  reaper.TrackFX_SetParam(tr, pp, 1, (range - 1) / 95)
  reaper.TrackFX_SetParam(tr, pp, 2, preview and 1 or 0)
  local channels, mon = 2, 0

  if not preview then
    channels = 8
    reaper.SetMediaTrackInfo_Value(tr, "I_NCHAN", 8)
    reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0), "I_NCHAN", 8) -- render reads the master
    mon = reaper.TrackFX_AddByName(tr, "JS:pp_midimon", false, -1)
    reaper.TrackFX_Show(tr, mon, 2)
    for pin = 0, 7 do reaper.TrackFX_SetPinMappings(tr, mon, 1, pin, 1 << pin, 0) end
  end

  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", dir, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", name, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "evaw", true)
  reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", 8, true) -- 16 beats: two passes of the 8-beat demo loop
  reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
  reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", channels, true)
  reaper.Main_SaveProjectEx(0, dir .. "/" .. name .. ".rpp", 0)
  reaper.Main_OnCommand(40860, 0) -- close project tab
  return pp >= 0 and mon >= 0 and loaded
end

local ok = make("mpe", 0, 48, false) and make("mono", 1, 12, false) and make("preview", 1, 12, true)
local f = io.open(dir .. "/make_projects.log", "w")
f:write(ok and "ok\n" or "failed to add PitchPaint or the monitor\n")
f:close()
