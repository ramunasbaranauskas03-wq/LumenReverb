"""Small DSL for writing Lumen Synth XL factory presets.

Every preset is a dict of plain parameter values plus modulation rows.
`emit_cpp` writes Source/FactoryPresets.cpp.
"""
import math, json, os

# ---------------------------------------------------------------- constants
TABLES = ["Basic Shapes", "Sine Harmonics", "Saw Sweep", "PWM Square", "Sync Saw", "Formant Vowels", "Digital Spectra", "FM Bell",
          "FM Growl", "Organ", "Odd Sweep", "Hollow", "Phase Saw", "Growl Vowel", "Fold Sine", "Tri Fold",
          "Bit Saw", "Spectral Comb", "Choir", "Neuro", "Pluck", "Metallic", "Super Square", "Noise Spectra",
          "Saw Stack", "Square Sweep", "Squeal", "Talk Box", "Dirt Saw", "Brass", "Strings", "Glass Bell",
          "Mallet", "Warm Spectra", "Fidget", "Future Bwah"]
T = {n: i for i, n in enumerate(TABLES)}
# positions inside "Basic Shapes"
SINE, TRI, SAW, SQUARE, PULSE = 0.0, 0.25, 0.5, 0.75, 1.0

MODES = dict(std=0, bendp=1, bendm=2, mirror=3, pwm=4, sync=5, formant=6, jump=7, quant=8, fold=9)
FILT = dict(lp12=0, lp24=1, alp=2, hp12=3, hp24=4, bp=5, notch=6, dnotch=7, combp=8, combm=9, formant=10)
ROUTE = dict(A=0, B=1, AB=2, D=3)
INS = dict(off=0, drive=1, tube=2, fold=3, rect=4, crush=5, down=6, clip=7)
LFO = dict(sine=0, tri=1, up=2, down=3, square=4, pump=5, stairs=6, smooth=7)
DIV = {"8bar": 0, "4bar": 1, "2bar": 2, "1bar": 3, "1/2": 4, "1/2T": 5, "1/4D": 6, "1/4": 7, "1/4T": 8, "1/8D": 9, "1/8": 10,
       "1/8T": 11, "1/16D": 12, "1/16": 13, "1/16T": 14, "1/32": 15, "1/64": 16}
NOISE = dict(white=0, pink=1, crackle=2, metal=3, vinyl=4)
DIST = dict(soft=0, hard=1, tube=2, fold=3)

# modulation sources
def M(n): return n                  # Mod 1..9
VEL, KEY, AT, WHEEL, BEND = 10, 11, 12, 13, 14
def MACRO(n): return 14 + n         # Macro 1..8
VRAND, AMPENV = 23, 24

# ---------------------------------------------------------------- normalisation helpers (match JUCE ranges)
def _skew_norm(v, lo, hi, centre):
    s = math.log(0.5) / math.log((centre - lo) / (hi - lo))
    return max(0.0, min(1.0, (v - lo) / (hi - lo))) ** s

def cut_norm(hz):
    return _skew_norm(hz, 20.0, 20000.0, 900.0)

def cut_amt(base_hz, to_hz):
    """Matrix amount that moves a filter cutoff from base_hz to to_hz."""
    return round(cut_norm(to_hz) - cut_norm(base_hz), 4)

def semis_amt(st, rng=96.0):
    """Amount for Voice Pitch (range -48..48)."""
    return round(st / rng, 4)

def osc_semi_amt(st):
    return round(st / 48.0, 4)


DESTS = set()
for _o in (1, 2):
    for _n in ("semi", "fine", "pos", "warp", "level", "pan", "detune", "fm", "ring"):
        DESTS.add(f"osc{_o}_{_n}")
for _f in (1, 2):
    for _n in ("cutoff", "reso", "drive", "mix"):
        DESTS.add(f"f{_f}_{_n}")
DESTS |= {"sub_level", "noise_level", "noise_color", "fb_amount", "ins1_amount", "ins2_amount", "amp_level", "amp_pan", "voice_pitch"}
GLOBAL_DESTS = {"fx_dist_drive", "fx_chorus_mix", "fx_phaser_mix", "fx_delay_mix", "fx_delay_fb", "fx_rev_mix", "fx_rev_size", "fx_tape_mix",
                "fx_hyper_mix", "fx_ott_depth", "fx_width", "fx_eq_high", "amp_a", "amp_d", "amp_r", "mod1_d", "mod2_d"}
GLOBAL_SRCS = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}


class Preset:
    def __init__(self, name, category, genre):
        self.name, self.category, self.genre = name, category, genre
        self.p = {}
        self.rows = []
        self.macros = []
        self.vol = None
        # predictable defaults for every preset
        self.p["osc1_on"] = 1
        self.p["osc2_on"] = 0
        self.p["f1_on"] = 0
        self.p["f2_on"] = 0
        self.p["voice_drift"] = 0.2

    # ------------------------------------------------ sources
    def osc(self, i, table, pos=0.0, level=0.75, mode="std", warp=0.0, oct=0, semi=0, fine=0.0, voices=1, detune=0.2,
            spread=0.7, blend=0.6, fm=0.0, ratio=1.0, ring=0.0, route="A", pan=0.0, phase=0.0, rndphase=1.0):
        k = f"osc{i}_"
        self.p.update({k + "on": 1, k + "table": T[table] if isinstance(table, str) else table, k + "pos": pos, k + "level": level,
                       k + "mode": MODES[mode], k + "warp": warp, k + "oct": oct, k + "semi": semi, k + "fine": fine,
                       k + "voices": voices, k + "detune": detune, k + "spread": spread, k + "blend": blend, k + "fm": fm,
                       k + "fmratio": ratio, k + "ring": ring, k + "route": ROUTE[route], k + "pan": pan, k + "phase": phase,
                       k + "rndphase": rndphase})
        return self

    def sub(self, level=0.5, shape=0, oct=0, route="D"):
        self.p.update(sub_on=1, sub_level=level, sub_shape=shape, sub_oct=oct, sub_route=ROUTE[route])
        return self

    def noise(self, level=0.2, kind="white", color=0.5, route="A"):
        self.p.update(noise_on=1, noise_level=level, noise_type=NOISE[kind], noise_color=color, noise_route=ROUTE[route])
        return self

    # ------------------------------------------------ filters / inserts
    def filt(self, i, kind="lp24", cutoff=8000.0, reso=0.1, drive=0.0, key=0.0, mix=1.0):
        k = f"f{i}_"
        self.p.update({k + "on": 1, k + "type": FILT[kind], k + "cutoff": cutoff, k + "reso": reso, k + "drive": drive,
                       k + "key": key, k + "mix": mix})
        return self

    def parallel(self):
        self.p["filter_struct"] = 1
        return self

    def feedback(self, amt):
        self.p["fb_amount"] = amt
        return self

    def insert(self, i, kind, amount=0.4, pos="pre", mix=1.0):
        k = f"ins{i}_"
        self.p.update({k + "type": INS[kind], k + "amount": amount, k + "pos": 0 if pos == "pre" else 1, k + "mix": mix})
        return self

    # ------------------------------------------------ amp / voice
    def amp(self, a=0.003, d=0.4, s=0.8, r=0.25, curve=0.35, level=0.8, vel=0.35):
        self.p.update(amp_a=a, amp_d=d, amp_s=s, amp_r=r, amp_curve=curve, amp_level=level, amp_vel=vel)
        return self

    def voice(self, mode="poly", glide=0.0, poly=16, bend=2, drift=None):
        self.p.update(voice_mode={"poly": 0, "mono": 1, "legato": 2}[mode], voice_glide=glide, voice_poly=poly, voice_pbrange=bend)
        if drift is not None:
            self.p["voice_drift"] = drift
        return self

    # ------------------------------------------------ modulators
    def env(self, slot, a=0.0, d=0.3, s=0.0, r=0.3, curve=0.3):
        k = f"mod{slot}_"
        self.p.update({k + "type": 0, k + "a": a, k + "d": d, k + "s": s, k + "r": r, k + "curve": curve})
        return self

    def lfo(self, slot, shape="sine", div=None, hz=None, retrig=False, bipolar=True, phase=0.0):
        k = f"mod{slot}_"
        self.p.update({k + "type": 1, k + "shape": LFO[shape], k + "retrig": int(retrig), k + "bipolar": int(bipolar), k + "phase": phase})
        if hz is not None:
            self.p.update({k + "sync": 0, k + "rate": hz})
        else:
            self.p.update({k + "sync": 1, k + "div": DIV[div or "1/4"]})
        return self

    def perf(self, slot, steps, div="1/16", smooth=0.05, bipolar=False, retrig=False):
        k = f"mod{slot}_"
        self.p.update({k + "type": 2, k + "sync": 1, k + "div": DIV[div], k + "smooth": smooth, k + "bipolar": int(bipolar),
                       k + "retrig": int(retrig), k + "len": len(steps)})
        for j, v in enumerate(steps):
            self.p[f"{k}step{j + 1}"] = round(max(0.0, min(1.0, v)), 4)
        return self

    def rnd(self, slot, div="1/8", hz=None, smooth=0.5, bipolar=True, retrig=False):
        k = f"mod{slot}_"
        self.p.update({k + "type": 3, k + "smooth": smooth, k + "bipolar": int(bipolar), k + "retrig": int(retrig)})
        if hz is not None:
            self.p.update({k + "sync": 0, k + "rate": hz})
        else:
            self.p.update({k + "sync": 1, k + "div": DIV[div]})
        return self

    def mod(self, src, dst, amt):
        if abs(amt) < 1e-4:
            return self
        assert dst in DESTS or dst in GLOBAL_DESTS, f"{self.name}: bad destination {dst}"
        if dst in GLOBAL_DESTS:
            assert src in GLOBAL_SRCS, f"{self.name}: {dst} can only be moved by macros / wheel"
        self.rows.append((src, dst, round(amt, 4)))
        return self

    def macro(self, n, name, targets, value=0.0):
        self.macros.append((n, name))
        self.p[f"macro{n}"] = value
        for dst, amt in targets:
            self.mod(MACRO(n), dst, amt)
        return self

    # ------------------------------------------------ FX
    def dist(self, drive=0.3, kind="soft", tone=0.5, mix=1.0):
        self.p.update(fx_dist_on=1, fx_dist_drive=drive, fx_dist_type=DIST[kind], fx_dist_tone=tone, fx_dist_mix=mix)
        return self

    def chorus(self, mix=0.35, rate=0.6, depth=0.5):
        self.p.update(fx_chorus_on=1, fx_chorus_mix=mix, fx_chorus_rate=rate, fx_chorus_depth=depth)
        return self

    def phaser(self, mix=0.5, rate=0.3, depth=0.6, fb=0.5):
        self.p.update(fx_phaser_on=1, fx_phaser_mix=mix, fx_phaser_rate=rate, fx_phaser_depth=depth, fx_phaser_fb=fb)
        return self

    def delay(self, mix=0.2, div="1/8D", fb=0.35, pp=True, tone=0.5):
        self.p.update(fx_delay_on=1, fx_delay_mix=mix, fx_delay_sync=1, fx_delay_div=DIV[div], fx_delay_fb=fb, fx_delay_pp=int(pp), fx_delay_tone=tone)
        return self

    def reverb(self, mix=0.22, size=0.6, decay=2.5, damp=0.45, pre=12.0, width=1.0):
        self.p.update(fx_rev_on=1, fx_rev_mix=mix, fx_rev_size=size, fx_rev_decay=decay, fx_rev_damp=damp, fx_rev_pre=pre, fx_rev_width=width)
        return self

    def tape(self, mix=1.0, wow=0.3, flutter=0.2, sat=0.3):
        self.p.update(fx_tape_on=1, fx_tape_mix=mix, fx_tape_wow=wow, fx_tape_flutter=flutter, fx_tape_sat=sat)
        return self

    def hyper(self, mix=0.4, rate=0.6, detune=0.35, dim=0.3):
        self.p.update(fx_hyper_on=1, fx_hyper_mix=mix, fx_hyper_rate=rate, fx_hyper_detune=detune, fx_hyper_dim=dim)
        return self

    def ott(self, depth=0.35, time=0.3, up=1.0, down=1.0, gain=0.0):
        self.p.update(fx_ott_on=1, fx_ott_depth=depth, fx_ott_time=time, fx_ott_up=up, fx_ott_down=down, fx_ott_gain=gain)
        return self

    def eq(self, hp=20.0, low=0.0, midf=1000.0, mid=0.0, high=0.0):
        self.p.update(fx_eq_on=1, fx_eq_hp=hp, fx_eq_low=low, fx_eq_midf=midf, fx_eq_mid=mid, fx_eq_high=high)
        return self

    def stereo(self, width=1.0, bassmono=20.0):
        self.p.update(fx_width=width, fx_bassmono=bassmono)
        return self

    # ------------------------------------------------ output
    def data(self):
        assert len(self.rows) <= 16, f"{self.name}: {len(self.rows)} matrix rows"
        parts = []
        for k, v in self.p.items():
            parts.append(f"{k}={_fmt(v)}")
        if self.vol is not None:
            parts.append(f"master_vol={_fmt(self.vol)}")
        for s, d, a in self.rows:
            parts.append(f"m={s}:{d}:{_fmt(a)}")
        return ";".join(parts)

    def macro_string(self):
        names = ["" for _ in range(8)]
        for n, nm in self.macros:
            names[n - 1] = nm
        while names and names[-1] == "":
            names.pop()
        return "|".join(names)


def _fmt(v):
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, int):
        return str(v)
    s = f"{v:.5f}".rstrip("0").rstrip(".")
    return s if s not in ("", "-0") else "0"


def _cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_cpp(presets, path):
    lines = ['// Generated by tools/gen_presets.py - do not edit by hand.', '#include "Presets.h"', '', 'namespace lxl', '{',
             'const FactoryPreset kFactoryPresets[] = {']
    for p in presets:
        lines.append(f"    {{ {_cstr(p.name)}, {_cstr(p.category)}, {_cstr(p.genre)}, {_cstr(p.macro_string())},")
        lines.append(f"      {_cstr(p.data())} }},")
    lines += ['};', f'const int kNumFactoryPresets = {len(presets)};', '} // namespace lxl', '']
    with open(path, "w") as f:
        f.write("\n".join(lines))
