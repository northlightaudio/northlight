#!/usr/bin/env python3
"""Measure single electric piano notes (docs/plans/electric-piano.md, section 4).

For each note it finds the fundamental (and how far it is from equal
temperament), the levels of harmonics 2 to 8 and of the bell (the tine's first
beam mode, near 6.3 times the fundamental) against the fundamental in the
attack, how long the fundamental takes to fall 60 dB while the key is held, and
how long it takes once the key is let go. Run it on Lumen's own render and on
reference recordings, then compare the two, so a change to the model is checked
in numbers before anyone listens.

Two kinds of input:

    # Lumen's test phrase (Northlight.exe --render-phrase northlight.lumen x.wav
    # writes x.wav and x.cues.txt):
    python scripts/analysis/ep-measure.py phrase lumen.wav --out lumen.csv

    # A folder of reference recordings, one note a file, listed in a CSV of
    # file,note,velocity[,release_seconds] (the key's release time in the file, if
    # it has one):
    python scripts/analysis/ep-measure.py files refs/notes.csv --out reference.csv

    # Lumen against the reference, note by note, or averaged by register and
    # soft or hard strike:
    python scripts/analysis/ep-measure.py compare reference.csv lumen.csv
    python scripts/analysis/ep-measure.py summary reference.csv lumen.csv

scripts/analysis/lumen-calibrate.ps1 renders Lumen at a reference listing's notes
and velocities and prints the summary.

Only measured numbers belong in the repository, never the recordings
themselves. Needs numpy.
"""

import argparse
import csv
import math
import pathlib
import struct
import sys

import numpy as np

HARMONICS = range(2, 9)
BELL_RATIO = 6.267
FIELDS = (
    ["source", "note", "velocity", "f0_hz", "cents"]
    + [f"h{k}_db" for k in HARMONICS]
    + ["bell_ratio", "bell_db", "t60_held_s", "t60_released_s", "level_db"]
)


def read_wav(path):
    """Mono float samples and the rate of a PCM (16, 24, 32-bit) or float WAV."""
    data = pathlib.Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt, frames = 12, None, None
    while pos + 8 <= len(data):
        tag, size = data[pos : pos + 4], struct.unpack("<I", data[pos + 4 : pos + 8])[0]
        body = data[pos + 8 : pos + 8 + size]
        if tag == b"fmt ":
            code, channels, rate = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
            if code == 0xFFFE and len(body) >= 26:  # WAVE_FORMAT_EXTENSIBLE: the real code
                code = struct.unpack("<H", body[24:26])[0]
            fmt = (code, channels, rate, bits)
        elif tag == b"data":
            frames = body
        pos += 8 + size + (size & 1)
    if fmt is None or frames is None:
        raise ValueError(f"{path}: no fmt or data chunk")
    code, channels, rate, bits = fmt
    if code == 3 and bits == 32:
        x = np.frombuffer(frames, dtype="<f4").astype(np.float64)
    elif code == 1 and bits == 16:
        x = np.frombuffer(frames, dtype="<i2") / 32768.0
    elif code == 1 and bits == 24:
        raw = np.frombuffer(frames[: len(frames) // 3 * 3], dtype=np.uint8).reshape(-1, 3)
        ints = raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16)
        ints = np.where(ints & 0x800000, ints - 0x1000000, ints)
        x = ints / 8388608.0
    elif code == 1 and bits == 32:
        x = np.frombuffer(frames, dtype="<i4") / 2147483648.0
    else:
        raise ValueError(f"{path}: unsupported format {code}, {bits}-bit")
    x = x[: len(x) // channels * channels].reshape(-1, channels).mean(axis=1)
    return x, rate


def bin_level(x, rate, start, length, hz):
    """A Hann-windowed DFT bin's magnitude at any frequency."""
    a = int(start * rate)
    n = int(length * rate)
    seg = x[a : a + n]
    if len(seg) < n or n < 16:
        return 0.0
    t = np.arange(n)
    w = 0.5 - 0.5 * np.cos(2 * math.pi * t / n)
    return abs(np.sum(seg * w * np.exp(-2j * math.pi * hz * (a + t) / rate))) * 2 / n


def refine_f0(x, rate, start, guess):
    """The fundamental near `guess`, from the phase a bin turns between two windows."""
    length, gap = 0.25, 0.05
    a = int(start * rate)
    n = int(length * rate)
    if a + int(gap * rate) + n > len(x):
        return guess
    t = np.arange(n)
    w = 0.5 - 0.5 * np.cos(2 * math.pi * t / n)

    def b(s):
        i = int(s * rate)
        return np.sum(x[i : i + n] * w * np.exp(-2j * math.pi * guess * (i + t) / rate))

    turn = np.angle(b(start + gap) / b(start))
    return guess + turn / (2 * math.pi * gap)


def t60(x, rate, start, end, hz):
    """How long the partial at `hz` takes to fall 60 dB, from a line through its
    level in 50 ms windows between `start` and `end` (seconds; inf if it does not fall)."""
    times, levels = [], []
    s = start
    while s + 0.05 <= end:
        lv = bin_level(x, rate, s, 0.05, hz)
        if lv > 1e-7:
            times.append(s)
            levels.append(20 * math.log10(lv))
        s += 0.025
    if len(times) < 3:
        return float("nan")
    slope = np.polyfit(times, levels, 1)[0]  # dB a second
    return 60.0 / -slope if slope < 0 else float("inf")


def measure(x, rate, source, note, velocity, start, release=None, end=None):
    """One note's row. `start`: the strike; `release`: when the key went up (if it did)."""
    f0 = refine_f0(x, rate, start + 0.1, 440.0 * 2 ** ((note - 69) / 12))
    attack = max(0.0, start)
    # The attack: 40 ms, or six cycles of a low note so its harmonics stay apart.
    window = max(0.04, 6.0 / f0)
    fund = bin_level(x, rate, attack, window, f0)
    row = {"source": source, "note": note, "velocity": velocity, "f0_hz": round(f0, 4)}
    row["cents"] = round(1200 * math.log2(f0 / (440.0 * 2 ** ((note - 69) / 12))), 2)
    for k in HARMONICS:
        level = bin_level(x, rate, attack, window, f0 * k)
        row[f"h{k}_db"] = round(20 * math.log10(max(level, 1e-12) / max(fund, 1e-12)), 1)
    # The bell: the strongest line between the 6th and 7th harmonics, a quarter of
    # the fundamental away from both, in a window of 20 cycles or more so neither
    # harmonic's skirt reaches into the search.
    best, best_ratio = 0.0, BELL_RATIO
    for ratio in np.linspace(6.25, 6.75, 21):
        level = bin_level(x, rate, attack, max(0.08, 20.0 / f0), f0 * ratio)
        if level > best:
            best, best_ratio = level, ratio
    row["bell_ratio"] = round(float(best_ratio), 3)
    row["bell_db"] = round(20 * math.log10(max(best, 1e-12) / max(fund, 1e-12)), 1)
    held_end = release if release is not None else (end if end is not None else start + 1.0)
    row["t60_held_s"] = round(t60(x, rate, start + 0.1, held_end, f0), 3)
    if release is not None:
        stop = end if end is not None else release + 0.4
        row["t60_released_s"] = round(t60(x, rate, release + 0.03, stop, f0), 3)
    else:
        row["t60_released_s"] = float("nan")
    row["level_db"] = round(20 * math.log10(max(fund, 1e-12)), 1)
    return row


def read_cues(path):
    cues = []
    for line in pathlib.Path(path).read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        seconds, kind, note, velocity, length, part = line.split()
        cues.append((float(seconds), kind, int(note), int(velocity), float(length), part))
    return cues


def phrase_rows(wav):
    x, rate = read_wav(wav)
    cues_path = pathlib.Path(wav).with_suffix(".cues.txt")
    singles = [c for c in read_cues(cues_path) if c[5] == "single" and c[1] == "note"]
    rows = []
    for i, (seconds, _, note, velocity, length, _) in enumerate(singles):
        nxt = singles[i + 1][0] if i + 1 < len(singles) else seconds + length + 1.0
        rows.append(measure(x, rate, pathlib.Path(wav).name, note, velocity, seconds, seconds + length, nxt))
    return rows


def file_rows(listing):
    rows = []
    base = pathlib.Path(listing).parent
    with open(listing, newline="") as f:
        for r in csv.reader(f):
            if not r or r[0].startswith("#") or r[0] == "file":
                continue
            x, rate = read_wav(base / r[0])
            onset = int(np.argmax(np.abs(x) > 0.05 * np.max(np.abs(x))))
            release = float(r[3]) if len(r) > 3 and r[3] else None
            rows.append(measure(x, rate, r[0], int(r[1]), int(r[2]), onset / rate, release, len(x) / rate))
    return rows


def write(rows, out):
    stream = open(out, "w", newline="") if out else sys.stdout
    w = csv.DictWriter(stream, fieldnames=FIELDS)
    w.writeheader()
    for r in rows:
        w.writerow(r)
    if out:
        stream.close()


def compare(reference, ours):
    def load(p):
        with open(p, newline="") as f:
            return {(int(r["note"]), int(r["velocity"])): r for r in csv.DictReader(f)}

    ref, mine = load(reference), load(ours)
    cols = ["cents", "h2_db", "h3_db", "bell_db", "t60_held_s", "t60_released_s"]
    print("note vel  " + "  ".join(f"{c:>14}" for c in cols))
    for key in sorted(set(ref) & set(mine)):
        cells = []
        for c in cols:
            a, b = float(ref[key][c]), float(mine[key][c])
            cells.append(f"{b - a:+14.2f}" if math.isfinite(a) and math.isfinite(b) else f"{'-':>14}")
        print(f"{key[0]:4d} {key[1]:3d}  " + "  ".join(cells))
    print("(Lumen minus reference: dB, cents and seconds)")


def summary(reference, ours):
    """Lumen minus the reference, averaged by register and by soft (<= 60) or hard
    strike: harmonics 2 to 4 and the bell in dB, the held ring time as a ratio
    (geometric mean), and Lumen's own fundamental level."""

    def load(p):
        with open(p, newline="") as f:
            return {(int(r["note"]), int(r["velocity"])): r for r in csv.DictReader(f)}

    def register(n):
        return "low 29-45" if n <= 45 else "mid 50-65" if n <= 65 else "high 71-81" if n <= 81 else "top 86-96"

    ref, mine = load(reference), load(ours)
    groups = {}
    for key in sorted(set(ref) & set(mine)):
        a, b = ref[key], mine[key]
        g = groups.setdefault((register(key[0]), "soft" if key[1] <= 60 else "hard"), {})
        for c in ("h2_db", "h3_db", "h4_db", "bell_db"):
            g.setdefault(c, []).append(float(b[c]) - float(a[c]))
        ta, tb = float(a["t60_held_s"]), float(b["t60_held_s"])
        if math.isfinite(ta) and math.isfinite(tb) and ta > 0 and tb > 0:
            g.setdefault("t60", []).append(math.log(tb / ta))
        g.setdefault("ref_h2", []).append(float(a["h2_db"]))
        g.setdefault("level", []).append(float(b["level_db"]))

    def mean(g, k):
        return sum(g[k]) / len(g[k]) if g.get(k) else float("nan")

    print(f"{'register':11} {'strike':6} {'ref h2':>7} {'d h2':>6} {'d h3':>6} {'d h4':>6} {'d bell':>7} {'ring x':>7} {'level':>6}")
    for (r, v), g in sorted(groups.items()):
        ring = math.exp(mean(g, "t60")) if g.get("t60") else float("nan")
        print(
            f"{r:11} {v:6} {mean(g, 'ref_h2'):7.1f} {mean(g, 'h2_db'):6.1f} {mean(g, 'h3_db'):6.1f} "
            f"{mean(g, 'h4_db'):6.1f} {mean(g, 'bell_db'):7.1f} {ring:7.2f} {mean(g, 'level'):6.1f}"
        )
    print("(Lumen minus reference in dB; ring x: Lumen's ring time over the reference's)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("phrase", help="a render of the test phrase, with its .cues.txt beside it")
    p.add_argument("wav")
    p.add_argument("--out")
    f = sub.add_parser("files", help="a CSV of file,note,velocity[,release_seconds]")
    f.add_argument("listing")
    f.add_argument("--out")
    c = sub.add_parser("compare", help="two measurement CSVs, reference first")
    c.add_argument("reference")
    c.add_argument("ours")
    m = sub.add_parser("summary", help="two measurement CSVs, reference first, averaged by register")
    m.add_argument("reference")
    m.add_argument("ours")
    args = parser.parse_args()
    if args.command == "phrase":
        write(phrase_rows(args.wav), args.out)
    elif args.command == "files":
        write(file_rows(args.listing), args.out)
    elif args.command == "summary":
        summary(args.reference, args.ours)
    else:
        compare(args.reference, args.ours)


if __name__ == "__main__":
    main()
