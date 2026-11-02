#include "../Source/PluginProcessor.h"
#include "../Source/UI/PluginEditor.h"
#include "../Source/Fx.h"

struct RenderResult { float peak = 0, lufs = -100, tailDb = -100; bool bad = false; double speed = 0; };

static RenderResult renderCore (LumenSynthXLProcessor& proc, const juce::String& cat, const juce::File* wavOut);

static RenderResult renderPreset (LumenSynthXLProcessor& proc, int index, const juce::File* wavOut)
{
    proc.presets.load (index);
    return renderCore (proc, proc.presets.entries()[index].category, wavOut);
}

static RenderResult renderPresetState (LumenSynthXLProcessor& proc, const juce::MemoryBlock& state, const juce::String& cat, const juce::File* wavOut)
{
    proc.setStateInformation (state.getData(), (int) state.getSize());
    return renderCore (proc, cat, wavOut);
}

static RenderResult renderCore (LumenSynthXLProcessor& proc, const juce::String& cat, const juce::File* wavOut)
{
    const double sr = 48000.0;
    const int block = 256;
    proc.refreshTables();
    proc.prepareToPlay (sr, block);
    proc.engine.reset();
    const int total = (int) (sr * 6.0);
    juce::AudioBuffer<float> out (2, total), buf (2, block);

    struct Ev { double t; int note; bool on; int vel; };
    std::vector<Ev> evs;
    auto note = [&] (double t, double len, int n, int v = 105) { evs.push_back ({ t, n, true, v }); evs.push_back ({ t + len, n, false, 0 }); };
    const double s16 = 60.0 / 124.0 / 4.0;
    if (cat == "Bass")
    {
        const int pat[] = { 36, 36, 48, 36, 43, 36, 46, 39 };
        for (int k = 0; k < 16; ++k) note (k * s16 * 2.0, s16 * 1.6, pat[k % 8], k % 2 ? 90 : 120);
        note (2.2, 1.6, 36);
    }
    else if (cat == "Lead")
    {
        const int mel[] = { 72, 74, 76, 79, 76, 74, 72, 67 };
        for (int k = 0; k < 8; ++k) note (k * 0.3, 0.27, mel[k]);
        note (2.5, 1.8, 72);
    }
    else if (cat == "Arp" || cat == "FX")
    {
        note (0.0, 4.2, cat == "Arp" ? 48 : 60);
    }
    else
    {
        for (int n : { 48, 55, 60, 64, 67 }) note (0.0, 2.2, n);
        const int arp[] = { 60, 64, 67, 72, 67, 64, 60, 55 };
        for (int k = 0; k < 8; ++k) note (2.6 + k * 0.25, 0.2, arp[k]);
    }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        for (auto& e : evs)
        {
            const int s = (int) (e.t * sr);
            if (s >= pos && s < pos + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos);
        }
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    RenderResult r;
    r.speed = (total / sr * 1000.0) / (juce::Time::getMillisecondCounterHiRes() - t0);

    // K-weighting (BS.1770): high shelf +4 dB @ 1.68 kHz, high-pass @ 38 Hz
    lxl::Biquad sh[2], hp[2];
    for (int c = 0; c < 2; ++c) { sh[c].highShelf (sr, 1681.0, 4.0); hp[c].highPass (sr, 38.0, 0.5); }
    std::vector<double> blockPow;
    const int bl = (int) (0.4 * sr), hop = (int) (0.1 * sr);
    std::vector<float> kw[2] = { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (c, i);
            if (! std::isfinite (v)) r.bad = true;
            r.peak = juce::jmax (r.peak, std::abs (v));
            kw[c][(size_t) i] = hp[c].tick (sh[c].tick (v));
        }
    for (int s = 0; s + bl <= total; s += hop)
    {
        double p = 0;
        for (int c = 0; c < 2; ++c) for (int i = s; i < s + bl; ++i) p += (double) kw[c][(size_t) i] * kw[c][(size_t) i];
        blockPow.push_back (p / bl);
    }
    auto lufsOf = [] (double p) { return -0.691 + 10.0 * std::log10 (p + 1e-20); };
    double sum = 0; int cnt = 0;
    for (auto p : blockPow) if (lufsOf (p) > -70.0) { sum += p; ++cnt; }
    if (cnt > 0)
    {
        const double rel = lufsOf (sum / cnt) - 10.0;
        sum = 0; cnt = 0;
        for (auto p : blockPow) if (lufsOf (p) > rel) { sum += p; ++cnt; }
        r.lufs = cnt > 0 ? (float) lufsOf (sum / cnt) : -100.0f;
    }
    // tail: last 0.3 s must have died out for one-shot categories
    double tp = 0;
    for (int c = 0; c < 2; ++c) for (int i = total - (int) (0.3 * sr); i < total; ++i) tp += out.getSample (c, i) * out.getSample (c, i);
    r.tailDb = (float) (10.0 * std::log10 (tp / (0.6 * sr) + 1e-20));

    if (wavOut != nullptr)
    {
        wavOut->deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (*wavOut), sr, 2, 16, {}, 0));
        if (w) w->writeFromAudioSampleBuffer (out, 0, total);
    }
    return r;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? argv[1] : "render";
    const juce::File outDir (argc > 2 ? argv[2] : "/tmp");
    outDir.createDirectory();
    LumenSynthXLProcessor proc;

    if (mode == "render")
    {
        // render [outDir] [wavFilter]: writes results.csv; WAVs for presets whose name contains wavFilter ("*" = all)
        const juce::String filter = argc > 3 ? argv[3] : "";
        juce::FileOutputStream csv (outDir.getChildFile ("results.csv"));
        csv.setPosition (0); csv.truncate();
        csv << "index,name,category,genre,peak,lufs,tail,speed,bad\n";
        const auto entries = proc.presets.entries();
        for (int i = 0; i < entries.size(); ++i)
        {
            if (! entries[i].factory) continue;
            const bool wantWav = filter.isNotEmpty() && ((filter == "every5" && (i - 1) % 5 == 0) || filter == "*" || entries[i].name.containsIgnoreCase (filter) || entries[i].category == filter);
            const auto wf = outDir.getChildFile (entries[i].category + " - " + entries[i].name + ".wav");
            const auto r = renderPreset (proc, i, wantWav ? &wf : nullptr);
            csv << i << "," << entries[i].name << "," << entries[i].category << "," << entries[i].genre << ","
                << juce::String (r.peak, 4) << "," << juce::String (r.lufs, 2) << "," << juce::String (r.tailDb, 1) << ","
                << juce::String (r.speed, 1) << "," << (r.bad ? 1 : 0) << "\n";
        }
        csv.flush();
        std::printf ("rendered %d presets\n", entries.size());
        return 0;
    }
    if (mode == "params")
    {
        // params outDir "id=v;id=v" category [wavName]
        proc.presets.load (0);
        for (auto tok : juce::StringArray::fromTokens (argv[3], ";", ""))
            if (auto* prm = proc.apvts.getParameter (tok.upToFirstOccurrenceOf ("=", false, false)))
                prm->setValueNotifyingHost (prm->convertTo0to1 (tok.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
        const juce::String cat = argc > 4 ? argv[4] : "Lead";
        // hijack the category of the Init entry for note selection
        auto& e = const_cast<lxl::PresetEntry&> (proc.presets.entries().getReference (0));
        e.category = cat;
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        struct Keep { LumenSynthXLProcessor& p; juce::MemoryBlock m; };
        const auto wf = outDir.getChildFile (argc > 5 ? juce::String (argv[5]) + ".wav" : juce::String ("params.wav"));
        // renderPreset reloads the preset, so re-apply the state through a factory-less path
        proc.presets.applyFullState = nullptr;
        auto r = renderPresetState (proc, mb, cat, argc > 5 ? &wf : nullptr);
        std::printf ("peak %.3f  lufs %.2f  tail %.1f\n", r.peak, r.lufs, r.tailDb);
        return 0;
    }
    if (mode == "shot")
    {
        const int page = argc > 3 ? juce::String (argv[3]).getIntValue() : 0;
        proc.presets.load (argc > 4 ? juce::String (argv[4]).getIntValue() : 1);
        proc.prepareToPlay (48000.0, 256);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (auto* pe = dynamic_cast<lxl::PluginEditor*> (ed.get())) pe->showPageForTest (page);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("page" + juce::String (page) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        return 0;
    }
    if (mode == "state")
    {
        // make a test wavetable file: 16 frames of morphing pulse, and a short sample
        auto wtFile = outDir.getChildFile ("test_table.wav");
        {
            juce::AudioBuffer<float> b (1, 2048 * 16);
            for (int f = 0; f < 16; ++f)
                for (int i = 0; i < 2048; ++i)
                    b.setSample (0, f * 2048 + i, (i < 2048 * (0.5f - f * 0.028f)) ? 0.8f : -0.8f);
            wtFile.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (wtFile), 48000.0, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        auto err = proc.importWavetable (1, wtFile);
        std::printf ("import: %s  frames=%d  name=%s\n", err.isEmpty() ? "ok" : err.toRawUTF8(),
                     proc.currentTable (1)->numFrames, proc.currentTable (1)->name.toRawUTF8());
        // sample conversion path
        auto smp = outDir.getChildFile ("Neuro Growl.wav");
        err = proc.importWavetable (0, smp);
        std::printf ("sample import: %s  frames=%d\n", err.isEmpty() ? "ok" : err.toRawUTF8(), proc.currentTable (0)->numFrames);

        proc.assignModulation (lxl::SrcMod1 + 2, lxl::destIndexForParam ("osc2_pos"), 0.5f);
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        LumenSynthXLProcessor p2;
        p2.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("restore: osc2 table=%s frames=%d, osc1 frames=%d, rows on osc2_pos=%d, state %d KB\n",
                     p2.currentTable (1)->name.toRawUTF8(), p2.currentTable (1)->numFrames, p2.currentTable (0)->numFrames,
                     p2.rowsForDest (lxl::destIndexForParam ("osc2_pos")).size(), (int) mb.getSize() / 1024);
        // mutate a few times then render
        for (int i = 0; i < 5; ++i) p2.mutate (0.6f);
        p2.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        float peak = 0; bool bad = false;
        for (int k = 0; k < 400; ++k)
        {
            juce::MidiBuffer m;
            if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
            if (k == 200) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buf.clear(); p2.processBlock (buf, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) bad = true; peak = juce::jmax (peak, std::abs (v)); }
        }
        std::printf ("mutated render: peak %.3f %s, undo steps available: %s\n", peak, bad ? "NaN!" : "ok", p2.undoManager.canUndo() ? "yes" : "no");
        // quality switch + sample-rate change
        for (double sr : { 44100.0, 96000.0 })
        {
            p2.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> b2 (2, 512);
            juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            float pk = 0; for (int k = 0; k < 50; ++k) { b2.clear(); p2.processBlock (b2, m); m.clear(); pk = juce::jmax (pk, b2.getMagnitude (0, 512)); }
            std::printf ("sr %.0f peak %.3f\n", sr, pk);
        }
        return 0;
    }
    return 0;
}
