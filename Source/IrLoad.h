// Reads and validates an impulse response file, mixing it down to mono.
#pragma once

#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

struct LoadedImpulse
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
    juce::String error;
};

inline LoadedImpulse readImpulseFile (const juce::File& file)
{
    static constexpr double maxSeconds = 12.0;

    LoadedImpulse result;

    if (! file.existsAsFile())
    {
        result.error = "File not found";
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        result.error = "Not a readable audio file";
        return result;
    }

    const auto length = reader->lengthInSamples;

    if (length <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        result.error = "The file contains no audio";
        return result;
    }

    if ((double) length / reader->sampleRate > maxSeconds)
    {
        result.error = "Impulse is longer than 12 seconds";
        return result;
    }

    const int numSamples = (int) length;
    const int channels = (int) reader->numChannels;
    juce::AudioBuffer<float> source (channels, numSamples);
    reader->read (&source, 0, numSamples, 0, true, true);

    result.buffer.setSize (1, numSamples);
    result.buffer.clear();
    for (int ch = 0; ch < channels; ++ch)
        result.buffer.addFrom (0, 0, source, ch, 0, numSamples, 1.0f / (float) channels);

    if (result.buffer.getMagnitude (0, numSamples) < 1.0e-6f)
    {
        result.error = "The file is silent";
        return result;
    }

    result.sampleRate = reader->sampleRate;
    return result;
}
