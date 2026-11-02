#pragma once
#include "Params.h"
#include "Wavetable.h"
#include "Filters.h"
#include "Fx.h"

namespace lxl
{
// Destination indices: must match the order in modDests() (checked at runtime).
namespace D
{
enum : int
{
    None = 0,
    OscBase = 1, OscStride = 9,
    Semi = 0, Fine, Pos, Warp, Level, Pan, Detune, Fm, Ring,
    SubLevel = 19, NoiseLevel, NoiseColor,
    FilBase = 22, FilStride = 4,
    Cutoff = 0, Reso, Drive, Mix,
    Feedback = 30, Ins1 = 31, Ins2 = 32, AmpLevel = 33, AmpPan = 34, VoicePitch = 35,
    GlobalStart = 36,
    DistDrive = 36, ChorusMix, PhaserMix, DelayMix, DelayFb, RevMix, RevSize, TapeMix, HyperMix, OttDepth, Width, EqHigh,
    AmpA, AmpD, AmpR, Mod1Decay, Mod2Decay,
    Count = 53
};
inline int osc (int o, int which) { return OscBase + o * OscStride + which; }
inline int fil (int f, int which) { return FilBase + f * FilStride + which; }
} // namespace D

struct OscSnap   { bool on; int mode, oct, voices, route; float spread, blend, fmratio, phase, rndphase; };
struct FilSnap   { bool on; int type; float key; };
struct InsSnap   { int type, pos; float mix; };
struct SlotSnap  { int type, div, shape, len; float rate, phase, smooth, a, d, s, r, curve; bool sync, retrig, bipolar; float steps[kNumSteps]; };

struct Snapshot
{
    OscSnap osc[kNumOsc];
    bool subOn; int subOct, subShape, subRoute;
    bool noiseOn; int noiseType, noiseRoute;
    FilSnap fil[kNumFilters];
    int filterStruct;
    InsSnap ins[kNumInserts];
    float ampA, ampD, ampS, ampR, ampCurve, ampVel;
    int poly, voiceMode, pbRange;
    float glide, masterVol, drift;
    SlotSnap slot[kNumModSlots];
    float macro[kNumMacros];
    // FX
    bool distOn, chorusOn, phaserOn, delayOn, revOn, tapeOn, clipOn, delaySync, delayPP;
    int distType, delayDiv;
    float distTone, distMix, chorusRate, chorusDepth, phaserRate, phaserDepth, phaserFb, delayTime, delayTone,
          revDecay, revDamp, revPre, revWidth, tapeWow, tapeFlutter, tapeSat;
    bool hyperOn, ottOn, eqOn;
    float hyperRate, hyperDetune, hyperDim, ottTime, ottUp, ottDown, ottGain, eqHp, eqLow, eqMidF, eqMid, bassMono;
};

struct ModRow { int src, dst; float amt; };

/** Per-block info about each modulation slot shared by all voices. */
struct SlotGlobal
{
    double absStart = 0.0;   // free-running phase (in cycles / steps) at block start
    double incPerSample = 0.0;
};

struct EnvCoefs
{
    float aCoef = 0, aBase = 0, dCoef = 0, dBase = 0, rCoef = 0, rBase = 0, sustain = 0;
    float aCoefN = 0, dCoefN = 0, rCoefN = 0; // coef ^ kSubBlock
    void compute (float a, float d, float s, float r, float curve, float fs);
};

struct EnvState
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0.0f;
    void gateOn() { stage = Attack; }
    void gateOff() { if (stage != Idle) stage = Release; }
    inline float tick (const EnvCoefs& c) noexcept;
    inline void advance (const EnvCoefs& c, int n) noexcept;
};

class Engine;

class Voice
{
public:
    void prepare (double maxSr);
    void setSampleRate (double sr);
    void start (int note, float vel, float glideFromPitch, bool glideOn, const Snapshot& sp, bool legatoOnly);
    void release();
    void kill() { active = false; amp.stage = EnvState::Idle; amp.level = 0.0f; }
    void render (Engine& e, const Snapshot& sp, int blockOffset, float* outL, float* outR, int n);

    bool active = false, gate = false, sustained = false;
    int note = 60;
    float velocity = 1.0f, polyAT = 0.0f;
    uint64_t age = 0;
    float pitchCur = 60.0f, pitchTarget = 60.0f;
    float lastOffsets[D::Count] {};
    float slotOut[kNumModSlots] {};
    int perfStep[kNumModSlots] {};
    float curDest[D::Count] {};
    EnvState amp;

private:
    void renderSub (Engine& e, const Snapshot& sp, int blockOffset, float* outL, float* outR, int n);

    float fs = 44100.0f;
    bool fresh = true;
    struct OscRt
    {
        float phase[kMaxUnison] {}, mphase[kMaxUnison] {};
        float gL[kMaxUnison] {}, gR[kMaxUnison] {}, ratio[kMaxUnison] {};
        float prevInc = 0.0f;
    } osc[kNumOsc];
    float subPhase = 0.0f;
    float noiseCrackle[2] {}, pink[2][7] {}, noiseTilt[2] {}, noiseHp[2] {}, metalPhase[6] {};
    FilterChannel filt[kNumFilters][2];
    float fbL = 0.0f, fbR = 0.0f;
    float dsHold[4][2] {}, dsCount[4] {};
    EnvState env[kNumModSlots];
    double slotAbs[kNumModSlots] {};
    float slotPrevVal[kNumModSlots] {};
    float voiceRand = 0.0f;
    float driftOff[kNumOsc][kMaxUnison] {};
    float driftWalk = 0.0f, driftTarget = 0.0f, driftCut = 0.0f;
    int driftTimer = 0;
    uint32_t seed = 1;
    Rng rng;
    float stepD[D::Count] {};
};

class Engine
{
public:
    explicit Engine (juce::AudioProcessorValueTreeState& state);
    void prepare (double sr, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const HostInfo& host);

    std::atomic<const Wavetable*> tables[kNumOsc];

    // ---- UI feedback ----
    std::array<std::atomic<float>, D::Count> modDisplay {};
    std::array<std::atomic<float>, kNumModSlots> slotDisplay {};
    std::array<std::atomic<int>, kNumModSlots> stepDisplay {};
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    // ---- used by voices ----
    float baseNorm[D::Count] {};
    juce::NormalisableRange<float> destRange[D::Count];
    std::vector<ModRow> voiceRows;
    float globalSrc[kNumSources] {};
    SlotGlobal slotGlobal[kNumModSlots];
    EnvCoefs ampCoefs, slotCoefs[kNumModSlots];
    float pitchBend = 0.0f, modWheel = 0.0f, chanPressure = 0.0f;
    double sampleRate = 44100.0;
    uint64_t noteCounter = 0;

private:
    void readSnapshot();
    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void renderVoices (int start, int n);
    void applyFx (int n);

    juce::AudioProcessorValueTreeState& apvts;
    Snapshot snap {};
    juce::RangedAudioParameter* destParam[D::Count] {};
    std::unordered_map<std::string, std::atomic<float>*> raw;
    std::atomic<float>* p (const juce::String& id);

    std::array<Voice, kMaxVoices> voices;
    std::vector<int> heldNotes;
    bool sustainPedal = false;
    float lastPitch = 60.0f;

    juce::AudioBuffer<float> mix, osBuf;
    int osFactor = 1;

    /** 2x halfband decimator (63-tap windowed sinc). */
    struct Decimator
    {
        static constexpr int kTaps = 63;
        float h[kTaps] {};
        float hist[2][kTaps * 2] {};
        int pos = 0;
        void init();
        void reset() { for (auto& c : hist) for (auto& v : c) v = 0.0f; pos = 0; }
        void process (const float* inL, const float* inR, float* outL, float* outR, int nOut);
    } decimator;
    double internalPpq = 0.0;
    double freePhase[kNumModSlots] {};
    float globalVal[D::Count] {};

    Distortion dist; Chorus chorus; Phaser phaser; StereoDelay delay; FdnReverb reverb; TapeWobble tape;
    Hyper hyper; Ott ott; Eq eq; StereoUtility utility;
    HostInfo hostInfo;
    float smoothedVol = 0.0f, dcL = 0.0f, dcR = 0.0f;
};
} // namespace lxl
