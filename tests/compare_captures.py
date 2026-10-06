#!/usr/bin/env python3
"""Compares MnmCapture renders against the 44.1 kHz references (tests/audio-reference).

    python3 tests/compare_captures.py --ref tests/audio-reference --test <capture dir> [--json report.json]

Per render in the test capture's manifest.json:
  44.1 kHz     the samples must be bit-identical to the reference (SHA-256 of the samples).
  other rates  the render is shifted by the latency the plugin reports for its sample-rate conversion
               ("alignSamples" in the manifest; estimated by cross-correlation when absent), converted back to
               44.1 kHz offline with a far longer filter than the plugin's (scipy, about 150 dB), and compared with
               the reference in 20 Hz-18 kHz:
    residual   energy of the difference, relative to the reference (dB). "floor" is the same comparison for the
               reference itself, sent through the offline converter to that rate and back: what this measurement
               can resolve.
    bands      largest level difference of the third-octave bands 31.5 Hz-16 kHz (dB)
    pitch      largest frequency difference of the strong, stable spectral peaks (cents)
    lag        delay left after alignment, from the cross-correlation peak (44.1 kHz samples)
    local      the residual in its worst 10 ms window, relative to the reference there (windows more than 30 dB
               below the reference's average are judged against that level): a click or glitch that the
               whole-signal residual would average away shows up here
    blocks     the capture's own check: the same render with host blocks of other sizes (32, 441, 2048, random
               1-1024) must be bit-identical, i.e. nothing depends on where the host cuts its blocks (1.0.0 lost
               samples at every block boundary)
Pass limits are the LIMITS below. Exit code 0 when every render passes. Requires numpy and scipy.
"""
import argparse
import hashlib
import json
import math
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import firwin, kaiserord, resample_poly

TICK_FRAMES, PRE_ROLL_TICKS = 2352, 16      # the capture grid (src/plugin/Capture.cpp)
BAND = (20.0, 18000.0)
LIMITS = {"residual_db": -80.0, "local_db": -60.0, "bands_db": 0.05, "pitch_cents": 0.1, "lag": 0.05}
TRIM = 4096                                  # 44.1 kHz samples left out of the comparison at both ends
EDGE = 1024                                  # samples at the end the offline converter cannot render exactly


def load(path):
    rate, x = wavfile.read(path)
    x = np.asarray(x, dtype=np.float64)
    return rate, x.reshape(len(x), -1)


def samples_hash(x):
    return hashlib.sha256(np.ascontiguousarray(x, dtype=np.float32).tobytes()).hexdigest()


_filters = {}


def convert(x, fs_in, fs_out):
    """Band-limited rate conversion, flat to 20 kHz, ~150 dB rejection above 22.05 kHz (polyphase, zero delay)."""
    if fs_in == fs_out:
        return x.copy()
    g = math.gcd(int(fs_in), int(fs_out))
    up, down = int(fs_out) // g, int(fs_in) // g
    key = (up, down, int(fs_in))
    if key not in _filters:
        fs_mid = fs_in * up
        lo = min(fs_in, fs_out) / 2.0                      # the lower Nyquist
        width = lo - 0.907 * lo                           # 20 kHz -> 22.05 kHz at 44.1 kHz
        numtaps, beta = kaiserord(150.0, width / (fs_mid / 2.0))
        numtaps |= 1                                      # odd: resample_poly then keeps the timing exact
        _filters[key] = firwin(numtaps, lo - width / 2.0, window=("kaiser", beta), fs=fs_mid)
    return resample_poly(x, up, down, axis=0, window=_filters[key])


def edge_fade(n, length=2048):
    """1 in the middle, raised-cosine ramps over the first and last `length` samples (column vector)."""
    w = np.ones(n)
    ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(length) / length)
    w[:length] = ramp
    w[n - length:] = ramp[::-1]
    return w[:, None]


def bandlimit(x, rate):
    spec = np.fft.rfft(x, axis=0)
    f = np.fft.rfftfreq(len(x), 1.0 / rate)
    spec[(f < BAND[0]) | (f > BAND[1])] = 0
    return np.fft.irfft(spec, len(x), axis=0)


def residual_db(test, ref):
    e = np.sum((test - ref) ** 2)
    s = np.sum(ref ** 2)
    return -999.0 if e == 0 else 10 * math.log10(e / max(s, 1e-30))


def lag_of(test, ref, search=64):
    a, b = test.sum(axis=1), ref.sum(axis=1)
    n = 1 << int(math.ceil(math.log2(len(a) + len(b))))
    cc = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
    cc = np.concatenate([cc[-search:], cc[:search + 1]])
    k = int(np.argmax(cc))
    if 0 < k < len(cc) - 1:
        y0, y1, y2 = cc[k - 1], cc[k], cc[k + 1]
        d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0.0
    else:
        d = 0.0
    return k - search + d


def band_diff(test, ref, rate):
    centers = [31.5 * 2 ** (i / 3) for i in range(0, 28)]
    st = np.abs(np.fft.rfft(test.sum(axis=1))) ** 2
    sr = np.abs(np.fft.rfft(ref.sum(axis=1))) ** 2
    f = np.fft.rfftfreq(len(test), 1.0 / rate)
    total = sr.sum()
    worst = 0.0
    for c in centers:
        if c > 16000:
            break
        m = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        er, et = sr[m].sum(), st[m].sum()
        if er > total * 1e-8:                      # bands that hold signal (within 80 dB of the total)
            worst = max(worst, abs(10 * math.log10(max(et, 1e-30) / er)))
    return worst


def pitch_diff(test, ref, rate):
    """Largest frequency difference (cents) of prominent peaks in 8192-sample windows; None when none qualifies."""
    a, b = test.sum(axis=1), ref.sum(axis=1)
    win, hop, n = 8192, 4096, 1 << 16
    w = np.hanning(win)
    worst = None
    for i in range(0, len(a) - win, hop):
        sa = np.abs(np.fft.rfft(a[i:i + win] * w, n))
        sb = np.abs(np.fft.rfft(b[i:i + win] * w, n))
        kb = int(np.argmax(sb[1:])) + 1
        if sb[kb] < 1e-4 or sb[kb] < 1000 * np.median(sb) or kb >= len(sb) - 1:
            continue                                   # no single strong tone in this window
        ka = kb - 3 + int(np.argmax(sa[kb - 3:kb + 4]))

        def peak(s, k):
            y0, y1, y2 = np.log(s[k - 1:k + 2] + 1e-30)
            return (k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)) * rate / n

        cents = abs(1200 * math.log2(peak(sa, ka) / peak(sb, kb)))
        worst = cents if worst is None else max(worst, cents)
    return worst


def worst_window_db(test, ref, rate, seconds=0.01):
    d = ((test - ref) ** 2).sum(axis=1)
    e = (ref ** 2).sum(axis=1)
    w = max(1, int(rate * seconds))
    n = len(d) // w
    dw, ew = d[:n * w].reshape(n, w).sum(axis=1), e[:n * w].reshape(n, w).sum(axis=1)
    r = dw / np.maximum(ew, ew.mean() * 1e-3)
    worst = float(r.max()) if n else 0.0
    return -999.0 if worst <= 0 else 10 * math.log10(worst)


def compare(ref_dir, test_dir, only):
    ref_manifest = json.load(open(os.path.join(ref_dir, "manifest.json")))
    refs = {r["scenario"]: r for r in ref_manifest["renders"] if round(r["rate"]) == 44100}
    test_manifest = json.load(open(os.path.join(test_dir, "manifest.json")))
    results = []
    for r in test_manifest["renders"]:
        name, rate = r["scenario"], int(round(r["rate"]))
        if only and name not in only:
            continue
        if name not in refs:
            results.append({"scenario": name, "rate": rate, "pass": False, "error": "no reference"})
            continue
        ref_rate, ref = load(os.path.join(ref_dir, refs[name]["file"]))
        rate_t, test = load(os.path.join(test_dir, r["file"]))
        res = {"scenario": name, "rate": rate, "plugin": r.get("plugin", "")}
        if rate == 44100:
            res["identical"] = samples_hash(test) == refs[name]["sha256"]
            res["pass"] = res["identical"]
            results.append(res)
            continue
        if "alignSamples" in r:
            shift = int(r["alignSamples"])
        else:                                          # no reported latency: estimate it
            ref_up = convert(ref, 44100, rate)
            n = min(len(ref_up), len(test))
            shift = max(0, int(round(lag_of(test[:n], ref_up[:n], search=512))))
            res["alignEstimated"] = True
        res["align"] = shift
        test44 = convert(test[shift:], rate, 44100)
        floor44 = convert(convert(ref, 44100, rate), rate, 44100)
        # all three cut where the shortest is still free of the offline converter's end effects, faded in and out
        # identically, band-limited over that length and only then trimmed: neither the converter's edges nor the
        # band edge's ringing reach the samples compared
        n = min(len(test44), len(ref), len(floor44)) - EDGE
        fade = edge_fade(n)
        keep = slice(TRIM, n - TRIM)
        t = bandlimit(test44[:n] * fade, 44100)[keep]
        f = bandlimit(ref[:n] * fade, 44100)[keep]
        res["residual_db"] = residual_db(t, f)
        res["floor_db"] = residual_db(bandlimit(floor44[:n] * fade, 44100)[keep], f)
        res["bands_db"] = band_diff(t, f, 44100)
        p = pitch_diff(t, f, 44100)
        res["pitch_cents"] = p
        res["lag"] = lag_of(t, f, search=16)
        res["local_db"] = worst_window_db(t, f, 44100)
        others = r.get("otherBlocks", [])
        res["blockSizes"] = "identical" if others and all(o["identical"] for o in others) else ("-" if not others else "DIFFER")
        res["pass"] = (res["residual_db"] <= LIMITS["residual_db"] and res["local_db"] <= LIMITS["local_db"]
                       and res["bands_db"] <= LIMITS["bands_db"] and (p is None or p <= LIMITS["pitch_cents"])
                       and abs(res["lag"]) <= LIMITS["lag"] and res["blockSizes"] != "DIFFER" and r.get("deterministic", True))
        results.append(res)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref", required=True)
    ap.add_argument("--test", required=True)
    ap.add_argument("--scenario", default="")
    ap.add_argument("--json", default="")
    a = ap.parse_args()
    only = set(s for s in a.scenario.split(",") if s)
    results = compare(a.ref, a.test, only)
    print(f"{'scenario':22s} {'rate':>6s} {'residual':>9s} {'floor':>7s} {'local':>7s} {'bands':>6s} {'pitch':>6s} {'lag':>6s}  blocks     result")
    for r in results:
        if "error" in r:
            print(f"{r['scenario']:22s} {r['rate']:6d}  {r['error']}")
        elif r["rate"] == 44100:
            print(f"{r['scenario']:22s} {r['rate']:6d}  {'bit-identical' if r['identical'] else 'NOT bit-identical':>48s}            {'ok' if r['pass'] else 'FAIL'}")
        else:
            p = "-" if r["pitch_cents"] is None else f"{r['pitch_cents']:.3f}"
            print(f"{r['scenario']:22s} {r['rate']:6d} {r['residual_db']:8.1f}  {r['floor_db']:6.1f} {r['local_db']:7.1f} {r['bands_db']:6.3f} {p:>6s} {r['lag']:6.3f}  "
                  f"{r['blockSizes']:10s} {'ok' if r['pass'] else 'FAIL'}")
    failed = [r for r in results if not r["pass"]]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed  (limits: residual <= {LIMITS['residual_db']} dB, local <= {LIMITS['local_db']} dB, "
          f"bands <= {LIMITS['bands_db']} dB, pitch <= {LIMITS['pitch_cents']} cent, |lag| <= {LIMITS['lag']}, identical at every block size)")
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"limits": LIMITS, "results": results}, f, indent=1)
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
