#include "Presets.h"

namespace lxl
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) { refresh(); }

juce::StringArray PresetManager::categories()
{
    return { "Bass", "Lead", "Pad", "Pluck", "Keys", "Chord", "Arp", "FX" };
}

juce::StringArray PresetManager::genres()
{
    juce::StringArray g;
    for (int i = 0; i < kNumFactoryPresets; ++i) g.addIfNotAlreadyThere (kFactoryPresets[i].genre);
    g.removeString ("Init");
    g.sort (true);
    return g;
}

juce::File PresetManager::userFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("Lumen Audio").getChildFile ("Lumen Synth XL").getChildFile ("Presets");
    f.createDirectory();
    return f;
}

void PresetManager::refresh()
{
    const auto keepName = currentName();
    const bool keepFactory = current >= 0 && current < list.size() && list[current].factory;
    list.clearQuick();
    for (int i = 0; i < kNumFactoryPresets; ++i)
        list.add ({ kFactoryPresets[i].name, kFactoryPresets[i].category, kFactoryPresets[i].genre, true, i, {} });
    for (auto& f : userFolder().findChildFiles (juce::File::findFiles, true, "*.lxlpreset"))
    {
        juce::String cat = "User";
        if (auto xml = juce::XmlDocument::parse (f))
            cat = xml->getStringAttribute ("presetCategory", "User");
        list.add ({ f.getFileNameWithoutExtension(), cat, "User", false, -1, f });
    }
    for (int i = 0; i < list.size(); ++i)
        if (list[i].name == keepName && list[i].factory == keepFactory) { current = i; break; }
    if (onListChanged) onListChanged();
}

juce::String PresetManager::currentInfo() const
{
    if (current < 0 || current >= list.size()) return {};
    const auto& e = list.getReference (current);
    return e.category + "  /  " + e.genre;
}

void PresetManager::resetToDefaults()
{
    for (auto* prm : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rp->getParameterID() != "quality")
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

static void setPlain (juce::AudioProcessorValueTreeState& s, const juce::String& id, float v)
{
    if (auto* p = s.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    else
        jassertfalse;
}

juce::StringArray PresetManager::applyFactory (int index)
{
    resetToDefaults();
    if (index < 0 || index >= kNumFactoryPresets) return {};
    const auto& fp = kFactoryPresets[index];
    int row = 0;
    for (auto token : juce::StringArray::fromTokens (fp.data, ";", ""))
    {
        token = token.trim();
        if (token.isEmpty()) continue;
        const auto key = token.upToFirstOccurrenceOf ("=", false, false);
        const auto val = token.fromFirstOccurrenceOf ("=", false, false);
        if (key == "m")
        {
            auto parts = juce::StringArray::fromTokens (val, ":", "");
            if (parts.size() != 3 || row >= kNumMatrix) continue;
            const int d = destIndexForParam (parts[1]);
            jassert (d > 0);
            if (d <= 0) continue;
            setPlain (apvts, mtxId (row, "src"), (float) parts[0].getIntValue());
            setPlain (apvts, mtxId (row, "dst"), (float) d);
            setPlain (apvts, mtxId (row, "amt"), parts[2].getFloatValue());
            ++row;
        }
        else setPlain (apvts, key, val.getFloatValue());
    }
    return juce::StringArray::fromTokens (fp.macros, "|", "");
}

void PresetManager::load (int index)
{
    if (index < 0 || index >= list.size()) return;
    current = index;
    const auto& e = list.getReference (index);
    if (e.factory)
    {
        auto macros = applyFactory (e.factoryIndex);
        if (onFactoryLoaded) onFactoryLoaded (macros);
    }
    else if (auto xml = juce::XmlDocument::parse (e.file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid() && applyFullState) applyFullState (tree);
    }
}

bool PresetManager::saveUser (const juce::String& name, const juce::String& category, const juce::ValueTree& fullState)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto file = userFolder().getChildFile (clean + ".lxlpreset");
    auto st = fullState.createCopy();
    st.setProperty ("presetCategory", category, nullptr);
    if (auto xml = st.createXml())
    {
        if (! xml->writeTo (file)) return false;
        refresh();
        for (int i = 0; i < list.size(); ++i)
            if (! list[i].factory && list[i].file == file) current = i;
        return true;
    }
    return false;
}

void PresetManager::step (int delta)
{
    if (list.isEmpty()) return;
    load ((current + delta + list.size()) % list.size());
}
} // namespace lxl
