#!/usr/bin/env python3
"""Generates the Lumen Synth XL factory library: 8 categories x 50 presets.

Usage:  python3 tools/gen_presets.py [gains.json]
gains.json (optional) maps preset name -> master volume in dB (from the loudness pass).
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
from presetlib import *

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALL = []

def P(name, cat, genre):
    p = Preset(name, cat, genre)
    ALL.append(p)
    return p

# ---------------------------------------------------------------- shared macro recipes
def mac_filter(p, n=1, base=None, to=None, f="f1_cutoff", name="Cutoff"):
    base = base or p.p.get("f1_cutoff", 2000.0)
    to = to or min(18000.0, base * 6.0)
    p.macro(n, name, [(f, cut_amt(base, to))])

def mac_space(p, n=3, rev=0.35, dly=0.25, name="Space"):
    t = []
    if p.p.get("fx_rev_on"): t.append(("fx_rev_mix", rev))
    if p.p.get("fx_delay_on"): t.append(("fx_delay_mix", dly))
    if t: p.macro(n, name, t)

def mac_drive(p, n=2, name="Drive", ins=None, dist=0.45):
    t = []
    if p.p.get("fx_dist_on"): t.append(("fx_dist_drive", dist))
    if ins: t.append((f"ins{ins}_amount", 0.5))
    if t: p.macro(n, name, t)

def bass_fx(p, ott=0.35, mono=140.0):
    p.ott(depth=ott, time=0.25)
    p.stereo(width=1.0, bassmono=mono)
    return p

def lead_space(p, rev=0.22, dly=0.18, div="1/8D", size=0.65, decay=2.8):
    p.delay(mix=dly, div=div, fb=0.38, tone=0.55)
    p.reverb(mix=rev, size=size, decay=decay)
    return p

# =====================================================================================================
# BASS
# =====================================================================================================
def bass():
    C = "Bass"
    # 1. subs ------------------------------------------------------------------------------------------
    subs = [
        ("Deep Sub", "Deep House", 0.0, 0.0, 0.0, 0.0, 0.05),
        ("Warm Triangle Sub", "House", 0.25, 0.25, 0.0, 0.0, 0.25),
        ("808 Glide", "Future Bass", 0.0, 0.0, -12.0, 0.25, 0.3),
        ("Rumble Sub", "Melodic Techno", 0.0, 0.1, 0.0, 0.0, 0.45),
        ("Sub Click", "Tech House", 0.08, 0.0, 24.0, 0.0, 0.2),
    ]
    for name, g, pos, sat, pitch_env, glide, drive in subs:
        p = P(name, C, g)
        p.osc(1, "Basic Shapes", pos=pos, level=0.9)
        p.filt(1, "lp12", cutoff=900.0 if name != "Sub Click" else 2500.0, reso=0.05)
        p.amp(a=0.002, d=2.5 if "808" in name else 0.6, s=0.0 if "808" in name or name == "Rumble Sub" else 1.0,
              r=0.12, level=0.85, vel=0.15)
        if name == "Rumble Sub":
            p.amp(a=0.001, d=0.32, s=0.0, r=0.08, level=0.85, vel=0.2)
            p.reverb(mix=0.12, size=0.5, decay=1.6, damp=0.8)
        p.insert(1, "tube", amount=drive, pos="post")
        if pitch_env:
            p.env(1, d=0.04 if pitch_env > 0 else 0.35, curve=0.15)
            p.mod(M(1), "voice_pitch", semis_amt(pitch_env if pitch_env > 0 else 0)) if pitch_env > 0 else None
            if pitch_env < 0:
                p.env(1, a=0.0, d=1.2, s=0.0, curve=0.5)
                p.mod(M(1), "voice_pitch", semis_amt(5))
        p.voice("legato" if glide or "Sub" in name else "mono", glide=glide or 0.02)
        p.stereo(bassmono=200.0)
        p.macro(1, "Drive", [("ins1_amount", 0.5)])
        p.macro(2, "Tone", [("f1_cutoff", cut_amt(900.0, 3500.0))])
        p.macro(3, "Decay", [("amp_d", 0.25)]) if "808" in name or name == "Rumble Sub" else None
        p.p["voice_drift"] = 0.05

    # 2. acid ------------------------------------------------------------------------------------------
    acids = [
        ("Acid Line 303", "Techno", 0.5, 0.72, 0.25, 0.2),
        ("Acid Square", "Techno", 0.75, 0.68, 0.2, 0.15),
        ("Squelch Acid", "Tech House", 0.5, 0.86, 0.5, 0.35),
        ("Rubber Acid", "Melodic Techno", 0.62, 0.6, 0.15, 0.1),
        ("Acid Burner", "Techno", 0.5, 0.8, 0.7, 0.5),
    ]
    for name, g, pos, reso, drive, dist in acids:
        p = P(name, C, g)
        p.osc(1, "Basic Shapes", pos=pos, level=0.8)
        p.filt(1, "lp24", cutoff=320.0, reso=reso, drive=drive)
        p.env(1, a=0.0, d=0.22, s=0.0, r=0.1, curve=0.25)
        p.mod(M(1), "f1_cutoff", cut_amt(320.0, 3800.0))
        p.mod(VEL, "f1_cutoff", 0.12)
        p.amp(a=0.001, d=0.4, s=0.7, r=0.08, level=0.8, vel=0.4)
        p.voice("legato", glide=0.06, drift=0.1)
        p.dist(drive=dist, kind="tube", tone=0.55)
        p.delay(mix=0.1, div="1/8D", fb=0.3)
        p.stereo(bassmono=120.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(320.0, 2600.0))])
        p.macro(2, "Resonance", [("f1_reso", 0.15)])
        p.macro(3, "Env Mod", [("mod1_d", 0.2)])
        p.macro(4, "Distortion", [("fx_dist_drive", 0.4)])

    # 3. rolling melodic / techno bass ----------------------------------------------------------------
    rollers = [
        ("Rolling Saw", "Melodic Techno", "Saw Sweep", 1.0, "lp24", 420.0, 0.2),
        ("Pulse Roller", "Melodic Techno", "PWM Square", 0.35, "lp24", 480.0, 0.15),
        ("Dark Roller", "Techno", "Dirt Saw", 0.4, "lp24", 300.0, 0.3),
        ("Analog Roller", "Melodic Techno", "Basic Shapes", 0.55, "alp", 380.0, 0.25),
        ("Hypnotic Roller", "Techno", "Saw Sweep", 0.9, "lp24", 360.0, 0.35),
    ]
    for i, (name, g, tab, pos, ft, cut, reso) in enumerate(rollers):
        p = P(name, C, g)
        p.osc(1, tab, pos=pos, level=0.8, voices=2 if i % 2 == 0 else 1, detune=0.08, spread=0.2)
        p.sub(level=0.45)
        p.filt(1, ft, cutoff=cut, reso=reso, drive=0.15)
        p.env(1, d=0.16, s=0.0, curve=0.2)
        p.mod(M(1), "f1_cutoff", cut_amt(cut, cut * 7))
        p.amp(a=0.001, d=0.22, s=0.0, r=0.1, level=0.85, vel=0.3)
        if name == "Hypnotic Roller":
            p.perf(5, [0.9, 0.2, 0.5, 0.1, 0.7, 0.3, 1.0, 0.15], div="1/16", smooth=0.05)
            p.mod(M(5), "f1_cutoff", 0.15)
        p.voice("poly", drift=0.3)
        p.reverb(mix=0.08, size=0.4, decay=1.2, damp=0.7)
        bass_fx(p, ott=0.2, mono=150.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(cut, cut * 5))])
        p.macro(2, "Decay", [("amp_d", 0.25), ("mod1_d", 0.2)])
        p.macro(3, "Sub", [("sub_level", 0.4)])
        p.macro(4, "Resonance", [("f1_reso", 0.4)])

    # 4. electro house dirty -------------------------------------------------------------------------
    electro = [
        ("Fidget Bass", "Electro House", "Fidget", 0.3, "std"),
        ("Squeal Bass", "Electro House", "Squeal", 0.2, "bendp"),
        ("Dirty Dutch", "Electro House", "Dirt Saw", 0.6, "std"),
        ("Complextro Chop", "Electro House", "Squeal", 0.0, "std"),
        ("Electro Buzz", "Electro House", "Square Sweep", 0.9, "pwm"),
    ]
    for i, (name, g, tab, pos, mode) in enumerate(electro):
        p = P(name, C, g)
        p.osc(1, tab, pos=pos, level=0.8, mode=mode, warp=0.2 if mode != "std" else 0.0, voices=2, detune=0.1, spread=0.4)
        p.osc(2, "Basic Shapes", pos=SAW, level=0.45, oct=-1, voices=1)
        p.sub(level=0.45)
        p.filt(1, "lp24", cutoff=1400.0, reso=0.35, drive=0.35)
        p.env(1, d=0.3, s=0.25, curve=0.25)
        p.mod(M(1), "f1_cutoff", cut_amt(1400.0, 6000.0))
        if name == "Complextro Chop":
            p.perf(5, [0.0, 0.6, 0.2, 1.0, 0.4, 0.8, 0.1, 0.9], div="1/16", smooth=0.02)
            p.mod(M(5), "osc1_pos", 0.9)
            p.perf(6, [1, 1, 0, 1, 1, 0, 1, 0.6], div="1/16", smooth=0.02)
            p.mod(M(6), "f1_cutoff", 0.25)
        elif name == "Squeal Bass":
            p.lfo(3, "tri", div="1/8", retrig=True, bipolar=False)
            p.mod(M(3), "osc1_pos", 0.7)
        else:
            p.lfo(3, "sine", div="1/4", retrig=True, bipolar=False)
            p.mod(M(3), "osc1_pos", 0.35)
        p.insert(1, "drive", amount=0.45, pos="post")
        p.amp(a=0.002, d=0.5, s=0.9, r=0.1, level=0.8)
        p.voice("legato", glide=0.04)
        p.dist(drive=0.35, kind="hard", tone=0.6, mix=0.8)
        bass_fx(p, ott=0.5, mono=120.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(1400.0, 9000.0))])
        p.macro(2, "Grit", [("fx_dist_drive", 0.45), ("ins1_amount", 0.4)])
        p.macro(3, "Wavetable", [("osc1_pos", 0.6)])
        p.macro(4, "OTT", [("fx_ott_depth", 0.5)])

    # 5. future house / bass house --------------------------------------------------------------------
    fh = [
        ("Future Bwah", "Future House", "Future Bwah", "lp12"),
        ("Metal Bass", "Future House", "Metallic", "combp"),
        ("Wobbly Future", "Future House", "Future Bwah", "lp24"),
        ("Ding Bass", "Future House", "FM Bell", "lp12"),
        ("Organ Bass House", "Bass House", "Organ", "lp24"),
    ]
    for i, (name, g, tab, ft) in enumerate(fh):
        p = P(name, C, g)
        p.osc(1, tab, pos=0.15, level=0.85, voices=2, detune=0.08, spread=0.3)
        p.sub(level=0.5)
        p.env(1, d=0.35, s=0.15, curve=0.25)
        p.mod(M(1), "osc1_pos", 0.7)
        p.env(2, d=0.05, s=0.0, curve=0.1)
        p.mod(M(2), "voice_pitch", semis_amt(7))
        if ft == "combp":
            p.filt(1, "combp", cutoff=220.0, reso=0.55, key=1.0)
            p.filt(2, "lp12", cutoff=5000.0, reso=0.1)
        else:
            p.filt(1, ft, cutoff=3200.0, reso=0.2, drive=0.25)
            p.mod(M(1), "f1_cutoff", cut_amt(3200.0, 9000.0))
        if name == "Wobbly Future":
            p.lfo(3, "sine", div="1/8", retrig=True, bipolar=False)
            p.mod(M(3), "osc1_pos", 0.5)
        if name == "Organ Bass House":
            p.osc(2, "Basic Shapes", pos=SQUARE, level=0.4, oct=-1)
            p.filt(1, "lp24", cutoff=900.0, reso=0.3, drive=0.3)
            p.mod(M(1), "f1_cutoff", cut_amt(900.0, 5500.0))
        p.insert(1, "drive", amount=0.35, pos="post")
        p.amp(a=0.001, d=0.45, s=0.7, r=0.1, level=0.8)
        p.voice("legato", glide=0.03)
        p.dist(drive=0.25, kind="soft", tone=0.6)
        bass_fx(p, ott=0.55, mono=130.0)
        p.macro(1, "Bwah", [("mod1_d", 0.25)])
        p.macro(2, "Grit", [("fx_dist_drive", 0.45)])
        p.macro(3, "Pitch Drop", [("mod2_d", 0.3)])
        p.macro(4, "OTT", [("fx_ott_depth", 0.45)])

    # 6. reese -----------------------------------------------------------------------------------------
    reeses = [
        ("Classic Reese", "Dubstep", 0.3, 0.25, 1500.0),
        ("Dark Reese", "Techno", 0.6, 0.35, 700.0),
        ("Moving Reese", "Bass House", 0.2, 0.3, 1800.0),
        ("Neuro Reese", "Dubstep", 0.5, 0.45, 2200.0),
        ("Melodic Reese", "Melodic Techno", 0.15, 0.2, 1200.0),
    ]
    for i, (name, g, pos, det, cut) in enumerate(reeses):
        p = P(name, C, g)
        p.osc(1, "Phase Saw" if i != 3 else "Neuro", pos=pos, level=0.75, voices=4, detune=det, spread=0.4)
        p.osc(2, "Saw Sweep", pos=1.0, level=0.5, voices=3, detune=det * 1.2, oct=-1, spread=0.2)
        p.sub(level=0.5)
        p.filt(1, "lp24", cutoff=cut, reso=0.2, drive=0.3)
        p.lfo(3, "sine", div="2bar" if i != 2 else "1/2", bipolar=True)
        p.mod(M(3), "osc1_pos", 0.25)
        p.mod(M(3), "f1_cutoff", 0.06)
        if i == 2:
            p.mod(M(3), "osc1_detune", 0.15)
        p.feedback(0.15 if i != 3 else 0.35)
        p.insert(1, "tube", amount=0.35, pos="post")
        p.amp(a=0.003, d=0.5, s=1.0, r=0.2, level=0.75)
        p.voice("legato", glide=0.05)
        if i == 3:
            p.dist(drive=0.35, kind="tube")
        bass_fx(p, ott=0.4, mono=140.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(cut, cut * 5))])
        p.macro(2, "Detune", [("osc1_detune", 0.35), ("osc2_detune", 0.3)])
        p.macro(3, "Feedback", [("fb_amount", 0.5)])
        p.macro(4, "Movement", [("osc1_pos", 0.4)])

    # 7. wobble / growl -------------------------------------------------------------------------------
    wobbles = [
        ("Eighth Wobble", "Dubstep", "Saw Sweep", "1/8", "lp24"),
        ("Triplet Wobble", "Dubstep", "Square Sweep", "1/8T", "lp24"),
        ("Yoi Growl", "Dubstep", "Growl Vowel", "1/4", "formant"),
        ("Riddim Fold", "Dubstep", "Fold Sine", "1/8", "lp24"),
        ("Talk Bass", "Bass House", "Talk Box", "1/4", "lp12"),
    ]
    for i, (name, g, tab, div, ft) in enumerate(wobbles):
        p = P(name, C, g)
        p.osc(1, tab, pos=0.4, level=0.8, voices=2, detune=0.1, spread=0.35, mode="fold" if i == 3 else "std", warp=0.25 if i == 3 else 0.0)
        p.sub(level=0.5)
        p.filt(1, ft, cutoff=500.0 if ft == "lp24" else 700.0, reso=0.4 if ft == "lp24" else 0.6, drive=0.3)
        p.lfo(3, "sine", div=div, retrig=True, bipolar=False)
        p.mod(M(3), "f1_cutoff", cut_amt(500.0, 4500.0) if ft == "lp24" else 0.3)
        p.mod(M(3), "osc1_pos", 0.45)
        p.insert(1, "drive", amount=0.45, pos="pre")
        p.amp(a=0.002, d=0.4, s=1.0, r=0.1, level=0.8)
        p.voice("mono", glide=0.02)
        p.dist(drive=0.3, kind="hard" if i == 3 else "soft")
        bass_fx(p, ott=0.55, mono=120.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(500.0, 2500.0))])
        p.macro(2, "Growl", [("osc1_pos", 0.4), ("ins1_amount", 0.35)])
        p.macro(3, "Wobble Depth", [("osc1_warp", 0.4)])
        p.macro(4, "Grit", [("fx_dist_drive", 0.45)])

    # 8. festival / trance / psy ----------------------------------------------------------------------
    fest = [
        ("Big Room Drop", "Festival EDM"),
        ("Pumping Saw Bass", "Festival EDM"),
        ("Offbeat Trance", "Trance"),
        ("Psy Rolling", "Trance"),
        ("Progressive Pump", "Progressive House"),
    ]
    for i, (name, g) in enumerate(fest):
        p = P(name, C, g)
        p.osc(1, "Basic Shapes", pos=SAW, level=0.75, voices=3 if i in (0, 1, 4) else 1, detune=0.15, spread=0.3)
        p.sub(level=0.5)
        p.filt(1, "lp24", cutoff=900.0 if i < 2 else 500.0, reso=0.2 if i < 3 else 0.35, drive=0.25)
        p.env(1, d=0.12 if i in (2, 3) else 0.3, s=0.0, curve=0.2)
        p.mod(M(1), "f1_cutoff", cut_amt(600.0, 5000.0))
        if i in (2, 3):
            p.amp(a=0.001, d=0.15 if i == 3 else 0.2, s=0.0, r=0.06, level=0.85)
        else:
            p.amp(a=0.002, d=0.6, s=0.8, r=0.12, level=0.8)
        if i in (1, 4):
            p.lfo(3, "pump", div="1/4", bipolar=False)
            p.p["amp_level"] = 0.35
            p.mod(M(3), "amp_level", 0.55)
        if i == 0:
            p.env(2, d=0.08, s=0.0, curve=0.1)
            p.mod(M(2), "voice_pitch", semis_amt(12))
            p.dist(drive=0.4, kind="hard")
        p.voice("poly" if i in (2, 3) else "legato", glide=0.0 if i in (2, 3) else 0.03)
        bass_fx(p, ott=0.35, mono=140.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(600.0, 3500.0))])
        p.macro(2, "Decay", [("amp_d", 0.2), ("mod1_d", 0.2)])
        p.macro(3, "Sub", [("sub_level", 0.4)])
        p.macro(4, "OTT", [("fx_ott_depth", 0.4)])

    # 9. house bass ------------------------------------------------------------------------------------
    house = [
        ("Deep Organ Bass", "Deep House", "Organ", 0.2, "lp12", 1200.0),
        ("Moog Style Bass", "House", "Basic Shapes", 0.62, "alp", 700.0),
        ("Funky Pluck Bass", "House", "Basic Shapes", SAW, "lp24", 500.0),
        ("Disco Octave", "House", "Basic Shapes", SQUARE, "lp24", 900.0),
        ("Garage FM Bass", "Deep House", "FM Growl", 0.2, "lp12", 1600.0),
    ]
    for i, (name, g, tab, pos, ft, cut) in enumerate(house):
        p = P(name, C, g)
        p.osc(1, tab, pos=pos, level=0.8)
        if i == 1:
            p.osc(2, "Basic Shapes", pos=SQUARE, level=0.5, fine=-6.0)
        if i == 3:
            p.osc(2, "Basic Shapes", pos=SAW, level=0.5, oct=1)
        p.sub(level=0.35 if i != 0 else 0.5)
        p.filt(1, ft, cutoff=cut, reso=0.25 if i != 2 else 0.45, drive=0.2)
        p.env(1, d=0.18 if i != 1 else 0.4, s=0.1, curve=0.25)
        p.mod(M(1), "f1_cutoff", cut_amt(cut, cut * 6))
        if i == 4:
            p.env(2, d=0.25, s=0.0)
            p.mod(M(2), "osc1_pos", 0.5)
        p.amp(a=0.001, d=0.35, s=0.4 if i in (2, 3) else 0.8, r=0.08, level=0.8, vel=0.45)
        p.voice("mono" if i == 1 else "poly", glide=0.04 if i == 1 else 0.0, drift=0.35)
        p.insert(1, "tube", amount=0.25, pos="post")
        p.chorus(mix=0.15) if i == 0 else None
        bass_fx(p, ott=0.2, mono=150.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(cut, cut * 4))])
        p.macro(2, "Env Amount", [("mod1_d", 0.2)])
        p.macro(3, "Drive", [("ins1_amount", 0.5)])
        p.macro(4, "Sub", [("sub_level", 0.4)])

    # 10. tech house -----------------------------------------------------------------------------------
    tech = [
        ("Rubber Tech", "Tech House"),
        ("Groove Square", "Tech House"),
        ("Dirty Tech", "Tech House"),
        ("Bouncy Tech", "Tech House"),
        ("Minimal Thump", "Tech House"),
    ]
    for i, (name, g) in enumerate(tech):
        p = P(name, C, g)
        tab_pos = [SINE + 0.1, SQUARE, SAW, 0.35, TRI][i]
        p.osc(1, "Basic Shapes", pos=tab_pos, level=0.85)
        p.sub(level=0.35)
        p.filt(1, "lp24", cutoff=[2000.0, 600.0, 900.0, 700.0, 1500.0][i], reso=0.3, drive=0.3)
        p.env(1, d=0.12, s=0.0, curve=0.2)
        p.mod(M(1), "f1_cutoff", cut_amt(700.0, 4000.0))
        p.env(2, d=0.035, s=0.0, curve=0.1)
        p.mod(M(2), "voice_pitch", semis_amt([12, 5, 7, 9, 18][i]))
        if i == 3:
            p.perf(5, [1.0, 0.3, 0.6, 0.0, 0.8, 0.2, 0.5, 0.1], div="1/16", smooth=0.08)
            p.mod(M(5), "f1_cutoff", 0.18)
        p.amp(a=0.001, d=0.28, s=0.25, r=0.07, level=0.85, vel=0.4)
        p.insert(1, "drive" if i == 2 else "tube", amount=0.4 if i == 2 else 0.25, pos="post")
        if i == 2:
            p.dist(drive=0.35, kind="tube")
        p.voice("mono", glide=0.0, drift=0.15)
        bass_fx(p, ott=0.3, mono=150.0)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(800.0, 4000.0))])
        p.macro(2, "Thump", [("mod2_d", 0.25)])
        p.macro(3, "Decay", [("amp_d", 0.25)])
        p.macro(4, "Drive", [("ins1_amount", 0.5)])


# =====================================================================================================
# LEAD
# =====================================================================================================
def lead():
    C = "Lead"
    # 1. festival supersaws -------------------------------------------------------------------------
    saws = [
        ("Festival Anthem", "Festival EDM", 7, 0.5, 0.0),
        ("Mainstage Saw", "Festival EDM", 8, 0.55, 0.2),
        ("Big Room Lead", "Festival EDM", 5, 0.4, 0.35),
        ("Euphoric Supersaw", "Trance", 7, 0.45, 0.0),
        ("Progressive Saw", "Progressive House", 6, 0.38, 0.0),
    ]
    for i, (name, g, v, det, dist) in enumerate(saws):
        p = P(name, C, g)
        p.osc(1, "Basic Shapes", pos=SAW, level=0.75, voices=v, detune=det, spread=0.95, blend=0.75)
        p.osc(2, "Saw Stack" if i != 2 else "Basic Shapes", pos=0.3 if i != 2 else 0.68, level=0.45, oct=1 if i != 2 else 0,
              voices=5, detune=det * 0.8, spread=0.9)
        p.filt(1, "lp24", cutoff=9000.0 if i != 4 else 3500.0, reso=0.1)
        p.filt(2, "hp12", cutoff=150.0, reso=0.0)
        p.amp(a=0.004, d=1.0, s=0.9, r=0.4, level=0.7)
        if i == 4:
            p.lfo(3, "sine", div="2bar", bipolar=True)
            p.mod(M(3), "f1_cutoff", 0.15)
        if dist:
            p.dist(drive=dist, kind="soft", tone=0.6, mix=0.7)
        p.hyper(mix=0.35, detune=0.3, dim=0.4)
        p.ott(depth=0.3)
        lead_space(p, rev=0.25, dly=0.15)
        p.eq(hp=120.0, high=1.5)
        p.voice("poly", drift=0.25)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(p.p["f1_cutoff"], 18000.0) if i == 4 else -0.35)])
        p.macro(2, "Detune", [("osc1_detune", 0.3), ("osc2_detune", 0.3)])
        p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.2)])
        p.macro(4, "Width", [("fx_hyper_mix", 0.4)])

    # 2. electro leads -------------------------------------------------------------------------------
    el = [
        ("Electro Saw Lead", "Electro House", "Dirt Saw", 0.4, "std"),
        ("Squeal Lead", "Electro House", "Squeal", 0.5, "bendp"),
        ("Sync Screamer", "Electro House", "Saw Sweep", 1.0, "sync"),
        ("Dutch Screech", "Festival EDM", "Fidget", 0.6, "sync"),
        ("Chip Lead", "Electro House", "PWM Square", 0.35, "quant"),
    ]
    for i, (name, g, tab, pos, mode) in enumerate(el):
        p = P(name, C, g)
        p.osc(1, tab, pos=pos, level=0.8, mode=mode, warp=0.3 if mode != "std" else 0.0, voices=3 if i != 4 else 1, detune=0.15, spread=0.6)
        p.filt(1, "lp12", cutoff=8000.0, reso=0.15)
        p.env(1, d=0.4, s=0.2)
        if mode in ("sync", "bendp"):
            p.mod(M(1), "osc1_warp", 0.45)
        p.lfo(4, "sine", hz=5.5, retrig=True)
        p.env(2, a=0.4, d=0.1, s=1.0)
        p.mod(M(4), "osc1_fine", 0.04)  # gentle vibrato
        p.amp(a=0.002, d=0.8, s=0.9, r=0.2, level=0.7)
        p.voice("legato", glide=0.05)
        p.insert(1, "drive", amount=0.35, pos="post")
        p.dist(drive=0.25 if i != 4 else 0.1, kind="hard" if i == 3 else "soft")
        p.ott(depth=0.35)
        lead_space(p, rev=0.18, dly=0.18)
        p.macro(1, "Cutoff", [("f1_cutoff", -0.3)])
        p.macro(2, "Scream", [("osc1_warp", 0.5)])
        p.macro(3, "Grit", [("fx_dist_drive", 0.45)])
        p.macro(4, "Vibrato", [("osc1_fine", 0.06)])

    # 3. melodic techno leads -------------------------------------------------------------------------
    mt = [
        ("Hollow Sync", "Melodic Techno", "Hollow", 0.4, "sync"),
        ("Afterhours Lead", "Melodic Techno", "Saw Sweep", 0.85, "std"),
        ("Analog Mono", "Melodic Techno", "Basic Shapes", 0.62, "std"),
        ("Wide Saw Lead", "Melodic Techno", "Basic Shapes", SAW, "std"),
        ("Shimmer Lead", "Melodic Techno", "Glass Bell", 0.5, "std"),
    ]
    for i, (name, g, tab, pos, mode) in enumerate(mt):
        p = P(name, C, g)
        p.osc(1, tab, pos=pos, level=0.75, mode=mode, warp=0.2 if mode == "sync" else 0.0, voices=3 if i != 2 else 1, detune=0.12, spread=0.7)
        p.osc(2, "Basic Shapes", pos=SAW if i != 4 else SINE, level=0.4, oct=0 if i != 4 else 1, fine=7.0, voices=1)
        p.filt(1, "alp" if i == 2 else "lp24", cutoff=1800.0, reso=0.35, drive=0.15)
        p.lfo(3, "sine", div="2bar", bipolar=True)
        p.mod(M(3), "f1_cutoff", 0.18)
        if mode == "sync":
            p.mod(M(3), "osc1_warp", 0.2)
        p.env(1, a=0.05, d=0.8, s=0.4)
        p.mod(M(1), "f1_cutoff", 0.15)
        p.amp(a=0.01, d=1.0, s=0.85, r=0.6, level=0.7)
        p.voice("legato" if i == 2 else "poly", glide=0.08 if i == 2 else 0.0, drift=0.4)
        p.delay(mix=0.28, div="1/4D" if i % 2 else "1/8D", fb=0.5, tone=0.4)
        p.reverb(mix=0.35, size=0.85, decay=5.0, damp=0.5)
        p.tape(mix=0.6, wow=0.2, flutter=0.15, sat=0.25) if i in (1, 2) else None
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(1800.0, 9000.0))])
        p.macro(2, "Resonance", [("f1_reso", 0.4)])
        p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.25)])
        p.macro(4, "Movement", [("osc1_pos", 0.3)])

    # 4. future house / pop leads --------------------------------------------------------------------
    fhl = [
        ("Whistle Lead", "Future House", "Basic Shapes", SINE, 0.0),
        ("Future Organ Lead", "Future House", "Organ", 0.3, 0.0),
        ("Talk Box Lead", "Future House", "Talk Box", 0.3, 0.0),
        ("Bell Hook", "Future House", "FM Bell", 0.35, 0.0),
        ("Square Hook", "Bass House", "Square Sweep", 0.8, 0.0),
    ]
    for i, (name, g, tab, pos, _) in enumerate(fhl):
        p = P(name, C, g)
        p.osc(1, tab, pos=pos, level=0.8, voices=1 if i in (0, 3) else 2, detune=0.08, spread=0.4)
        if i == 0:
            p.lfo(4, "sine", hz=5.0, retrig=True)
            p.mod(M(4), "osc1_fine", 0.05)
        if i == 2:
            p.lfo(3, "tri", div="1/4", retrig=True, bipolar=False)
            p.mod(M(3), "osc1_pos", 0.6)
        if i == 3:
            p.env(1, d=0.6, s=0.1)
            p.mod(M(1), "osc1_pos", 0.4)
        p.filt(1, "lp12", cutoff=7000.0, reso=0.1)
        p.amp(a=0.003, d=0.6 if i == 3 else 0.8, s=0.3 if i == 3 else 0.9, r=0.25, level=0.75)
        p.voice("legato" if i in (0, 2) else "poly", glide=0.06)
        p.ott(depth=0.45)
        p.chorus(mix=0.2)
        lead_space(p, rev=0.2, dly=0.2, div="1/8D")
        p.macro(1, "Tone", [("f1_cutoff", -0.3)])
        p.macro(2, "Wavetable", [("osc1_pos", 0.5)])
        p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.2)])
        p.macro(4, "OTT", [("fx_ott_depth", 0.4)])

    # 5. hoover / rave -------------------------------------------------------------------------------
    hv = [("Classic Hoover", "Techno"), ("Rave Hoover", "Festival EDM"), ("Dark Hoover", "Techno"), ("Alpha Hoover", "Electro House"), ("Hoover Glide", "Trance")]
    for i, (name, g) in enumerate(hv):
        p = P(name, C, g)
        p.osc(1, "PWM Square", pos=0.3 + 0.1 * i, level=0.7, voices=5, detune=0.5, spread=0.8)
        p.osc(2, "Saw Sweep", pos=0.8, level=0.5, oct=-1, voices=3, detune=0.35)
        p.env(2, d=0.25, s=0.0)
        p.mod(M(2), "voice_pitch", semis_amt(-7))
        p.lfo(3, "sine", div="1/4", bipolar=True)
        p.mod(M(3), "osc1_pos", 0.25)
        p.filt(1, "lp24", cutoff=4000.0 if i != 2 else 1600.0, reso=0.1)
        p.chorus(mix=0.45, depth=0.7)
        p.amp(a=0.01, d=0.6, s=0.9, r=0.3, level=0.7)
        p.voice("legato", glide=0.1 + 0.03 * i)
        p.ott(depth=0.3)
        p.reverb(mix=0.18)
        p.macro(1, "Cutoff", [("f1_cutoff", cut_amt(p.p["f1_cutoff"], 12000.0))])
        p.macro(2, "Detune", [("osc1_detune", 0.3)])
        p.macro(3, "Dive", [("mod2_d", 0.3)])
        p.macro(4, "Space", [("fx_rev_mix", 0.35)])

    # 6. mono solo leads -----------------------------------------------------------------------------
    solo = [("Moog Solo", "House"), ("Square Solo", "Deep House"), ("Fat Mono", "Progressive House"), ("Acid Lead", "Techno"), ("Portamento Sine", "Melodic Techno")]
    for i, (name, g) in enumerate(solo):
        p = P(name, C, g)
        pos = [SAW, SQUARE, SAW, SAW, SINE][i]
        p.osc(1, "Basic Shapes", pos=pos, level=0.75)
        if i in (0, 2):
            p.osc(2, "Basic Shapes", pos=SQUARE, level=0.5, fine=8.0, oct=-1 if i == 2 else 0)
        p.filt(1, "alp" if i != 3 else "lp24", cutoff=[2200.0, 3000.0, 2000.0, 700.0, 6000.0][i], reso=[0.3, 0.15, 0.2, 0.75, 0.0][i], drive=0.2)
        p.env(1, d=0.5 if i != 3 else 0.2, s=0.3 if i != 3 else 0.0)
        p.mod(M(1), "f1_cutoff", 0.2 if i != 3 else cut_amt(700.0, 5000.0))
        p.lfo(4, "sine", hz=5.2, retrig=True)
        p.mod(M(4), "osc1_fine", 0.03)
        p.amp(a=0.003, d=0.8, s=0.85, r=0.2, level=0.75)
        p.voice("legato", glide=0.12 if i != 3 else 0.06, drift=0.45)
        p.delay(mix=0.18, div="1/4D")
        p.reverb(mix=0.18)
        p.insert(1, "tube", amount=0.25, pos="post")
        p.macro(1, "Cutoff", [("f1_cutoff", 0.3)])
        p.macro(2, "Glide", [("voice_glide", 0.3)]) if False else p.macro(2, "Resonance", [("f1_reso", 0.35)])
        p.macro(3, "Drive", [("ins1_amount", 0.5)])
        p.macro(4, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.2)])

    # 7. trance leads --------------------------------------------------------------------------------
    tr = [("Uplifting Lead", "Trance"), ("Gated Supersaw", "Trance"), ("Trance Pluck Lead", "Trance"), ("Psy Lead", "Trance"), ("Vocal Trance Lead", "Trance")]
    for i, (name, g) in enumerate(tr):
        p = P(name, C, g)
        p.osc(1, "Basic Shapes" if i != 4 else "Formant Vowels", pos=SAW if i != 4 else 0.4, level=0.75, voices=7, detune=0.42, spread=0.95)
        p.osc(2, "Basic Shapes", pos=SAW, level=0.4, oct=1, voices=3, detune=0.25)
        p.filt(1, "lp24", cutoff=6000.0 if i != 3 else 1500.0, reso=0.15 if i != 3 else 0.5)
        if i == 1:
            p.perf(5, [1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0], div="1/16", smooth=0.04)
            p.p["amp_level"] = 0.0
            p.mod(M(5), "amp_level", 0.8)
        if i == 2:
            p.env(1, d=0.25, s=0.2)
            p.mod(M(1), "f1_cutoff", 0.2)
        if i == 3:
            p.env(1, d=0.12, s=0.0)
            p.mod(M(1), "f1_cutoff", cut_amt(1500.0, 7000.0))
        if i == 4:
            p.lfo(3, "tri", div="1/2", bipolar=False, retrig=True)
            p.mod(M(3), "osc1_pos", 0.5)
        p.amp(a=0.005, d=0.5 if i == 2 else 1.0, s=0.6 if i == 2 else 0.9, r=0.4, level=0.7 if i != 1 else 0.0)
        p.hyper(mix=0.3, dim=0.4)
        lead_space(p, rev=0.3, dly=0.22, div="1/8D", size=0.8, decay=4.0)
        p.ott(depth=0.25)
        p.macro(1, "Cutoff", [("f1_cutoff", 0.25)])
        p.macro(2, "Detune", [("osc1_detune", 0.3)])
        p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.2)])
        p.macro(4, "Width", [("fx_hyper_mix", 0.4)])

    # 8. brass leads ----------------------------------------------------------------------------------
    br = [("Synth Brass", "House"), ("Big Horn", "Festival EDM"), ("Brass Section", "Progressive House"), ("Fanfare", "Festival EDM"), ("Soft Brass", "Deep House")]
    for i, (name, g) in enumerate(br):
        p = P(name, C, g)
        p.osc(1, "Brass", pos=0.3, level=0.75, voices=4, detune=0.2, spread=0.7)
        p.osc(2, "Basic Shapes", pos=SAW, level=0.4, voices=3, detune=0.25, oct=-1 if i in (1, 3) else 0)
        p.env(1, a=0.03, d=0.5, s=0.3)
        p.mod(M(1), "osc1_pos", 0.5)
        p.filt(1, "lp24", cutoff=1500.0 if i != 4 else 900.0, reso=0.1)
        p.mod(M(1), "f1_cutoff", cut_amt(1500.0, 7000.0))
        p.mod(VEL, "f1_cutoff", 0.1)
        p.amp(a=0.02, d=0.6, s=0.85, r=0.25, level=0.7)
        p.chorus(mix=0.25)
        p.reverb(mix=0.2)
        if i in (1, 3):
            p.dist(drive=0.25, kind="tube")
            p.ott(depth=0.35)
        p.macro(1, "Brightness", [("f1_cutoff", 0.3)])
        p.macro(2, "Swell", [("amp_a", 0.2)])
        p.macro(3, "Space", [("fx_rev_mix", 0.3)])
        p.macro(4, "Detune", [("osc1_detune", 0.3)])

    # 9. pluck-leads with delay ----------------------------------------------------------------------
    pl = [("Delay Hook", "Progressive House"), ("Bouncy Lead", "Future House"), ("Stutter Lead", "Electro House"), ("Glide Pluck Lead", "Melodic Techno"), ("Arcade Lead", "Future Bass")]
    for i, (name, g) in enumerate(pl):
        p = P(name, C, g)
        p.osc(1, ["Saw Sweep", "Square Sweep", "Fidget", "Hollow", "PWM Square"][i], pos=0.8, level=0.8, voices=3, detune=0.18)
        p.filt(1, "lp24", cutoff=1200.0, reso=0.25)
        p.env(1, d=0.3, s=0.25)
        p.mod(M(1), "f1_cutoff", cut_amt(1200.0, 8000.0))
        if i == 2:
            p.perf(5, [1, 1, 0, 1, 0, 1, 1, 0], div="1/32", smooth=0.02)
            p.p["amp_level"] = 0.1
            p.mod(M(5), "amp_level", 0.7)
        p.amp(a=0.002, d=0.5, s=0.5, r=0.25, level=0.75 if i != 2 else 0.1)
        p.voice("legato" if i == 3 else "poly", glide=0.07)
        p.ott(depth=0.3)
        lead_space(p, rev=0.22, dly=0.32, div=["1/8D", "1/8", "1/16", "1/4D", "1/8T"][i])
        p.macro(1, "Cutoff", [("f1_cutoff", 0.3)])
        p.macro(2, "Decay", [("mod1_d", 0.25)])
        p.macro(3, "Echo", [("fx_delay_mix", 0.3), ("fx_delay_fb", 0.25)])
        p.macro(4, "Space", [("fx_rev_mix", 0.3)])

    # 10. vocal / formant leads ----------------------------------------------------------------------
    vx = [("Vox Lead", "Melodic Techno"), ("Choir Lead", "Trance"), ("Yoi Lead", "Dubstep"), ("Talk Lead", "Electro House"), ("Formant Sweep", "Future Bass")]
    for i, (name, g) in enumerate(vx):
        p = P(name, C, g)
        p.osc(1, ["Formant Vowels", "Choir", "Growl Vowel", "Talk Box", "Formant Vowels"][i], pos=0.3, level=0.8, voices=3, detune=0.12)
        p.osc(2, "Basic Shapes", pos=SAW, level=0.3, voices=1)
        p.lfo(3, ["tri", "sine", "sine", "tri", "up"][i], div=["1/2", "1bar", "1/4", "1/8", "1bar"][i], retrig=True, bipolar=False)
        p.mod(M(3), "osc1_pos", 0.6)
        p.filt(1, "formant" if i in (2, 3) else "lp12", cutoff=900.0 if i in (2, 3) else 8000.0, reso=0.5 if i in (2, 3) else 0.1)
        if i in (2, 3):
            p.mod(M(3), "f1_cutoff", 0.25)
        p.amp(a=0.01, d=0.8, s=0.9, r=0.3, level=0.75)
        p.voice("legato", glide=0.06)
        p.ott(depth=0.35)
        p.chorus(mix=0.2)
        lead_space(p, rev=0.25, dly=0.15)
        p.macro(1, "Vowel", [("osc1_pos", 0.6)])
        p.macro(2, "Formant", [("f1_cutoff", 0.3)] if i in (2, 3) else [("osc1_warp", 0.3)])
        p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.2)])
        p.macro(4, "OTT", [("fx_ott_depth", 0.4)])


# =====================================================================================================
# PAD
# =====================================================================================================
def pad():
    C = "Pad"
    groups = [
        # name list, genre list, table, pos, voices, detune, cutoff, filter type
        (["Warm Analog Pad", "Velvet Pad", "Soft Saw Pad", "Juno Wash", "Sunset Pad"],
         ["Deep House", "House", "Progressive House", "Deep House", "Melodic Techno"], "Basic Shapes", SAW, 4, 0.22, 1600.0, "alp"),
        (["Trance Supersaw Pad", "Euphoria Pad", "Stadium Pad", "Anthem Pad", "Hands Up Pad"],
         ["Trance", "Trance", "Festival EDM", "Festival EDM", "Progressive House"], "Basic Shapes", SAW, 7, 0.4, 7000.0, "lp24"),
        (["String Ensemble", "Silk Strings", "Cinematic Strings", "Dark Strings", "Disco Strings"],
         ["Progressive House", "Deep House", "Melodic Techno", "Techno", "House"], "Strings", 0.5, 4, 0.25, 4500.0, "lp12"),
        (["Choir Air", "Angel Voices", "Ghost Choir", "Vocal Haze", "Monastery"],
         ["Melodic Techno", "Trance", "Techno", "Future Bass", "Melodic Techno"], "Choir", 0.5, 3, 0.15, 6000.0, "lp12"),
        (["Glass Cathedral", "Crystal Pad", "Ice Field", "Digital Mist", "Prism Pad"],
         ["Melodic Techno", "Future Bass", "Trance", "Progressive House", "Future Bass"], "Glass Bell", 0.4, 3, 0.15, 7000.0, "lp12"),
        (["Underground Drone", "Abyss", "Night Drive", "Black Hole", "Concrete Pad"],
         ["Techno", "Melodic Techno", "Melodic Techno", "Techno", "Techno"], "Warm Spectra", 0.3, 3, 0.2, 900.0, "lp24"),
        (["Evolving Spectra", "Spectral Drift", "Morphing Pad", "Living Texture", "Aurora"],
         ["Melodic Techno", "Progressive House", "Melodic Techno", "Techno", "Trance"], "Noise Spectra", 0.2, 3, 0.15, 2600.0, "lp24"),
        (["Pumping Pad", "Sidechain Wash", "Ducking Chords Pad", "Breathing Pad", "Club Pump Pad"],
         ["House", "Festival EDM", "Progressive House", "Future House", "Tech House"], "Basic Shapes", SAW, 6, 0.32, 5000.0, "lp24"),
        (["Slow Sweep Pad", "Filter Tide", "Ocean Sweep", "Phase Drift Pad", "Rising Haze"],
         ["Progressive House", "Deep House", "Melodic Techno", "Techno", "Trance"], "Saw Sweep", 0.9, 4, 0.25, 900.0, "lp24"),
        (["Air Pad", "Breath Texture", "Wind Pad", "Dust Pad", "Vinyl Haze"],
         ["Melodic Techno", "Deep House", "Techno", "Deep House", "House"], "Warm Spectra", 0.6, 2, 0.1, 3500.0, "lp12"),
    ]
    for gi, (names, genres, tab, pos, voices, det, cut, ft) in enumerate(groups):
        for k in range(5):
            p = P(names[k], C, genres[k])
            att = [0.6, 1.0, 1.5, 0.35, 2.2][k]
            p.osc(1, tab, pos=min(1.0, pos + 0.08 * (k - 2) if tab != "Basic Shapes" else pos), level=0.7, voices=voices, detune=det, spread=0.9, blend=0.7)
            second = ["Basic Shapes", "Sine Harmonics", "Warm Spectra", "Strings", "Hollow"][k]
            p.osc(2, second, pos=[SQUARE, 0.4, 0.5, 0.5, 0.3][k], level=0.38, oct=[-1, 1, 0, 0, 1][k], voices=2, detune=0.15, spread=0.8)
            p.filt(1, ft, cutoff=cut * [1.0, 0.8, 1.2, 0.65, 1.0][k], reso=0.12 + 0.04 * k, drive=0.1)
            p.amp(a=att, d=2.0, s=0.85, r=2.5 + 0.5 * k, curve=0.4, level=0.7, vel=0.2)
            p.lfo(3, "sine", hz=0.07 + 0.03 * k, bipolar=True)
            p.lfo(4, "tri", hz=0.11 + 0.02 * k, bipolar=True)
            p.mod(M(3), "osc1_pos", 0.15 if tab != "Basic Shapes" else 0.04)
            p.mod(M(4), "f1_cutoff", 0.07)
            p.mod(M(4), "osc2_pos", 0.2)
            if gi == 5:      # dark drones: slow random movement, noise
                p.rnd(7, div="1bar", smooth=0.9, bipolar=True)
                p.mod(M(7), "f1_cutoff", 0.08)
                p.noise(level=0.06, kind="pink", color=0.35)
            if gi == 6:      # evolving
                p.rnd(7, div="2bar", smooth=0.95)
                p.mod(M(7), "osc1_pos", 0.45)
            if gi == 7:      # pump
                p.lfo(5, "pump", div="1/4", bipolar=False)
                p.p["mod5_type"] = 1
                p.p["amp_level"] = 0.25
                p.mod(M(5), "amp_level", 0.6)
                p.amp(a=0.05, d=1.0, s=1.0, r=0.6, level=0.25, vel=0.2)
            if gi == 8:      # sweeps
                p.lfo(5, "sine", div=["4bar", "8bar", "4bar", "2bar", "8bar"][k], bipolar=True)
                p.p["mod5_type"] = 1
                p.mod(M(5), "f1_cutoff", cut_amt(cut, cut * 6))
                if k == 3:
                    p.phaser(mix=0.5, rate=0.15)
            if gi == 9:      # air
                p.noise(level=0.18, kind="pink" if k != 4 else "vinyl", color=0.65)
                p.tape(mix=0.7, wow=0.35, flutter=0.2, sat=0.2)
            if gi == 3:
                p.lfo(5, "tri", hz=0.05, bipolar=False)
                p.p["mod5_type"] = 1
                p.mod(M(5), "osc1_pos", 0.4)
            if gi in (0, 2, 3):
                p.chorus(mix=0.3, depth=0.55)
            if gi in (1, 7):
                p.hyper(mix=0.35, dim=0.5)
            p.reverb(mix=0.38 + 0.03 * (k % 3), size=0.85, decay=5.0 + k, damp=0.45)
            if gi in (4, 6, 8):
                p.delay(mix=0.15, div="1/4D", fb=0.45, tone=0.35)
            p.eq(hp=140.0 if gi != 5 else 60.0, high=1.0 if gi in (1, 4) else 0.0)
            p.stereo(width=1.25)
            p.voice("poly", drift=0.35)
            p.macro(1, "Brightness", [("f1_cutoff", cut_amt(p.p["f1_cutoff"], min(16000.0, p.p["f1_cutoff"] * 5)))])
            p.macro(2, "Movement", [("osc1_pos", 0.35), ("f1_reso", 0.2)])
            p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_rev_size", 0.15)])
            p.macro(4, "Attack", [("amp_a", 0.25)])


# =====================================================================================================
# PLUCK
# =====================================================================================================
def pluck():
    C = "Pluck"
    groups = [
        (["House Pluck", "Chord Pluck", "Piano House Pluck", "Garage Pluck", "Shuffle Pluck"], ["House", "House", "House", "Deep House", "Tech House"],
         "Pluck", 1.0, 3, 0.15, "lp24", 500.0, 0.3, "1/8D"),
        (["Trance Pluck", "Euphoric Pluck", "Gate Pluck", "Uplift Pluck", "Classic Trance Pluck"], ["Trance"] * 5,
         "Basic Shapes", SAW, 7, 0.38, "lp24", 700.0, 0.25, "1/8D"),
        (["Afterlife Pluck", "Hollow Pluck", "Nocturne Pluck", "Echo Chamber", "Deep Water Pluck"], ["Melodic Techno"] * 5,
         "Hollow", 0.4, 1, 0.1, "lp24", 900.0, 0.2, "1/4D"),
        (["Future Bass Pluck", "Kawaii Pluck", "Sparkle Pluck", "Wonky Pluck", "Glide Pluck"], ["Future Bass"] * 5,
         "Super Square", 0.4, 4, 0.25, "lp12", 1200.0, 0.15, "1/8"),
        (["Marimba", "Wood Mallet", "Log Drum", "Tropical Mallet", "Soft Mallet"], ["Deep House", "House", "Deep House", "Future House", "Melodic Techno"],
         "Mallet", 0.2, 1, 0.0, "lp12", 6000.0, 0.0, "1/8D"),
        (["Bell Pluck", "Chime Pluck", "Glass Pluck", "Music Box", "Crystal Drop"], ["Progressive House", "Melodic Techno", "Future Bass", "Deep House", "Trance"],
         "Glass Bell", 0.4, 1, 0.0, "lp12", 9000.0, 0.0, "1/8D"),
        (["Progressive Pluck", "Filtered Saw Pluck", "Big Pluck", "Anthem Pluck", "Night Pluck"], ["Progressive House"] * 3 + ["Festival EDM", "Progressive House"],
         "Saw Sweep", 1.0, 5, 0.3, "lp24", 400.0, 0.3, "1/8D"),
        (["Kalimba", "FM Pluck", "Thumb Piano", "Steel Pluck", "Digital Kalimba"], ["Deep House", "Tech House", "Melodic Techno", "Techno", "Future House"],
         "FM Bell", 0.25, 1, 0.0, "lp12", 7000.0, 0.0, "1/8D"),
        (["Electro Pluck", "Buzz Pluck", "Square Pluck", "Fidget Pluck", "Dirty Pluck"], ["Electro House"] * 4 + ["Bass House"],
         "Square Sweep", 0.9, 2, 0.12, "lp24", 900.0, 0.35, "1/16"),
        (["Deep Pluck", "Soft Sine Pluck", "Lofi Pluck", "Warm Pluck", "Muted Pluck"], ["Deep House", "Deep House", "House", "Melodic Techno", "Tech House"],
         "Basic Shapes", TRI, 1, 0.0, "lp12", 1500.0, 0.1, "1/4D"),
    ]
    for gi, (names, genres, tab, pos, voices, det, ft, cut, reso, div) in enumerate(groups):
        for k in range(5):
            p = P(names[k], C, genres[k])
            decay = [0.25, 0.35, 0.18, 0.45, 0.3][k]
            p.osc(1, tab, pos=pos, level=0.8, voices=voices, detune=det, spread=0.85)
            if gi in (0, 6):
                p.osc(2, "Basic Shapes", pos=SAW, level=0.4, voices=3, detune=0.2, oct=[0, 1, 0, -1, 1][k])
            if gi == 2:
                p.osc(2, "Basic Shapes", pos=SINE, level=0.5, oct=1)
            if gi == 7:
                p.p["osc1_fm"] = 0.2 + 0.1 * k
                p.p["osc1_fmratio"] = [2.0, 3.5, 4.0, 7.0, 1.5][k]
            p.filt(1, ft, cutoff=cut * [1.0, 1.3, 0.8, 1.1, 0.7][k], reso=reso, drive=0.1)
            p.env(1, d=decay, s=0.0, curve=0.2)
            if tab == "Pluck":
                p.p["osc1_pos"] = 1.0
                p.mod(M(1), "osc1_pos", -0.9)
            if gi in (4, 5, 7):
                p.mod(M(1), "osc1_pos", 0.35)
                p.mod(M(1), "osc1_fm", 0.2) if gi == 7 else None
            p.mod(M(1), "f1_cutoff", cut_amt(p.p["f1_cutoff"], min(16000.0, p.p["f1_cutoff"] * 9)) if gi not in (4, 5, 7) else 0.15)
            p.mod(VEL, "f1_cutoff", 0.08)
            p.amp(a=0.001, d=decay * 2.2, s=0.0, r=decay * 1.4, curve=0.3, level=0.8, vel=0.5)
            if gi == 3 and k in (3, 4):
                p.env(2, d=0.12, s=0.0)
                p.mod(M(2), "voice_pitch", semis_amt(-12 if k == 4 else 5))
            if gi == 1 and k == 2:
                p.perf(5, [1, 0.2, 1, 1, 0.2, 1, 0.2, 1], div="1/16", smooth=0.03)
                p.mod(M(5), "amp_level", 0.4)
            if gi == 8:
                p.insert(1, "drive", amount=0.4, pos="post")
                p.dist(drive=0.25, kind="hard")
            if gi == 9 and k == 2:
                p.tape(mix=0.8, wow=0.45, flutter=0.3, sat=0.4)
                p.insert(1, "crush", amount=0.35, pos="post", mix=0.6)
            if gi in (1, 3, 6):
                p.hyper(mix=0.3, dim=0.4)
                p.ott(depth=0.3)
            if gi in (0, 2, 9):
                p.chorus(mix=0.2)
            p.delay(mix=0.22 + 0.04 * (gi == 2), div=div, fb=0.42 if gi == 2 else 0.33, tone=0.45)
            p.reverb(mix=0.22 + 0.1 * (gi == 2), size=0.7, decay=2.5 + 2.0 * (gi == 2))
            p.eq(hp=150.0 if gi not in (4, 9) else 60.0)
            p.voice("poly", drift=0.25)
            p.macro(1, "Brightness", [("f1_cutoff", 0.3)])
            p.macro(2, "Decay", [("mod1_d", 0.3), ("amp_d", 0.3), ("amp_r", 0.2)])
            p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.3)])
            p.macro(4, "Resonance", [("f1_reso", 0.35)])


# =====================================================================================================
# KEYS
# =====================================================================================================
def keys():
    C = "Keys"
    groups = [
        (["House Organ", "M1 Organ", "Bass House Organ", "Classic 90s Organ", "Rave Organ"], ["House", "House", "Bass House", "House", "Techno"], "Organ", 0.2),
        (["House Piano", "Piano Stab Keys", "Uplift Piano", "Rave Piano", "Bright Piano"], ["House", "House", "Progressive House", "Festival EDM", "House"], "FM Bell", 0.15),
        (["Digital EP", "Rhodes Dream", "Soul Keys", "Lounge EP", "Tine Piano"], ["Deep House", "Deep House", "House", "Deep House", "Future Bass"], "Sine Harmonics", 0.06),
        (["Bell Keys", "Celesta", "Bright Bells", "Toy Box", "Cathedral Bells"], ["Progressive House", "Melodic Techno", "Future Bass", "Future House", "Trance"], "Glass Bell", 0.3),
        (["Wurly", "Clav Funk", "Reed Keys", "Funk Keys", "Wah Clav"], ["House", "House", "Deep House", "Tech House", "House"], "Odd Sweep", 0.4),
        (["Drive Organ", "Leslie Organ", "Gospel Organ", "Rock Organ", "Church Organ"], ["House", "Deep House", "House", "Festival EDM", "Melodic Techno"], "Organ", 0.5),
        (["Vibraphone", "Soft Vibes", "Jazz Vibes", "Ambient Vibes", "Tremolo Vibes"], ["Deep House"] * 2 + ["House", "Melodic Techno", "Deep House"], "Mallet", 0.3),
        (["Poly Keys", "Juno Keys", "Analog Poly", "Stab Keys", "Brass Keys"], ["Deep House", "House", "Melodic Techno", "Tech House", "Progressive House"], "Basic Shapes", SAW),
        (["Deep House Keys", "Late Night Keys", "Smooth Keys", "Velvet Keys", "Warm Tines"], ["Deep House"] * 5, "Sine Harmonics", 0.12),
        (["Synth Harp", "Digital Harp", "Glass Harp", "Airy Keys", "Dream Keys"], ["Trance", "Progressive House", "Melodic Techno", "Future Bass", "Trance"], "Pluck", 0.6),
    ]
    for gi, (names, genres, tab, pos) in enumerate(groups):
        for k in range(5):
            p = P(names[k], C, genres[k])
            p.osc(1, tab, pos=min(1.0, pos + 0.05 * k), level=0.75, voices=1 if gi not in (7,) else 3, detune=0.15)
            if gi == 1:    # piano-ish: FM bell + saw body with fast decay
                p.osc(2, "Basic Shapes", pos=SAW, level=0.08, voices=2, detune=0.08)
                p.env(2, d=0.6, s=0.0)
                p.mod(M(2), "osc2_level", 0.4)
            if gi == 2:    # EP: sine harmonics + bell
                p.osc(2, "FM Bell", pos=0.25, level=0.35)
                p.env(2, d=0.5, s=0.05)
                p.mod(M(2), "osc2_level", 0.35)
                p.mod(VEL, "osc2_level", 0.25)
                p.lfo(3, "sine", hz=4.5 + 0.3 * k)
                p.mod(M(3), "amp_pan", 0.25)
            if gi in (0, 5):
                p.osc(2, "Basic Shapes", pos=SQUARE if gi == 0 else SINE, level=0.35, oct=1 if gi == 0 else -1)
            if gi == 4:
                p.p["osc1_mode"] = MODES["pwm"]; p.p["osc1_warp"] = 0.3
            if gi == 6:
                p.lfo(3, "sine", hz=5.0, retrig=False)
                p.mod(M(3), "amp_level", 0.12 if k != 4 else 0.3)
            if gi == 9:
                p.p["osc1_pos"] = 1.0
            p.filt(1, "lp12" if gi != 4 else "lp24", cutoff=[9000, 7000, 5500, 12000, 1200, 6000, 8000, 2500, 3000, 4000][gi], reso=0.1 if gi != 4 else 0.45)
            p.env(1, d=[0.2, 0.8, 1.2, 1.5, 0.25, 0.3, 1.0, 0.6, 1.0, 0.7][gi] * (0.8 + 0.1 * k), s=0.0)
            if gi == 9:
                p.mod(M(1), "osc1_pos", -0.85)
            p.mod(M(1), "f1_cutoff", 0.25 if gi in (4, 7, 8) else 0.1)
            p.mod(VEL, "f1_cutoff", 0.12)
            sustain = gi in (0, 5)
            p.amp(a=0.002, d=[0.3, 2.2, 2.8, 3.0, 0.8, 0.3, 2.5, 1.2, 2.6, 2.0][gi], s=0.9 if sustain else 0.0, r=0.35 if not sustain else 0.12, level=0.75, vel=0.55)
            if gi in (0, 5):
                p.chorus(mix=0.45, rate=1.2 + 0.4 * k, depth=0.7)   # rotary feel
                p.insert(1, "tube", amount=0.2 + 0.08 * k, pos="post")
            if gi in (2, 8):
                p.chorus(mix=0.25)
                p.tape(mix=0.5, wow=0.2, flutter=0.15, sat=0.2) if k % 2 else None
            if gi == 4 and k == 4:
                p.lfo(4, "sine", div="1/8", retrig=True, bipolar=False)
                p.mod(M(4), "f1_cutoff", 0.3)
            p.reverb(mix=0.18 + 0.03 * k, size=0.65, decay=2.0 + 0.5 * k)
            if gi in (3, 9):
                p.delay(mix=0.18, div="1/8D", fb=0.35)
            p.eq(hp=80.0)
            p.voice("poly", drift=0.2)
            p.macro(1, "Tone", [("f1_cutoff", 0.3)])
            p.macro(2, "Decay", [("amp_d", 0.25), ("mod1_d", 0.25)])
            p.macro(3, "Space", [("fx_rev_mix", 0.3)])
            p.macro(4, "Character", [("osc1_pos", 0.3)])


# =====================================================================================================
# CHORD / STAB
# =====================================================================================================
def chord():
    C = "Chord"
    groups = [
        (["Deep House Stab", "Late Night Stab", "Chord Memory", "Velvet Stab", "Smoky Stab"], ["Deep House"] * 5),
        (["Detroit Stab", "Motor City", "Warehouse Stab", "Rust Stab", "Machine Chord"], ["Techno"] * 3 + ["Tech House", "Techno"]),
        (["Future Bass Chord", "Supersaw Swell", "Wonky Chords", "Kawaii Chords", "Feels Chord"], ["Future Bass"] * 5),
        (["Big Room Chord", "Main Stage Stab", "Festival Chord", "Drop Chord", "Arena Stab"], ["Festival EDM"] * 5),
        (["Rave Stab", "Fifth Stab", "Hardcore Stab", "Old School Stab", "Warehouse Rave"], ["Techno", "Techno", "Festival EDM", "House", "Techno"]),
        (["Dub Chord", "Dub Techno Echo", "Basic Channel", "Fog Chord", "Tape Dub"], ["Techno", "Techno", "Techno", "Melodic Techno", "Techno"]),
        (["Brass Stab", "Horn Stab", "Disco Brass", "Funk Stab", "Power Brass"], ["House", "House", "House", "Tech House", "Festival EDM"]),
        (["Organ Stab", "Bass House Stab", "90s Organ Stab", "Garage Stab", "Speed Stab"], ["Bass House", "Bass House", "House", "Deep House", "Bass House"]),
        (["Pumping Chords", "Sidechain Saw", "Prog Chords", "Ducked Supersaw", "Breathing Chords"], ["Progressive House", "Festival EDM", "Progressive House", "Trance", "Future House"]),
        (["Melancholy Chord", "Hollow Chord", "Dark Chord", "Afterlife Chord", "Midnight Chord"], ["Melodic Techno"] * 5),
    ]
    for gi, (names, genres) in enumerate(groups):
        for k in range(5):
            p = P(names[k], C, genres[k])
            if gi == 0:
                p.osc(1, "Basic Shapes", pos=SAW, level=0.7, voices=3, detune=0.15)
                p.osc(2, "Basic Shapes", pos=SQUARE, level=0.4, fine=-8.0)
                p.filt(1, "alp", cutoff=700.0 + 150.0 * k, reso=0.25)
            elif gi == 1:
                p.osc(1, "Basic Shapes", pos=SQUARE, level=0.7, voices=2, detune=0.1)
                p.osc(2, "Basic Shapes", pos=SAW, level=0.5, oct=1)
                p.filt(1, "lp24", cutoff=600.0 + 100.0 * k, reso=0.45)
            elif gi in (2, 3, 8):
                p.osc(1, "Basic Shapes", pos=SAW, level=0.72, voices=7, detune=0.38 + 0.03 * k, spread=0.95, blend=0.7)
                p.osc(2, "Saw Stack" if gi == 3 else "Super Square", pos=0.3, level=0.4, oct=1 if gi == 3 else 0, voices=4, detune=0.3)
                p.filt(1, "lp24", cutoff=6000.0 if gi != 8 else 3500.0, reso=0.12)
            elif gi == 4:
                p.osc(1, "Basic Shapes", pos=SAW, level=0.65, voices=3, detune=0.2)
                p.osc(2, "Basic Shapes", pos=SAW, level=0.55, semi=[7, 7, 5, 12, 7][k], voices=3, detune=0.2)
                p.filt(1, "lp24", cutoff=1800.0, reso=0.3)
            elif gi == 5:
                p.osc(1, "Basic Shapes", pos=SAW, level=0.7, voices=2, detune=0.1)
                p.filt(1, "bp", cutoff=900.0 + 120.0 * k, reso=0.3)
                p.filt(2, "combp", cutoff=180.0, reso=0.25, mix=0.4)
            elif gi == 6:
                p.osc(1, "Brass", pos=0.4, level=0.7, voices=4, detune=0.2)
                p.osc(2, "Basic Shapes", pos=SAW, level=0.4, voices=2)
                p.filt(1, "lp24", cutoff=1600.0, reso=0.15)
            elif gi == 7:
                p.osc(1, "Organ", pos=0.25 + 0.1 * k, level=0.75)
                p.osc(2, "Basic Shapes", pos=SQUARE, level=0.35, oct=1)
                p.filt(1, "lp24", cutoff=1500.0, reso=0.3)
            else:  # 9 melodic techno chords
                p.osc(1, "Hollow", pos=0.3 + 0.1 * k, level=0.7, voices=3, detune=0.15)
                p.osc(2, "Basic Shapes", pos=SAW, level=0.4, voices=2, detune=0.15, fine=5.0)
                p.filt(1, "lp24", cutoff=1200.0, reso=0.3)
                p.lfo(3, "sine", div="4bar")
                p.mod(M(3), "f1_cutoff", 0.15)
            cut = p.p["f1_cutoff"]
            p.env(1, d=[0.25, 0.18, 0.5, 0.3, 0.22, 0.35, 0.3, 0.2, 0.5, 0.6][gi], s=0.1 if gi in (2, 8, 9) else 0.0, curve=0.25)
            p.mod(M(1), "f1_cutoff", cut_amt(cut, min(15000.0, cut * 6)))
            p.mod(VEL, "f1_cutoff", 0.1)
            short = gi in (0, 1, 3, 4, 5, 6, 7)
            p.amp(a=0.002, d=0.5 if short else 1.0, s=0.15 if short else 0.85, r=0.35, level=0.72, vel=0.4)
            if gi == 2:
                p.amp(a=0.12 if k == 1 else 0.005, d=1.0, s=0.85, r=0.4, level=0.72)
                p.env(2, d=0.12, s=0.0)
                p.mod(M(2), "voice_pitch", semis_amt(-2 if k == 2 else 0))
                p.lfo(4, "sine", hz=3.5)
                p.mod(M(4), "osc1_fine", 0.03 if k == 2 else 0)
            if gi in (2, 8):
                p.lfo(5, "pump", div="1/4", bipolar=False)
                p.p["mod5_type"] = 1
                p.p["amp_level"] = 0.3
                p.mod(M(5), "amp_level", 0.55)
            if gi in (2, 3, 8):
                p.hyper(mix=0.35, dim=0.45)
                p.ott(depth=0.35)
            if gi == 3:
                p.dist(drive=0.2, kind="soft", mix=0.6)
            if gi in (0, 1):
                p.chorus(mix=0.3)
            if gi == 5:
                p.rows = [r for r in p.rows if not (r[0] == M(1) and r[1] == "f1_cutoff")]
                p.mod(M(1), "f1_cutoff", cut_amt(cut, cut * 2.5))
                p.delay(mix=0.38, div="1/8D", fb=0.6, tone=0.3)
                p.reverb(mix=0.35, size=0.85, decay=5.5, damp=0.7)
                if k == 4: p.tape(mix=0.8, wow=0.4, flutter=0.3, sat=0.4)
            else:
                p.delay(mix=0.15 if gi != 9 else 0.25, div="1/8D" if gi != 9 else "1/4D", fb=0.35)
                p.reverb(mix=0.22 if gi != 9 else 0.35, size=0.7 if gi != 9 else 0.85, decay=2.5 if gi != 9 else 5.0)
            if gi == 7:
                p.insert(1, "tube", amount=0.3, pos="post")
                p.ott(depth=0.4)
            p.eq(hp=160.0, high=-4.0 if gi == 5 else 0.0)
            p.voice("poly", drift=0.3)
            p.macro(1, "Cutoff", [("f1_cutoff", 0.3)])
            p.macro(2, "Decay", [("mod1_d", 0.3), ("amp_d", 0.25)])
            p.macro(3, "Space", [("fx_rev_mix", 0.3), ("fx_delay_mix", 0.25)])
            p.macro(4, "Resonance", [("f1_reso", 0.35)])


# =====================================================================================================
# ARP / SEQ
# =====================================================================================================
def arp():
    C = "Arp"
    def pitch_seq(p, slot, semis, div="1/16"):
        """Melodic step sequence on Voice Pitch: steps are semitone offsets 0..24."""
        p.perf(slot, [s / 24.0 for s in semis], div=div, smooth=0.0)
        p.mod(M(slot), "voice_pitch", semis_amt(24))

    seqs = [
        [0, 12, 7, 12, 3, 12, 7, 15],
        [0, 3, 7, 10, 12, 10, 7, 3],
        [0, 0, 12, 0, 7, 0, 10, 0],
        [0, 7, 12, 19, 12, 7, 15, 10],
        [0, 12, 0, 12, 5, 17, 3, 15],
        [0, 5, 7, 12, 7, 5, 3, 0],
        [12, 0, 7, 0, 10, 0, 7, 3],
        [0, 3, 0, 5, 0, 7, 0, 10],
        [0, 0, 7, 7, 3, 3, 10, 12],
        [0, 12, 24, 12, 7, 19, 3, 15],
    ]
    groups = [
        (["Afterlife Arp", "Hypnotic Arp", "Ritual Arp", "Midnight Sequence", "Tale Arp"], ["Melodic Techno"] * 5, "melodic"),
        (["Trance Gate", "Gated Strings", "Chopped Pad", "Stutter Gate", "Uplift Gate"], ["Trance", "Trance", "Progressive House", "Trance", "Trance"], "gate"),
        (["Rolling Sequence", "Bass Sequence", "Driving Sequence", "Dark Sequence", "Warehouse Roll"], ["Techno", "Melodic Techno", "Techno", "Techno", "Tech House"], "rolling"),
        (["Glitch Sequence", "Random Bits", "Data Stream", "Broken Circuit", "Chaos Arp"], ["Techno", "Electro House", "Techno", "Melodic Techno", "Techno"], "glitch"),
        (["Pulse Pad Seq", "Breathing Seq", "Pulse Chords", "Throb", "Heartbeat Pad"], ["Progressive House", "Melodic Techno", "House", "Techno", "Trance"], "pulse"),
        (["Bouncy House Seq", "Jackin Seq", "Disco Seq", "Shuffle Seq", "Garage Seq"], ["House", "House", "House", "Tech House", "Deep House"], "bouncy"),
        (["Acid Sequence", "Acid Arp", "303 Pattern", "Squelch Seq", "Acid Rain"], ["Techno", "Techno", "Tech House", "Techno", "Melodic Techno"], "acid"),
        (["Psy Roll", "Triplet Roll", "Goa Sequence", "Forest Roll", "Full On Roll"], ["Trance"] * 5, "psy"),
        (["Octave Arp", "Sparkle Arp", "Crystal Arp", "Future Arp", "Pixel Arp"], ["Future Bass", "Progressive House", "Trance", "Future House", "Future Bass"], "pitched"),
        (["Polyrhythm 5", "Polyrhythm 7", "Odd Meter", "Phase Arp", "Shifting Arp"], ["Melodic Techno", "Techno", "Melodic Techno", "Techno", "Melodic Techno"], "poly"),
    ]
    for gi, (names, genres, kind) in enumerate(groups):
        for k in range(5):
            p = P(names[k], C, genres[k])
            seq = seqs[(gi + k) % len(seqs)]
            if kind in ("melodic", "pitched", "poly"):
                p.osc(1, ["Hollow", "Saw Sweep", "Glass Bell", "Pluck", "Basic Shapes"][k], pos=[0.4, 0.9, 0.4, 1.0, SAW][k], level=0.75, voices=2, detune=0.12)
                p.filt(1, "lp24", cutoff=1100.0, reso=0.3)
                p.env(1, d=0.15, s=0.0)
                p.mod(M(1), "f1_cutoff", cut_amt(1100.0, 7000.0))
                if p.p["osc1_table"] == T["Pluck"]:
                    p.mod(M(1), "osc1_pos", -0.85)
                if kind == "poly":
                    lens = [5, 7, 3, 9, 11][k]
                    p.perf(5, [1.0 if j % 2 == 0 else 0.35 for j in range(lens)], div="1/16", smooth=0.05)
                    p.mod(M(5), "f1_cutoff", 0.2)
                    pitch_seq(p, 6, seq[:[4, 6, 5, 8, 7][k]], div="1/16")
                else:
                    pitch_seq(p, 5, seq if kind == "melodic" else [s if s % 12 == 0 else (s // 12) * 12 for s in seq], div="1/16")
                    p.perf(6, [1, 0.7, 0.9, 0.6, 1, 0.75, 0.85, 0.65], div="1/16", smooth=0.1)
                    p.mod(M(6), "f1_cutoff", 0.1)
                # per-step pluck: a synced falling saw retriggers volume + filter on every 16th
                p.lfo(4, "down", div="1/16", bipolar=False)
                p.mod(M(4), "amp_level", 0.7)
                p.mod(M(4), "f1_cutoff", 0.22)
                p.amp(a=0.002, d=1.0, s=1.0, r=0.2, level=0.12)
                p.delay(mix=0.25, div="1/8D" if kind != "pitched" else "1/16", fb=0.45, tone=0.45)
                p.reverb(mix=0.3, size=0.8, decay=4.0)
            elif kind == "gate":
                p.osc(1, "Basic Shapes" if k != 1 else "Strings", pos=SAW if k != 1 else 0.5, level=0.72, voices=7, detune=0.38, spread=0.95)
                p.osc(2, "Basic Shapes", pos=SAW, level=0.35, oct=1, voices=3, detune=0.25)
                p.filt(1, "lp24", cutoff=5500.0, reso=0.12)
                pats = [[1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0], [1, 1, 0, 1, 1, 0, 1, 0], [1, 0, 0, 1, 0, 0, 1, 0], [1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1], [1, 0, 1, 0, 1, 1, 0, 1]]
                p.perf(5, pats[k], div="1/16" if k != 2 else "1/8", smooth=0.04)
                p.amp(a=0.01, d=1.0, s=1.0, r=0.4, level=0.0)
                p.mod(M(5), "amp_level", 0.8)
                p.hyper(mix=0.3, dim=0.4)
                p.delay(mix=0.15, div="1/8D")
                p.reverb(mix=0.28, size=0.8, decay=4.0)
            elif kind == "rolling":
                p.osc(1, ["Saw Sweep", "Basic Shapes", "Dirt Saw", "Phase Saw", "Square Sweep"][k], pos=[0.9, SAW, 0.4, 0.3, 0.9][k], level=0.8, voices=2, detune=0.08)
                p.sub(level=0.35)
                p.filt(1, "lp24", cutoff=400.0, reso=0.35, drive=0.2)
                p.perf(5, [1, 0.4, 0.7, 0.2, 0.9, 0.3, 0.6, 0.1, 1, 0.5, 0.75, 0.25, 0.95, 0.35, 0.65, 0.15], div="1/16", smooth=0.03)
                p.mod(M(5), "f1_cutoff", cut_amt(400.0, 2600.0))
                p.perf(6, [1, 0.6, 0.8, 0.6] * 4, div="1/16", smooth=0.01)
                p.p["amp_level"] = 0.35
                p.mod(M(6), "amp_level", 0.5)
                p.amp(a=0.001, d=1.0, s=1.0, r=0.1, level=0.35)
                p.voice("mono")
                p.reverb(mix=0.12, size=0.5, decay=1.5)
                bass_fx(p, ott=0.25, mono=150.0)
            elif kind == "glitch":
                p.osc(1, ["Bit Saw", "Digital Spectra", "Noise Spectra", "Metallic", "Fidget"][k], pos=0.5, level=0.75)
                p.filt(1, "bp" if k % 2 else "lp24", cutoff=1500.0, reso=0.4)
                p.rnd(7, div="1/16", smooth=0.0)
                p.mod(M(7), "osc1_pos", 0.8)
                p.mod(M(7), "f1_cutoff", 0.3)
                p.rnd(8, div="1/32" if k % 2 else "1/16", smooth=0.0, bipolar=False)
                p.p["mod8_type"] = 3
                p.mod(M(8), "voice_pitch", semis_amt(12) if k != 2 else semis_amt(24))
                p.insert(1, "crush", amount=0.4, pos="post", mix=0.7)
                p.insert(2, "down", amount=0.3, pos="post", mix=0.5)
                p.amp(a=0.001, d=0.4, s=0.6, r=0.1, level=0.7)
                p.delay(mix=0.25, div="1/16", fb=0.5)
                p.reverb(mix=0.2)
            elif kind == "pulse":
                p.osc(1, ["Warm Spectra", "Strings", "Basic Shapes", "Saw Sweep", "Choir"][k], pos=[0.4, 0.5, SAW, 0.8, 0.5][k], level=0.7, voices=4, detune=0.25, spread=0.9)
                p.filt(1, "lp24", cutoff=1800.0, reso=0.2)
                p.perf(5, [1, 0.25, 0.6, 0.25] * 4, div=["1/8", "1/4", "1/16", "1/8", "1/8D"][k], smooth=0.35)
                p.mod(M(5), "f1_cutoff", 0.22)
                p.p["amp_level"] = 0.3
                p.mod(M(5), "amp_level", 0.5)
                p.amp(a=0.2, d=1.0, s=1.0, r=1.5, level=0.3)
                p.reverb(mix=0.35, size=0.85, decay=5.0)
                p.chorus(mix=0.25)
            elif kind == "bouncy":
                p.osc(1, ["Organ", "Basic Shapes", "Square Sweep", "Sine Harmonics", "FM Bell"][k], pos=[0.3, SAW, 0.8, 0.1, 0.2][k], level=0.75, voices=2, detune=0.1)
                p.filt(1, "lp24", cutoff=900.0, reso=0.3)
                p.perf(5, [1, 0, 0.6, 1, 0, 0.8, 0.4, 0], div="1/16", smooth=0.02)
                p.mod(M(5), "amp_level", 0.6)
                p.p["amp_level"] = 0.2
                pitch_seq(p, 6, [0, 0, 12, 0, 7, 0, 12, 10], div="1/8")
                p.env(1, d=0.12, s=0.0)
                p.mod(M(1), "f1_cutoff", cut_amt(900.0, 6000.0))
                p.amp(a=0.001, d=0.4, s=0.6, r=0.1, level=0.2)
                p.delay(mix=0.15, div="1/8D")
                p.reverb(mix=0.18)
            elif kind == "acid":
                p.osc(1, "Basic Shapes", pos=SAW if k % 2 == 0 else SQUARE, level=0.8)
                p.filt(1, "lp24", cutoff=350.0, reso=0.72 + 0.03 * k, drive=0.3)
                p.perf(5, [1, 0.2, 0.6, 0.9, 0.1, 0.5, 1, 0.3, 0.7, 0.2, 0.8, 0.4, 1, 0.1, 0.6, 0.9], div="1/16", smooth=0.05)
                p.mod(M(5), "f1_cutoff", cut_amt(350.0, 3500.0))
                pitch_seq(p, 6, [0, 0, 12, 0, 3, 0, 15, 12, 0, 7, 0, 10, 0, 12, 3, 0][: [16, 8, 12, 16, 8][k]], div="1/16")
                p.amp(a=0.001, d=1.0, s=1.0, r=0.08, level=0.8)
                p.voice("mono")
                p.dist(drive=0.35, kind="tube")
                p.delay(mix=0.18, div="1/8D", fb=0.4)
                p.stereo(bassmono=120.0)
            else:  # psy
                p.osc(1, "Basic Shapes", pos=SAW, level=0.8)
                p.sub(level=0.4)
                p.filt(1, "lp24", cutoff=500.0, reso=0.4, drive=0.2)
                p.perf(5, [0, 1, 1, 1] * 4 if k != 1 else [0, 1, 1] * 4, div="1/16" if k != 1 else "1/16T", smooth=0.01)
                p.p["amp_level"] = 0.0
                p.mod(M(5), "amp_level", 0.85)
                p.env(1, d=0.08, s=0.0)
                p.lfo(3, "sine", div="1bar")
                p.mod(M(5), "f1_cutoff", 0.12)
                p.mod(M(3), "f1_cutoff", 0.1)
                p.amp(a=0.001, d=1.0, s=1.0, r=0.05, level=0.0)
                p.voice("mono")
                bass_fx(p, ott=0.3, mono=160.0)
            p.eq(hp=60.0 if kind in ("rolling", "acid", "psy") else 150.0)
            p.voice(p.p.get("voice_mode") and ["poly", "mono", "legato"][p.p["voice_mode"]] or "poly", drift=0.2)
            p.macro(1, "Cutoff", [("f1_cutoff", 0.3)])
            p.macro(2, "Resonance", [("f1_reso", 0.35)])
            p.macro(3, "Space", [(x, 0.3) for x in ("fx_rev_mix", "fx_delay_mix") if p.p.get(x.replace("_mix", "_on").replace("rev_on", "rev_on"))] or [("f1_drive", 0.3)])
            p.macro(4, "Drive", [("f1_drive", 0.5)])


# =====================================================================================================
# FX
# =====================================================================================================
def fx():
    C = "FX"
    groups = [
        (["White Noise Riser", "Big Riser", "Tension Build", "Sweep Up", "Snare Roll Riser"], ["Festival EDM", "Festival EDM", "Trance", "Progressive House", "Festival EDM"], "riser"),
        (["Downlifter", "Noise Fall", "Drop Down", "Exhale", "Sink"], ["Festival EDM", "Techno", "Electro House", "Melodic Techno", "Tech House"], "down"),
        (["Laser Zap", "Pew Pew", "Electro Laser", "Retro Blip", "Sci Fi Zap"], ["Electro House", "Electro House", "Bass House", "Future House", "Techno"], "laser"),
        (["Siren", "Air Raid", "Police Siren", "Rave Siren", "Alarm"], ["Festival EDM", "Techno", "Bass House", "Techno", "Dubstep"], "siren"),
        (["Space Drone", "Deep Space", "Dark Matter", "Void", "Interstellar"], ["Melodic Techno", "Techno", "Techno", "Melodic Techno", "Trance"], "drone"),
        (["Wind Sweep", "Storm", "Ocean Waves", "Desert Wind", "Breath Sweep"], ["Melodic Techno", "Techno", "Deep House", "Melodic Techno", "Progressive House"], "wind"),
        (["Impact Boom", "Sub Drop", "Cinematic Hit", "Thunder", "Earthquake"], ["Festival EDM", "Dubstep", "Melodic Techno", "Techno", "Festival EDM"], "impact"),
        (["Hoover Rise", "Detuned Rise", "Pitch Climb", "Screaming Rise", "Endless Rise"], ["Techno", "Festival EDM", "Trance", "Electro House", "Trance"], "pitchrise"),
        (["Glitch FX", "Data Burst", "Computer Talk", "Robot Babble", "Bit Rain"], ["Techno", "Electro House", "Techno", "Electro House", "Melodic Techno"], "glitch"),
        (["Shimmer Texture", "Granular Cloud", "Crystal Rain", "Spectral Wash", "Stardust"], ["Melodic Techno", "Melodic Techno", "Trance", "Progressive House", "Future Bass"], "texture"),
    ]
    for gi, (names, genres, kind) in enumerate(groups):
        for k in range(5):
            p = P(names[k], C, genres[k])
            if kind == "riser":
                p.noise(level=0.6, kind="white", color=0.6)
                p.osc(1, "Sync Saw" if k != 4 else "Basic Shapes", pos=0.0, level=0.45 if k != 0 else 0.0, voices=5, detune=0.4)
                p.filt(1, "lp24" if k != 3 else "bp", cutoff=250.0, reso=0.35)
                p.env(1, a=[6.0, 8.0, 4.0, 5.0, 4.0][k], d=0.1, s=1.0, r=2.0, curve=0.5)
                p.mod(M(1), "f1_cutoff", cut_amt(250.0, 14000.0))
                p.mod(M(1), "osc1_pos", 1.0)
                p.mod(M(1), "voice_pitch", semis_amt(24 if k != 0 else 0))
                if k == 4:
                    p.lfo(3, "square", div="1/16", retrig=True, bipolar=True)
                    p.mod(M(3), "amp_level", 0.3)
                p.amp(a=3.0, d=0.1, s=1.0, r=2.5, level=0.75)
                p.hyper(mix=0.4)
                p.reverb(mix=0.4, size=0.9, decay=6.0)
                p.delay(mix=0.2, div="1/8")
            elif kind == "down":
                p.noise(level=0.5, kind="white" if k != 3 else "pink", color=0.55)
                p.osc(1, "Basic Shapes", pos=SAW, level=0.4, voices=4, detune=0.3)
                p.filt(1, "lp24", cutoff=16000.0, reso=0.25)
                p.env(1, a=0.0, d=[3.0, 2.5, 1.5, 4.0, 2.0][k], s=0.0, curve=0.5)
                p.p["f1_cutoff"] = 250.0
                p.mod(M(1), "f1_cutoff", cut_amt(250.0, 16000.0))
                p.mod(M(1), "voice_pitch", semis_amt(24))
                p.amp(a=0.005, d=4.0, s=0.0, r=2.0, level=0.75)
                p.reverb(mix=0.45, size=0.9, decay=6.0)
            elif kind == "laser":
                p.osc(1, ["Basic Shapes", "Basic Shapes", "Squeal", "PWM Square", "Sync Saw"][k], pos=[SAW, SQUARE, 0.5, 0.4, 0.5][k], level=0.75,
                      mode="sync" if k in (0, 4) else "std", warp=0.5)
                p.env(1, d=[0.18, 0.12, 0.3, 0.08, 0.4][k], s=0.0, curve=0.1)
                p.mod(M(1), "voice_pitch", semis_amt([36, 24, 30, 12, 48][k]))
                p.mod(M(1), "osc1_warp", 0.4)
                p.filt(1, "lp12", cutoff=9000.0, reso=0.2)
                p.amp(a=0.001, d=0.4, s=0.0, r=0.2, level=0.75)
                p.delay(mix=0.3, div="1/16", fb=0.45)
                p.reverb(mix=0.2)
            elif kind == "siren":
                p.osc(1, ["Basic Shapes", "Basic Shapes", "Basic Shapes", "Saw Sweep", "PWM Square"][k], pos=[SINE + 0.1, SAW, SQUARE, 1.0, 0.4][k], level=0.75, voices=2, detune=0.1)
                p.lfo(3, ["tri", "sine", "square", "up", "square"][k], hz=[0.35, 0.18, 1.2, 2.0, 4.0][k], retrig=True)
                p.mod(M(3), "voice_pitch", semis_amt(7 if k != 1 else 12))
                p.filt(1, "lp12", cutoff=6000.0, reso=0.1)
                p.amp(a=0.05, d=1.0, s=1.0, r=0.6, level=0.7)
                p.reverb(mix=0.3, size=0.85, decay=4.0)
                p.delay(mix=0.15, div="1/8D")
            elif kind == "drone":
                p.osc(1, ["Warm Spectra", "Noise Spectra", "Metallic", "Hollow", "Choir"][k], pos=0.4, level=0.7, voices=4, detune=0.3, spread=1.0)
                p.osc(2, "Basic Shapes", pos=SINE, level=0.45, oct=-1)
                p.noise(level=0.1, kind="pink", color=0.3)
                p.filt(1, "lp24", cutoff=1100.0, reso=0.3)
                p.rnd(7, hz=0.15, smooth=1.0)
                p.lfo(3, "sine", hz=0.06)
                p.mod(M(7), "osc1_pos", 0.5)
                p.mod(M(3), "f1_cutoff", 0.12)
                p.amp(a=2.0, d=2.0, s=1.0, r=5.0, level=0.7)
                p.reverb(mix=0.5, size=1.0, decay=12.0, damp=0.5)
                p.delay(mix=0.2, div="1/4D", fb=0.6, tone=0.3)
                p.stereo(width=1.5)
            elif kind == "wind":
                p.noise(level=0.7, kind="pink" if k != 1 else "white", color=0.5)
                p.p["osc1_on"] = 0
                p.filt(1, "bp", cutoff=900.0, reso=0.55)
                p.lfo(3, "smooth", hz=[0.25, 0.6, 0.12, 0.3, 0.2][k])
                p.mod(M(3), "f1_cutoff", 0.3)
                p.lfo(4, "sine", hz=0.17)
                p.mod(M(4), "amp_pan", 0.6)
                p.amp(a=1.5, d=1.0, s=1.0, r=3.0, level=0.8)
                p.reverb(mix=0.4, size=0.9, decay=6.0)
            elif kind == "impact":
                p.osc(1, "Basic Shapes", pos=SINE, level=0.85)
                p.noise(level=0.5, kind="white", color=0.4)
                p.env(1, d=[0.6, 1.5, 0.4, 0.8, 2.0][k], s=0.0, curve=0.4)
                p.mod(M(1), "voice_pitch", semis_amt(-0.0) if False else semis_amt(12))
                p.env(2, d=0.25, s=0.0, curve=0.2)
                p.mod(M(2), "noise_level", 0.5)
                p.p["noise_level"] = 0.0
                p.filt(1, "lp24", cutoff=3500.0, reso=0.1)
                p.mod(M(2), "f1_cutoff", 0.2)
                p.amp(a=0.001, d=[2.0, 3.0, 1.5, 3.5, 4.0][k], s=0.0, r=2.0, level=0.85)
                p.p["voice_pitch"] = -12
                p.insert(1, "tube", amount=0.4, pos="post")
                p.reverb(mix=0.45, size=1.0, decay=7.0, damp=0.6)
            elif kind == "pitchrise":
                p.osc(1, "PWM Square" if k == 0 else "Basic Shapes", pos=0.35 if k == 0 else SAW, level=0.7, voices=7, detune=0.5, spread=0.95)
                p.env(1, a=[6.0, 8.0, 5.0, 4.0, 10.0][k], d=0.1, s=1.0, curve=0.5)
                p.mod(M(1), "voice_pitch", semis_amt([24, 12, 36, 24, 48][k]))
                p.mod(M(1), "osc1_detune", 0.4)
                p.filt(1, "lp24", cutoff=1500.0, reso=0.2)
                p.mod(M(1), "f1_cutoff", cut_amt(1500.0, 14000.0))
                p.amp(a=1.0, d=1.0, s=1.0, r=1.5, level=0.7)
                p.chorus(mix=0.35)
                p.reverb(mix=0.35, size=0.9, decay=5.0)
            elif kind == "glitch":
                p.osc(1, ["Bit Saw", "Digital Spectra", "Talk Box", "Formant Vowels", "Noise Spectra"][k], pos=0.5, level=0.7)
                p.rnd(7, div="1/32", smooth=0.0)
                p.rnd(8, div="1/16", smooth=0.0, bipolar=False)
                p.p["mod8_type"] = 3
                p.mod(M(7), "osc1_pos", 0.9)
                p.mod(M(8), "voice_pitch", semis_amt(24))
                p.filt(1, "bp", cutoff=1800.0, reso=0.5)
                p.mod(M(7), "f1_cutoff", 0.3)
                p.insert(1, "crush", amount=0.5, pos="post")
                p.amp(a=0.001, d=0.5, s=0.8, r=0.2, level=0.7)
                p.delay(mix=0.3, div="1/16", fb=0.55)
                p.reverb(mix=0.25)
            else:  # texture
                p.osc(1, ["Glass Bell", "Noise Spectra", "Glass Bell", "Digital Spectra", "Spectral Comb"][k], pos=0.5, level=0.65, voices=4, detune=0.2, spread=1.0)
                p.osc(2, "Sine Harmonics", pos=0.3, level=0.35, oct=1)
                p.rnd(7, div="1/16", smooth=0.3)
                p.mod(M(7), "osc1_pos", 0.6)
                p.mod(M(7), "amp_pan", 0.5)
                p.filt(1, "hp12", cutoff=400.0, reso=0.1)
                p.amp(a=0.8, d=1.0, s=1.0, r=4.0, level=0.7)
                p.delay(mix=0.35, div="1/8D", fb=0.6, tone=0.6)
                p.reverb(mix=0.55, size=1.0, decay=10.0)
                p.chorus(mix=0.3)
            p.voice("poly" if kind not in ("siren", "laser") else "mono", drift=0.25)
            p.macro(1, "Filter", [("f1_cutoff", 0.3)])
            p.macro(2, "Space", [(x, 0.3) for x in ("fx_rev_mix", "fx_delay_mix") if p.p.get(x.replace("_mix", "_on"))] or [("amp_r", 0.3)])
            p.macro(3, "Pitch", [("voice_pitch", 0.125)])
            p.macro(4, "Length", [("amp_r", 0.25)])


# =====================================================================================================
def build(gains=None):
    ALL.clear()
    init = P("Init", "Init", "Init")
    init.osc(1, "Basic Shapes", pos=SAW, level=0.75)
    init.filt(1, "lp24", cutoff=12000.0, reso=0.1)
    init.amp()
    for fn in (bass, lead, pad, pluck, keys, chord, arp, fx):
        fn()
    names = [p.name for p in ALL]
    dup = {n for n in names if names.count(n) > 1}
    assert not dup, f"duplicate names: {dup}"
    cats = {}
    for p in ALL:
        cats[p.category] = cats.get(p.category, 0) + 1
    if gains:
        for p in ALL:
            if p.name in gains:
                p.vol = gains[p.name]
    return ALL, cats


if __name__ == "__main__":
    gains = None
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        gains = json.load(open(sys.argv[1]))
    presets, cats = build(gains)
    emit_cpp(presets, os.path.join(ROOT, "Source", "FactoryPresets.cpp"))
    print("presets:", len(presets), cats)
