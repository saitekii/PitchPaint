"""Checks the MIDI-monitor renders from run.ps1 against PitchPaint's demo pattern. Standard library only."""
import math
import struct
import sys
import wave

TOLERANCE = 0.15  # semitones; the monitor only updates once per host block
GUARD = 0.05      # beats skipped around note starts/ends, where the block-quantised monitor lags
BOUNDARIES = (0.0, 2.0, 6.0, 7.5, 8.0)


def curves(local):
    """(start beat, pitch) of each demo trajectory sounding at `local` (see makeDemoPattern in PluginProcessor.cpp)."""
    out = []
    if 0.0 <= local < 7.5:
        out.append((0.0, 60 + 7 * math.sin(2 * math.pi * local / 4)))
    if 2.0 <= local < 6.0:
        out.append((2.0, 72 + 5 * math.sin(2 * math.pi * (local - 2) / 4)))
    return out


def frames(path, every):
    with wave.open(path) as w:
        sr, n, ch = w.getframerate(), w.getnframes(), w.getnchannels()
        assert w.getsampwidth() == 3 and ch == 8, "expected 8-channel 24-bit WAV"
        raw = w.readframes(n)
    size = ch * 3
    for i in list(range(0, n, every)) + [n - 1]:
        vals = []
        for c in range(ch):
            b = raw[i * size + c * 3: i * size + c * 3 + 3]
            vals.append(struct.unpack("<i", b + (b"\xff" if b[2] & 0x80 else b"\x00"))[0] / float(1 << 23))
        yield i / sr, vals


def check(path, mode, expected_range, beats_per_second=2.0):
    failures = []
    max_err, compared, last = 0.0, 0, None
    for t, v in frames(path, 240):
        last = v
        beat = t * beats_per_second
        local = beat % 8.0
        if any(abs(local - b) < GUARD for b in BOUNDARIES):
            continue
        active = curves(local)
        if mode == "mono" and active:
            active = [max(active)]  # latest start wins
        want = sorted(p for _, p in active)
        got = sorted(x * 128 for x in v[:5] if x != 0.0)
        if len(got) != len(want):
            failures.append(f"beat {beat:.2f}: {len(got)} notes sounding, expected {len(want)}")
            continue
        for g, w in zip(got, want):
            max_err = max(max_err, abs(g - w))
        compared += 1

    members, rng, events = round(last[5] * 128), round(last[6] * 128), round(last[7] * 1e6)
    if mode == "mpe" and members != 15:
        failures.append(f"MPE configuration message: {members} member channels, expected 15")
    if rng != expected_range:
        failures.append(f"bend range RPN: {rng}, expected {expected_range}")
    if max_err > TOLERANCE:
        failures.append(f"max pitch error {max_err:.3f} st > {TOLERANCE}")

    print(f"{mode:5s}: {events} MIDI events, {compared} points compared, max pitch error {max_err:.3f} st"
          f" -> {'PASS' if not failures else 'FAIL'}")
    for f in failures[:10]:
        print("   ", f)
    return not failures


def check_preview(path, beats_per_second=2.0):
    """PitchPaint's preview sound in mono mode: the measured frequency must follow the drawn curve (60 = C4 = 261.63 Hz)."""
    with wave.open(path) as w:
        sr, n, ch = w.getframerate(), w.getnframes(), w.getnchannels()
        raw = w.readframes(n)
    size = ch * 3
    x = []
    for i in range(n):
        b = raw[i * size: i * size + 3]
        x.append(struct.unpack("<i", b + (b"\xff" if b[2] & 0x80 else b"\x00"))[0] / float(1 << 23))

    failures, max_err, compared = [], 0.0, 0
    win = int(0.04 * sr)
    for k in range(1, 64):
        beat = k * 0.25
        local = beat % 8.0
        # Skip note changes and the following 0.3 beats, where the previous note's release tail still sounds.
        if any(-0.1 < local - b < 0.3 for b in BOUNDARIES):
            continue
        active = curves(local)
        if not active:
            continue
        c = int(beat / beats_per_second * sr)
        seg = x[c - win // 2: c + win // 2]
        crossings = [i - 1 + seg[i - 1] / (seg[i - 1] - seg[i]) for i in range(1, len(seg)) if seg[i - 1] < 0 <= seg[i]]
        if len(crossings) < 3:
            failures.append(f"beat {beat:.2f}: silent, expected a note")
            continue
        hz = (len(crossings) - 1) * sr / (crossings[-1] - crossings[0])
        measured = 69 + 12 * math.log2(hz / 440.0)
        expected = max(active)[1]  # mono: latest start wins
        max_err = max(max_err, abs(measured - expected))
        compared += 1

    if max_err > 0.1:
        failures.append(f"max pitch error {max_err:.3f} st > 0.1")
    print(f"audio: preview synth, {compared} points compared, max pitch error {max_err:.3f} st"
          f" -> {'PASS' if not failures else 'FAIL'}")
    for f in failures[:10]:
        print("   ", f)
    return not failures


if __name__ == "__main__":
    d = sys.argv[1]
    ok = check(f"{d}/mpe.wav", "mpe", 48)
    ok &= check(f"{d}/mono.wav", "mono", 12)
    ok &= check_preview(f"{d}/preview.wav")
    sys.exit(0 if ok else 1)
