#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

using namespace lxl;

LumenSynthXLProcessor::LumenSynthXLProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "LumenSynthXL", createParameterLayout()),
      engine (apvts),
      presets (apvts)
{
    presets.applyFullState = [this] (const juce::ValueTree& t) { applyFullState (t); };
    presets.onFactoryLoaded = [this] (const juce::StringArray& names) { setMacroNames (names); refreshTables(); };
    refreshTables();
    startTimerHz (20);
}

LumenSynthXLProcessor::~LumenSynthXLProcessor() { stopTimer(); }

bool LumenSynthXLProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void LumenSynthXLProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    refreshTables();
    engine.prepare (sampleRate, samplesPerBlock);
}

void LumenSynthXLProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    HostInfo info;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) info.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) info.ppq = *ppq;
            info.playing = pos->getIsPlaying();
        }

    engine.process (buffer, midi, info);
}

juce::AudioProcessorEditor* LumenSynthXLProcessor::createEditor()
{
    return new lxl::PluginEditor (*this);
}

// ------------------------------------------------------------------ wavetables
const Wavetable* LumenSynthXLProcessor::factoryTable (int index)
{
    const juce::ScopedLock sl (tableLock);
    index = juce::jlimit (0, WavetableFactory::numFactoryTables() - 1, index);
    if (auto it = factoryCache.find (index); it != factoryCache.end()) return it->second;
    tableStore.push_back (WavetableFactory::make (index));
    const Wavetable* t = tableStore.back().get();
    factoryCache[index] = t;
    return t;
}

void LumenSynthXLProcessor::refreshTables()
{
    for (int o = 0; o < kNumOsc; ++o)
    {
        const int choice = (int) std::lround (paramValue (oscId (o, "table")));
        const Wavetable* t = nullptr;
        if (choice >= WavetableFactory::numFactoryTables())
            t = imported[o] != nullptr ? imported[o] : factoryTable (0);
        else
            t = factoryTable (choice);

        if (t != loadedPtr[o])
        {
            loadedPtr[o] = t;
            loadedChoice[o] = choice;
            engine.tables[o].store (t, std::memory_order_release);
        }
    }
}

void LumenSynthXLProcessor::timerCallback() { refreshTables(); }

juce::String LumenSynthXLProcessor::importWavetable (int osc, const juce::File& file)
{
    juce::String error;
    auto wt = WavetableFactory::fromFile (file, error);
    if (wt == nullptr) return error.isEmpty() ? juce::String ("Could not load file.") : error;
    {
        const juce::ScopedLock sl (tableLock);
        tableStore.push_back (std::move (wt));
        imported[osc] = tableStore.back().get();
    }
    loadedPtr[osc] = nullptr; // force refresh
    if (auto* p = apvts.getParameter (oscId (osc, "table")))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) WavetableFactory::numFactoryTables()));
    refreshTables();
    return {};
}

// ------------------------------------------------------------------ modulation helpers
static void setParam (juce::AudioProcessorValueTreeState& s, const juce::String& id, float plain)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

int LumenSynthXLProcessor::assignModulation (int src, int destIndex, float amount)
{
    if (src <= 0 || destIndex <= 0) return -1;
    for (int r = 0; r < kNumMatrix; ++r)
        if ((int) std::lround (paramValue (mtxId (r, "src"))) == src && (int) std::lround (paramValue (mtxId (r, "dst"))) == destIndex)
            return r;
    for (int r = 0; r < kNumMatrix; ++r)
    {
        const int s = (int) std::lround (paramValue (mtxId (r, "src")));
        const int d = (int) std::lround (paramValue (mtxId (r, "dst")));
        if (s == 0 || d == 0)
        {
            undoManager.beginNewTransaction ("Assign modulation");
            setParam (apvts, mtxId (r, "src"), (float) src);
            setParam (apvts, mtxId (r, "dst"), (float) destIndex);
            setParam (apvts, mtxId (r, "amt"), amount);
            return r;
        }
    }
    return -1;
}

void LumenSynthXLProcessor::removeModulation (int destIndex, int src)
{
    undoManager.beginNewTransaction ("Remove modulation");
    for (int r = 0; r < kNumMatrix; ++r)
    {
        const int s = (int) std::lround (paramValue (mtxId (r, "src")));
        const int d = (int) std::lround (paramValue (mtxId (r, "dst")));
        if (d == destIndex && (src < 0 || s == src))
        {
            setParam (apvts, mtxId (r, "src"), 0.0f);
            setParam (apvts, mtxId (r, "dst"), 0.0f);
            setParam (apvts, mtxId (r, "amt"), 0.0f);
        }
    }
}

juce::Array<int> LumenSynthXLProcessor::rowsForDest (int destIndex) const
{
    juce::Array<int> rows;
    for (int r = 0; r < kNumMatrix; ++r)
        if ((int) std::lround (paramValue (mtxId (r, "dst"))) == destIndex && (int) std::lround (paramValue (mtxId (r, "src"))) > 0)
            rows.add (r);
    return rows;
}

// ------------------------------------------------------------------ mutator
void LumenSynthXLProcessor::mutate (float amount)
{
    undoManager.beginNewTransaction ("Mutate");
    auto gauss = [this]
    {
        const float u1 = juce::jmax (1.0e-6f, random.nextFloat()), u2 = random.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (kTwoPi * u2);
    };
    for (auto* prm : getParameters())
    {
        auto* fp = dynamic_cast<juce::AudioParameterFloat*> (prm);
        if (fp == nullptr) continue;
        const auto id = fp->getParameterID();
        if (isMutationExcluded (id)) continue;
        // only mutate parts of the patch that are actually in use
        if (id.startsWith ("osc"))
        {
            const int o = id.substring (3, 4).getIntValue() - 1;
            if (paramValue (oscId (o, "on")) < 0.5f) continue;
        }
        if (id.startsWith ("fx_"))
        {
            const auto prefix = id.upToLastOccurrenceOf ("_", false, false);
            if (auto* on = apvts.getRawParameterValue (prefix + "_on"); on != nullptr && on->load() < 0.5f) continue;
        }
        if (id == "master_vol" || id == "amp_level") continue;
        const float spread = (id.contains ("_step") ? 0.6f : 0.22f) * amount;
        const float v = clamp01 (fp->convertTo0to1 (fp->get()) + gauss() * spread);
        fp->beginChangeGesture();
        fp->setValueNotifyingHost (v);
        fp->endChangeGesture();
    }
}

// ------------------------------------------------------------------ state
juce::ValueTree LumenSynthXLProcessor::fullState()
{
    auto st = apvts.copyState();
    for (int i = st.getNumChildren(); --i >= 0;)
        if (st.getChild (i).hasType ("Imported")) st.removeChild (i, nullptr);
    for (int o = 0; o < kNumOsc; ++o)
        if (imported[o] != nullptr && ! imported[o]->sourceFrames.empty())
        {
            juce::ValueTree t ("Imported");
            t.setProperty ("osc", o, nullptr);
            t.setProperty ("name", imported[o]->name, nullptr);
            t.setProperty ("frames", imported[o]->numFrames, nullptr);
            juce::MemoryBlock mb (imported[o]->sourceFrames.data(), imported[o]->sourceFrames.size() * sizeof (float));
            t.setProperty ("data", mb.toBase64Encoding(), nullptr);
            st.appendChild (t, nullptr);
        }
    st.setProperty ("presetName", presets.currentName(), nullptr);
    {
        const juce::ScopedLock sl (nameLock);
        st.setProperty ("macroNames", macroNames.joinIntoString ("|"), nullptr);
    }
    return st;
}

void LumenSynthXLProcessor::applyFullState (const juce::ValueTree& tree)
{
    auto copy = tree.createCopy();
    for (int i = copy.getNumChildren(); --i >= 0;)
    {
        auto c = copy.getChild (i);
        if (! c.hasType ("Imported")) continue;
        const int o = juce::jlimit (0, kNumOsc - 1, (int) c.getProperty ("osc"));
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (c.getProperty ("data").toString()))
            if (auto wt = WavetableFactory::fromSourceData (mb, (int) c.getProperty ("frames"), c.getProperty ("name").toString()))
            {
                const juce::ScopedLock sl (tableLock);
                tableStore.push_back (std::move (wt));
                imported[o] = tableStore.back().get();
                loadedPtr[o] = nullptr;
            }
        copy.removeChild (i, nullptr);
    }
    setMacroNames (juce::StringArray::fromTokens (copy.getProperty ("macroNames").toString(), "|", ""));
    if (copy.hasType (apvts.state.getType()))
        apvts.replaceState (copy);
    refreshTables();
}

void LumenSynthXLProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = fullState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LumenSynthXLProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyFullState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSynthXLProcessor(); }
