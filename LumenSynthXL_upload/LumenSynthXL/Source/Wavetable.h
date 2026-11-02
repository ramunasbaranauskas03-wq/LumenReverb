#pragma once
#include "Common.h"

namespace lxl
{
/** A band-limited wavetable: N frames of kSize samples, each with kLevels mip levels.
    Level k keeps harmonics 1 .. (kSize/2 >> k). Every frame row has one guard sample. */
struct Wavetable
{
    static constexpr int kSize   = 2048;
    static constexpr int kLevels = 11;
    static constexpr int kStride = kSize + 1;

    juce::String name;
    int numFrames = 1;
    std::vector<float> data;          // [frame][level][kStride]
    std::vector<float> sourceFrames;  // raw frames (only kept for user imports, for state saving)

    const float* frame (int f, int level) const noexcept
    {
        return data.data() + ((size_t) f * kLevels + (size_t) level) * kStride;
    }

    /** Builds mip levels from raw single-cycle frames (each kSize samples). */
    static std::unique_ptr<Wavetable> fromFrames (const std::vector<std::vector<float>>& frames, const juce::String& name, bool keepSource);
};

namespace WavetableFactory
{
juce::StringArray names();
int numFactoryTables();
std::unique_ptr<Wavetable> make (int index);
/** Loads a WAV/AIFF/FLAC file. Files whose length is a multiple of 2048 are read as wavetables (Serum-style),
    anything else is converted by slicing the sample into 64 single-cycle frames. */
std::unique_ptr<Wavetable> fromFile (const juce::File& file, juce::String& error);
std::unique_ptr<Wavetable> fromSourceData (const juce::MemoryBlock& raw, int numFrames, const juce::String& name);
} // namespace WavetableFactory
} // namespace lxl
