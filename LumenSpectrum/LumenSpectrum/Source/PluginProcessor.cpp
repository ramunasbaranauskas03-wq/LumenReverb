#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lumen::spec;

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    StringArray roleNames;  for (auto& r : roles()) roleNames.add (r.name);
    StringArray colNames { "Role colour", "DAW track colour" };  for (auto& p : palette()) colNames.add (p.first);
    StringArray targets { "No target" };  for (auto& g : genres()) targets.add (g.name);  targets.add ("Snapshot (SNAP REF)");

    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    { L.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    choice (ids::role,      "Instrument",    roleNames, kDefaultRole);
    choice (ids::colour,    "Colour",        colNames, 0);
    choice (ids::view,      "View",          { "Bars", "Curves", "Ownership", "Masking", "Sonogram" }, 0);
    choice (ids::barRes,    "Bar resolution",{ "1/3 octave", "1/6 octave", "1/12 octave" }, 0);
    choice (ids::barColour, "Bar colours",   { "Frequency bands", "Instrument colour" }, 0);
    choice (ids::fft,       "FFT size",      { "2048", "4096", "8192", "16384", "32768" }, 2);
    choice (ids::avg,       "Averaging",     { "Fast", "Medium", "Slow", "Long" }, 1);
    choice (ids::chan,      "Channel",       { "Stereo", "Mid", "Side", "Left", "Right" }, 0);
    choice (ids::slope,     "Slope",         { "0 dB/oct", "3 dB/oct", "4.5 dB/oct" }, 2);
    choice (ids::range,     "Range",         { "48 dB", "72 dB", "96 dB", "120 dB" }, 1);
    choice (ids::target,    "Target",        targets, 1);
    choice (ids::smooth,    "Smoothing",     { "Off", "1/24 octave", "1/12 octave", "1/6 octave", "1/3 octave" }, 2);
    L.add (std::make_unique<AudioParameterBool> (ParameterID { ids::peak, 1 },   "Peak hold", true));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { ids::others, 1 }, "Show other tracks", true));
    return L;
}
}

LumenSpectrumProcessor::LumenSpectrumProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LumenSpectrum", makeLayout())
{
    slotIndex = hub->claim();
    for (auto* id : { ids::role, ids::colour, ids::fft, ids::avg, ids::chan })
        apvts.addParameterListener (id, this);
    snap.fill (lumen::spec::kSilenceDb);
    refreshIdentity();
}

LumenSpectrumProcessor::~LumenSpectrumProcessor()
{
    cancelPendingUpdate();
    for (auto* id : { ids::role, ids::colour, ids::fft, ids::avg, ids::chan })
        apvts.removeParameterListener (id, this);
    hub->release (slotIndex);
}

bool LumenSpectrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void LumenSpectrumProcessor::prepareToPlay (double sampleRate, int)
{
    if (slotIndex >= 0) hub->slot (slotIndex).sampleRate.store (sampleRate);
}

void LumenSpectrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // analysis only - the audio passes through untouched (zero latency, bit-transparent)
    if (slotIndex < 0 || buffer.getNumChannels() == 0) return;
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    hub->slot (slotIndex).push (l, r, buffer.getNumSamples());
}

// ---------------------------------------------------------------- identity
void LumenSpectrumProcessor::refreshIdentity()
{
    if (slotIndex < 0) return;
    auto& s = hub->slot (slotIndex);
    const int role = (int) apvts.getRawParameterValue (ids::role)->load();
    const int col  = (int) apvts.getRawParameterValue (ids::colour)->load();
    juce::uint32 argb = roles()[(size_t) juce::jlimit (0, kNumRoles - 1, role)].argb;
    if (col == 1 && hostColour.getAlpha() > 0) argb = hostColour.withAlpha (1.0f).brighter (0.25f).getARGB();
    else if (col >= 2) argb = palette()[(size_t) juce::jlimit (0, kNumPalette - 1, col - 2)].second;
    s.role.store (role);
    s.argb.store (argb);
    s.setName (trackName.isNotEmpty() ? trackName : juce::String (roles()[(size_t) juce::jlimit (0, kNumRoles - 1, role)].name));
}

void LumenSpectrumProcessor::setTrackName (const juce::String& n, bool fromUser)
{
    if (fromUser) nameFromUser = n.isNotEmpty();
    trackName = n.trim();
    if (! roleFromUser)
    {
        const int g = guessRole (trackName);
        if (g >= 0)
        {
            autoRoleChange = true;
            if (auto* p = apvts.getParameter (ids::role))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) g));
            autoRoleChange = false;
        }
    }
    refreshIdentity();
}

void LumenSpectrumProcessor::setRoleFromUser (int r)
{
    roleFromUser = true;
    if (auto* p = apvts.getParameter (ids::role))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) r));
    refreshIdentity();
}

void LumenSpectrumProcessor::updateTrackProperties (const TrackProperties& props)
{
    auto apply = [this, props]
    {
        if (props.colourARGB.has_value()) hostColour = juce::Colour (*props.colourARGB);
        if (props.name.has_value() && props.name->isNotEmpty() && ! nameFromUser) setTrackName (*props.name, false);
        else refreshIdentity();
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) apply();
    else juce::MessageManager::callAsync (apply);
}

void LumenSpectrumProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == ids::role && ! autoRoleChange) roleFromUser = true;
    if (id == ids::role || id == ids::colour)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) refreshIdentity();
        else triggerAsyncUpdate();   // automation from the audio thread
    }
    pushGlobals();
}

void LumenSpectrumProcessor::pushGlobals()
{
    hub->fftOrder.store (11 + (int) apvts.getRawParameterValue (ids::fft)->load());
    hub->averaging.store ((int) apvts.getRawParameterValue (ids::avg)->load());
    hub->channelMode.store ((int) apvts.getRawParameterValue (ids::chan)->load());
}

// ---------------------------------------------------------------- snapshot
void LumenSpectrumProcessor::captureSnapshot()
{
    if (slotIndex < 0) return;
    auto& s = hub->slot (slotIndex);
    for (int p = 0; p < kPoints; ++p) snap[(size_t) p] = s.spec[p].load();
    hasSnap.store (true);
}

// ---------------------------------------------------------------- state
void LumenSpectrumProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (st.createXml());
    xml->setAttribute ("trackName", trackName);
    xml->setAttribute ("nameFromUser", nameFromUser);
    xml->setAttribute ("roleFromUser", roleFromUser);
    xml->setAttribute ("editorW", editorW);
    xml->setAttribute ("editorH", editorH);
    if (hasSnap.load())
    {
        juce::MemoryBlock mb (snap.data(), sizeof (float) * snap.size());
        xml->setAttribute ("snapshot", mb.toBase64Encoding());
    }
    copyXmlToBinary (*xml, destData);
}

void LumenSpectrumProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    trackName    = xml->getStringAttribute ("trackName");
    nameFromUser = xml->getBoolAttribute ("nameFromUser");
    roleFromUser = xml->getBoolAttribute ("roleFromUser");
    editorW = juce::jlimit (1180, 2400, xml->getIntAttribute ("editorW", editorW));
    editorH = juce::jlimit (640, 1500, xml->getIntAttribute ("editorH", editorH));
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (xml->getStringAttribute ("snapshot")) && mb.getSize() == sizeof (float) * snap.size())
    {
        std::memcpy (snap.data(), mb.getData(), mb.getSize());
        hasSnap.store (true);
    }
    pushGlobals();
    refreshIdentity();
}

juce::AudioProcessorEditor* LumenSpectrumProcessor::createEditor() { return new LumenSpectrumEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LumenSpectrumProcessor(); }
