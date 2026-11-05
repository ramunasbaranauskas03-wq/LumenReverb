#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0;
    // synthetic source: "vocal-ish" chirp + snare-ish noise burst
    juce::AudioBuffer<float> src (2, (int) (sr * 0.8));
    juce::Random rnd (1);
    for (int i = 0; i < src.getNumSamples(); ++i)
    {
        const float t = i / (float) sr;
        float v = 0.5f * std::sin (6.2831853f * (220.0f + 200.0f * t) * t) * std::exp (-2.0f * t);
        if (i < sr * 0.15) v += (rnd.nextFloat() * 2 - 1) * 0.6f * std::exp (-25.0f * t);
        src.setSample (0, i, v); src.setSample (1, i, v);
    }

    const char* names[] = { "Room", "Hall", "Plate", "Cathedral", "Shimmer", "Bloom" };
    for (int mode = 0; mode < 2; ++mode)
    for (int a = 0; a < 6; ++a)
    {
        lumen::RenderSettings s; s.sampleRate = sr; s.swellSamples = (int) (sr * 2.0); s.mode = mode;
        s.reverb.algo = a; s.reverb.decay = a == 3 || a == 5 ? 12.0f : 4.0f; s.reverb.shimmer = a == 4 ? 0.8f : 0.0f;
        s.sweep = 0.5f; s.sourceBlend = 1.0f;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = lumen::Renderer::render (src, s);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        bool bad = false;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < r->audio.getNumSamples(); ++i) if (! std::isfinite (r->audio.getSample (ch, i))) bad = true;
        // energy in quarters of the swell -> must rise
        juce::String q;
        for (int k = 0; k < 4; ++k) q << juce::String (juce::Decibels::gainToDecibels (r->audio.getRMSLevel (0, k * r->swellLen / 4, r->swellLen / 4)), 1) << " ";
        printf ("%s %-9s render %6.1f ms  peak %.3f  nan:%d  swell RMS quarters dB: %s\n", mode ? "PRE " : "TAIL", names[a], ms,
                r->audio.getMagnitude (0, r->audio.getNumSamples()), bad, q.toRawUTF8());
        if (a == 4 && mode == 0)
        {
            juce::File f ("/tmp/claude-0/shimmer_test.wav"); f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
            w->writeFromAudioSampleBuffer (r->audio, 0, r->audio.getNumSamples());
        }
    }

    // ---- full processor: capture -> render -> playback through processBlock ----
    LumenReverseProcessor p;
    p.setPlayConfigDetails (2, 2, sr, 512);
    p.prepareToPlay (sr, 512);
    p.apvts.getParameter ("capMode")->setValueNotifyingHost (0.0f); // manual
    p.armCapture();
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    for (int pos = 0; pos < src.getNumSamples(); pos += 512)
    {
        block.clear();
        const int n = std::min (512, src.getNumSamples() - pos);
        for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, src, ch, pos, n);
        p.processBlock (block, midi);
    }
    p.stopCapture();
    block.clear(); p.processBlock (block, midi);
    printf ("capture state after stop: %d len ok\n", p.getCaptureState());
    // pump message loop for timer -> finishCapture -> render
    for (int i = 0; i < 100 && (! p.hasSource() || p.isRendering() || p.getPlayheadFraction() > 2); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    printf ("hasSource %d rendering %d\n", p.hasSource(), p.isRendering());

    p.apvts.getParameter ("dry")->setValueNotifyingHost (0.0f);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
    float maxOut = 0; int blocks = 0;
    for (int b = 0; b < 400; ++b)
    {
        block.clear(); p.processBlock (block, midi); midi.clear();
        maxOut = std::max (maxOut, block.getMagnitude (0, 512));
        if (p.isVoicePlaying()) ++blocks;
    }
    printf ("MIDI playback: peak %.3f, voice blocks %d (%.2f s)\n", maxOut, blocks, blocks * 512 / sr);

    auto f = p.exportAudio (false);
    printf ("export: %s (%lld bytes)\n", f.getFullPathName().toRawUTF8(), (long long) f.getSize());

    juce::MemoryBlock state; p.getStateInformation (state);
    LumenReverseProcessor p2; p2.prepareToPlay (sr, 512); p2.setStateInformation (state.getData(), (int) state.getSize());
    printf ("state roundtrip: %zu bytes, restored source %d\n", state.getSize(), p2.hasSource());

    // ---- UI snapshot ----
    p.apvts.getParameter ("tremDepth")->setValueNotifyingHost (0.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File png ("/tmp/claude-0/ui.png"); png.deleteFile();
    juce::FileOutputStream os (png); juce::PNGImageFormat().writeImageToStream (img, os);
    printf ("ui snapshot written\n");
    ed.reset();
    return 0;
}
