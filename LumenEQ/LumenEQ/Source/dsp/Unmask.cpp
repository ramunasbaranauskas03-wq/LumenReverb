#include "EqEngine.h"
#include <juce_dsp/juce_dsp.h>

namespace lumen
{

// Averaged power spectra (Welch, Hann 4096, 50% overlap) -> 1/6-octave bands 30 Hz..8 kHz.
// Masking score per band = how loud the quieter of the two is (both must be present),
// boosted when the sidechain is as loud as or louder than the track there.
std::vector<MaskZone> findMasking (const std::vector<float>& track, const std::vector<float>& sc, double fs, int maxZones)
{
    constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win (N), buf (2 * N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPiE * i / (N - 1));

    auto spectrum = [&] (const std::vector<float>& x)
    {
        std::vector<double> p (N / 2 + 1, 0.0);
        int frames = 0;
        for (size_t start = 0; start + N <= x.size(); start += N / 2, ++frames)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i) buf[(size_t) i] = x[start + (size_t) i] * win[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int i = 0; i <= N / 2; ++i) p[(size_t) i] += (double) buf[(size_t) i] * buf[(size_t) i];
        }
        if (frames > 0) for (auto& v : p) v /= frames;
        return p;
    };
    const auto pt = spectrum (track), ps = spectrum (sc);

    // 1/6-octave bands
    std::vector<float> fc, dt, ds;
    for (double f = 30.0; f <= 8000.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        const double lo = f / std::pow (2.0, 1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
        double et = 0.0, es = 0.0;
        for (int i = (int) std::floor (lo * N / fs); i <= (int) std::ceil (hi * N / fs) && i <= N / 2; ++i)
            if (i > 0) { et += pt[(size_t) i]; es += ps[(size_t) i]; }
        fc.push_back ((float) f);
        dt.push_back ((float) (10.0 * std::log10 (et + 1e-20)));
        ds.push_back ((float) (10.0 * std::log10 (es + 1e-20)));
    }
    const float maxT = *std::max_element (dt.begin(), dt.end()), maxS = *std::max_element (ds.begin(), ds.end());

    std::vector<float> score (fc.size(), -1e9f);
    for (size_t i = 0; i < fc.size(); ++i)
    {
        const float rt = dt[i] - maxT, rs = ds[i] - maxS;           // relative to each signal's loudest band
        if (rt < -24.0f || rs < -24.0f) continue;                   // both must really be there
        float sc_ = std::min (rt, rs) + 24.0f;                       // 0..24
        if (rs >= rt - 3.0f) sc_ += 4.0f;                            // sidechain dominates here
        score[i] = sc_;
    }

    std::vector<MaskZone> zones;
    std::vector<bool> used (fc.size(), false);
    for (int z = 0; z < maxZones; ++z)
    {
        int best = -1;
        for (size_t i = 0; i < fc.size(); ++i)
            if (! used[i] && score[i] > 6.0f && (best < 0 || score[i] > score[(size_t) best])) best = (int) i;
        if (best < 0) break;
        // block +-1 octave around it
        for (size_t i = 0; i < fc.size(); ++i)
            if (std::abs (std::log2 (fc[i] / fc[(size_t) best])) < 1.0) used[i] = true;

        // width: how far the score stays within 6 dB of the peak
        int lo = best, hi = best;
        while (lo > 0 && score[(size_t) lo - 1] > score[(size_t) best] - 6.0f) --lo;
        while (hi + 1 < (int) fc.size() && score[(size_t) hi + 1] > score[(size_t) best] - 6.0f) ++hi;
        const float octaves = std::max (1.0f / 3.0f, (float) (hi - lo + 1) / 6.0f);
        const float q = std::clamp (1.0f / octaves * 1.4f, 0.7f, 4.0f);

        // sidechain peak level through a band-pass at this frequency (for the threshold)
        const double w0 = 2.0 * kPiE * fc[(size_t) best] / fs, alpha = std::sin (w0) / (2.0 * q), a0 = 1 + alpha;
        const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2 * std::cos (w0) / a0, a2 = (1 - alpha) / a0;
        double z1 = 0, z2 = 0, pk = 0;
        for (float v : sc)
        {
            const double y = b0 * v + z1;
            z1 = -a1 * y + z2;
            z2 = b2 * v - a2 * y;
            pk = std::max (pk, std::fabs (y));
        }
        zones.push_back ({ fc[(size_t) best], q, score[(size_t) best], (float) (20.0 * std::log10 (pk + 1e-9)) });
    }
    return zones;
}

} // namespace lumen
