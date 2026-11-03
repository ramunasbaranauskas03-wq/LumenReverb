#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include "Model.h"

// Session-wide analysis engine. Every Lumen Spectrum instance in the DAW process owns one
// Slot and pushes its audio into it; one background thread runs the FFTs for all slots, so
// every instance can draw every other instance (even when their windows are closed).
namespace lumen::spec
{
struct Slot
{
    std::atomic<bool> used { false };
    std::atomic<int> uid { 0 };
    std::atomic<int> role { kDefaultRole };
    std::atomic<juce::uint32> argb { 0xffc8f06b };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> writePos { 0 };
    std::atomic<juce::int64> lastSoundMs { 0 };
    std::atomic<float> peakDb { -120.0f };
    std::atomic<juce::uint32> lastPushMs { 0 };
    std::atomic<float> spec[kPoints];           // averaged spectrum, dB (sine at 0 dBFS reads 0 dB)

    float ring[2][kRing] {};

    juce::String getName() const            { const juce::SpinLock::ScopedLockType l (nameLock); return name; }
    void setName (const juce::String& n)    { const juce::SpinLock::ScopedLockType l (nameLock); name = n; }

    // audio thread
    void push (const float* l, const float* r, int n) noexcept
    {
        int wp = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[0][wp] = l[i]; ring[1][wp] = r[i];
            wp = (wp + 1) & (kRing - 1);
        }
        writePos.store (wp, std::memory_order_release);
        lastPushMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    }

    // analysis thread private
    float powAvg[kPoints] {};
    bool fresh = true;

private:
    mutable juce::SpinLock nameLock;
    juce::String name;
};

class Hub : private juce::Thread
{
public:
    Hub() : juce::Thread ("Lumen Spectrum analysis")
    {
        for (auto& s : slots) for (auto& v : s.spec) v.store (kSilenceDb);
        startThread (juce::Thread::Priority::low);
    }
    ~Hub() override { stopThread (3000); }

    int claim()
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            bool expected = false;
            if (slots[(size_t) i].used.compare_exchange_strong (expected, true))
            {
                auto& s = slots[(size_t) i];
                for (auto& ch : s.ring) std::fill (std::begin (ch), std::end (ch), 0.0f);
                for (auto& v : s.spec) v.store (kSilenceDb);
                s.fresh = true;
                s.uid.store (++uidCounter);
                s.setName ("Track " + juce::String (i + 1));
                generation.fetch_add (1);
                return i;
            }
        }
        return -1;   // more than 32 instances: this one analyses itself only
    }
    void release (int i)
    {
        if (i < 0) return;
        slots[(size_t) i].used.store (false);
        generation.fetch_add (1);
    }
    Slot& slot (int i) { return slots[(size_t) i]; }

    // session-wide analysis settings (the instance you edit sets them for all)
    std::atomic<int> fftOrder { 13 };     // 11..15  (2048 .. 32768)
    std::atomic<int> averaging { 1 };     // 0 fast, 1 medium, 2 slow, 3 long
    std::atomic<int> channelMode { 0 };   // 0 stereo, 1 mid, 2 side, 3 left, 4 right
    std::atomic<int> generation { 0 };

private:
    std::array<Slot, kMaxSlots> slots;
    std::atomic<int> uidCounter { 0 };

    // ------------------------------------------------------------ analysis thread
    std::unique_ptr<juce::dsp::FFT> fft;
    int order = -1, N = 0;
    std::vector<float> win, a, b;
    float winSum = 1.0f;
    int lastMode = -1;

    void prepare (int o)
    {
        order = o; N = 1 << o;
        fft = std::make_unique<juce::dsp::FFT> (o);
        win.resize ((size_t) N); a.assign ((size_t) N * 2, 0.0f); b.assign ((size_t) N * 2, 0.0f);
        winSum = 0.0f;
        for (int i = 0; i < N; ++i)   // 4-term Blackman-Harris: -92 dB side lobes
        {
            const double x = juce::MathConstants<double>::twoPi * i / (N - 1);
            win[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
            winSum += win[(size_t) i];
        }
        for (auto& s : slots) s.fresh = true;
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            const int o = juce::jlimit (11, 15, fftOrder.load());
            if (o != order) prepare (o);
            const int mode = channelMode.load();
            if (mode != lastMode) { lastMode = mode; for (auto& s : slots) s.fresh = true; }
            static const float alphas[] { 0.55f, 0.80f, 0.92f, 0.975f };
            const float alpha = alphas[juce::jlimit (0, 3, averaging.load())];
            for (auto& s : slots)
                if (s.used.load()) analyse (s, mode, alpha);
            wait (25);   // ~40 frames / s
        }
    }

    void analyse (Slot& s, int mode, float alpha)
    {
        const int wp = s.writePos.load (std::memory_order_acquire);
        const double sr = s.sampleRate.load();
        // host stopped calling this track (frozen / suspended): treat it as silence
        const bool stale = juce::Time::getMillisecondCounter() - s.lastPushMs.load (std::memory_order_relaxed) > 400u;
        float peak = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const int idx = (wp - N + i) & (kRing - 1);
            const float l = stale ? 0.0f : s.ring[0][idx], r = stale ? 0.0f : s.ring[1][idx];
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            float x = l, y = 0.0f;
            switch (mode)
            {
                case 1: x = 0.5f * (l + r); break;
                case 2: x = 0.5f * (l - r); break;
                case 3: x = l; break;
                case 4: x = r; break;
                default: x = l; y = r; break;
            }
            a[(size_t) i] = x * win[(size_t) i];
            if (mode == 0) b[(size_t) i] = y * win[(size_t) i];
        }
        const float pk = juce::Decibels::gainToDecibels (peak, -120.0f);
        s.peakDb.store (pk);
        if (pk > -80.0f) s.lastSoundMs.store (juce::Time::currentTimeMillis());

        std::fill (a.begin() + N, a.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (a.data(), true);
        if (mode == 0)
        {
            std::fill (b.begin() + N, b.end(), 0.0f);
            fft->performFrequencyOnlyForwardTransform (b.data(), true);
        }
        // to power, normalised so a full-scale sine reads 0 dB
        const float norm = 2.0f / winSum;
        const int half = N / 2;
        for (int k = 0; k <= half; ++k)
        {
            const float ma = a[(size_t) k] * norm;
            float p = ma * ma;
            if (mode == 0) { const float mb = b[(size_t) k] * norm; p = 0.5f * (p + mb * mb); }
            a[(size_t) k] = p;
        }

        const float binPerHz = (float) N / (float) sr;
        const float halfStep = std::pow (kFMax / kFMin, 0.5f / (float) (kPoints - 1));
        for (int p = 0; p < kPoints; ++p)
        {
            const float f  = pointFreq ((float) p);
            const float bl = f / halfStep * binPerHz, bh = f * halfStep * binPerHz;
            float pw = 1.0e-14f;
            if (bh >= (float) half) pw = 1.0e-14f;
            else if (bh - bl >= 1.0f)
            {
                // wide point: peak of the bins it covers (keeps narrow peaks visible)
                for (int k = (int) std::ceil (bl); k <= (int) std::floor (bh); ++k) pw = juce::jmax (pw, a[(size_t) k]);
            }
            else
            {
                // narrow point (low end): interpolate between bins in the dB domain
                const float bc = f * binPerHz;
                const int k0 = juce::jlimit (0, half - 1, (int) std::floor (bc));
                const float t = bc - (float) k0;
                const float d = (1.0f - t) * powToDb (a[(size_t) k0]) + t * powToDb (a[(size_t) k0 + 1]);
                pw = dbToPow (d);
            }
            s.powAvg[p] = s.fresh ? pw : s.powAvg[p] * alpha + pw * (1.0f - alpha);
            s.spec[p].store (powToDb (s.powAvg[p]), std::memory_order_relaxed);
        }
        s.fresh = false;
    }
};
} // namespace lumen::spec
