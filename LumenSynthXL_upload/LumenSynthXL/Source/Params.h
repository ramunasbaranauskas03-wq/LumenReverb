#pragma once
#include "Common.h"

namespace lxl
{
// ---------- parameter ID helpers ----------
inline juce::String oscId (int i, const char* n)   { return "osc" + juce::String (i + 1) + "_" + n; }
inline juce::String filId (int i, const char* n)   { return "f" + juce::String (i + 1) + "_" + n; }
inline juce::String insId (int i, const char* n)   { return "ins" + juce::String (i + 1) + "_" + n; }
inline juce::String modId (int s, const char* n)   { return "mod" + juce::String (s + 1) + "_" + n; }
inline juce::String stepId (int s, int k)          { return "mod" + juce::String (s + 1) + "_step" + juce::String (k + 1); }
inline juce::String mtxId (int r, const char* n)   { return "mtx" + juce::String (r + 1) + "_" + n; }
inline juce::String macroId (int m)                { return "macro" + juce::String (m + 1); }

// ---------- choice lists ----------
const juce::StringArray& oscModeNames();
const juce::StringArray& filterTypeNames();
const juce::StringArray& insertTypeNames();
const juce::StringArray& noiseTypeNames();
const juce::StringArray& subShapeNames();
const juce::StringArray& routeNames();
const juce::StringArray& modTypeNames();
const juce::StringArray& lfoShapeNames();
const juce::StringArray& syncDivNames();
double syncDivBeats (int index);
const juce::StringArray& distTypeNames();
const juce::StringArray& tableChoiceNames(); // factory tables + "User Import"

enum OscMode { ModeStandard, ModeBendPlus, ModeBendMinus, ModeMirror, ModePWM, ModeSync, ModeFormant, ModeJump, ModeQuantize, ModeFold, kNumOscModes };
enum FilterType { FLp12, FLp24, FAnalogLp, FHp12, FHp24, FBand, FNotch, FDoubleNotch, FCombPlus, FCombMinus, FFormant, kNumFilterTypes };
enum InsertType { InsOff, InsDrive, InsTube, InsFold, InsRectify, InsBitcrush, InsDownsample, InsHardClip, kNumInsertTypes };
enum Route { RouteA, RouteB, RouteAB, RouteDirect };
enum ModType { ModEnv, ModLfo, ModPerformer, ModRandom };
enum LfoShape { LfoSine, LfoTri, LfoSawUp, LfoSawDown, LfoSquare, LfoPump, LfoStairs, LfoSmoothRandom, kNumLfoShapes };

// ---------- modulation sources ----------
enum ModSource
{
    SrcNone = 0,
    SrcMod1,            // 1..9
    SrcVelocity = SrcMod1 + kNumModSlots,
    SrcKeyTrack,
    SrcAftertouch,
    SrcModWheel,
    SrcPitchBend,
    SrcMacro1,          // .. +7
    SrcVoiceRandom = SrcMacro1 + kNumMacros,
    SrcAmpEnv,
    kNumSources
};
const juce::StringArray& sourceNames();
inline bool isGlobalSource (int s) { return s == SrcModWheel || s == SrcPitchBend || (s >= SrcMacro1 && s < SrcMacro1 + kNumMacros); }

// ---------- modulation destinations ----------
struct ModDest
{
    juce::String paramId;
    juce::String name;
    bool global = false;
};
/** Index 0 is "None". */
const std::vector<ModDest>& modDests();
int numModDests();
int destIndexForParam (const juce::String& paramId); // -1 if not modulatable
juce::StringArray destNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Parameters that the Mutator must not touch. */
bool isMutationExcluded (const juce::String& paramId);
} // namespace lxl
