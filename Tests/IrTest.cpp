// End-to-end check of impulse loading: file parsing, rejection of bad files,
// and the response of the convolution engine. Exit code is the number of failures.
#include <cstdio>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "IrLoad.h"

static int failures = 0;

static void check (const char* name, bool ok)
{
    std::printf ("%-58s %s\n", name, ok ? "ok" : "FAIL");
    failures += ok ? 0 : 1;
}

// Writes a 16-bit PCM WAV directly, so the test does not depend on the reader's sibling writer.
static void writeWav (const juce::File& file, int channels, int rate, const std::vector<std::int16_t>& samples)
{
    juce::MemoryOutputStream o;
    const auto bytes = (juce::uint32) (samples.size() * 2);
    o.write ("RIFF", 4); o.writeInt ((int) (36 + bytes)); o.write ("WAVEfmt ", 8);
    o.writeInt (16); o.writeShort (1); o.writeShort ((short) channels);
    o.writeInt (rate); o.writeInt (rate * channels * 2); o.writeShort ((short) (channels * 2)); o.writeShort (16);
    o.write ("data", 4); o.writeInt ((int) bytes);
    o.write (samples.data(), samples.size() * 2);
    file.replaceWithData (o.getData(), o.getDataSize());
}

int main()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stack5150_irtest");
    dir.createDirectory();

    // Stereo 48 kHz IR: left is an impulse plus a decay, right has twice the decay.
    const int rate = 48000, frames = 4800;
    std::vector<std::int16_t> stereo ((size_t) frames * 2);
    for (int i = 0; i < frames; ++i)
    {
        const double e = std::exp (-(double) i / 500.0);
        stereo[(size_t) i * 2] = (std::int16_t) (i == 0 ? 30000 : 8000.0 * e);
        stereo[(size_t) i * 2 + 1] = (std::int16_t) (i == 0 ? 30000 : 16000.0 * e);
    }
    const auto good = dir.getChildFile ("good.wav");
    writeWav (good, 2, rate, stereo);

    auto loaded = readImpulseFile (good);
    check ("valid stereo WAV loads", loaded.error.isEmpty());
    check ("reports sample rate", loaded.sampleRate == rate);
    check ("mixed down to mono", loaded.buffer.getNumChannels() == 1 && loaded.buffer.getNumSamples() == frames);
    const float mid = loaded.buffer.getSample (0, 100);
    const float expected = (float) ((8000.0 + 16000.0) * 0.5 * std::exp (-100.0 / 500.0) / 32768.0);
    check ("mixdown averages channels", std::abs (mid - expected) < 0.002f);

    check ("missing file rejected", readImpulseFile (dir.getChildFile ("nope.wav")).error.isNotEmpty());

    const auto text = dir.getChildFile ("text.wav");
    text.replaceWithText ("this is not audio");
    check ("non-audio file rejected", readImpulseFile (text).error.isNotEmpty());

    const auto silent = dir.getChildFile ("silent.wav");
    writeWav (silent, 1, rate, std::vector<std::int16_t> (1000, 0));
    check ("silent file rejected", readImpulseFile (silent).error.isNotEmpty());

    const auto longFile = dir.getChildFile ("long.wav");
    writeWav (longFile, 1, 8000, std::vector<std::int16_t> (8000 * 13, 1000));
    check ("over-long file rejected", readImpulseFile (longFile).error.isNotEmpty());

    // Convolution at a different rate from the file, so resampling is exercised too.
    juce::dsp::Convolution convolution;
    const double sampleRate = 44100.0;
    const int blockSize = 512;
    convolution.prepare ({ sampleRate, (juce::uint32) blockSize, 1 });
    convolution.loadImpulseResponse (std::move (loaded.buffer), loaded.sampleRate,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     juce::dsp::Convolution::Normalise::yes);

    juce::AudioBuffer<float> block (1, blockSize);
    const auto run = [&]
    {
        juce::dsp::AudioBlock<float> b (block);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (b));
    };

    // The new IR is installed on a background thread and crossfaded in.
    for (int i = 0; i < 400; ++i)
    {
        block.clear();
        run();
        juce::Thread::sleep (5);
    }

    std::vector<float> response;
    for (int b = 0; b < 40; ++b)
    {
        block.clear();
        if (b == 0)
            block.setSample (0, 0, 1.0f);
        run();
        for (int i = 0; i < blockSize; ++i)
            response.push_back (block.getSample (0, i));
    }

    int peakAt = 0;
    double firstHalf = 0.0, secondHalf = 0.0, energy = 0.0;
    for (int i = 0; i < (int) response.size(); ++i)
    {
        const double v = response[(size_t) i];
        if (std::abs (response[(size_t) i]) > std::abs (response[(size_t) peakAt]))
            peakAt = i;
        energy += v * v;
        if (i >= 400 && i < 1400) firstHalf += v * v;
        if (i >= 1400 && i < 2400) secondHalf += v * v;
    }

    std::printf ("peak index %d, peak %.4f, energy %.4f\n", peakAt, response[(size_t) peakAt], energy);
    check ("response has energy", energy > 1.0e-4);
    check ("peak near the start, with no added latency", peakAt < 6);
    check ("response decays like the file", firstHalf > secondHalf * 2.0);

    dir.deleteRecursively();
    std::printf ("\n%d check(s) failed\n", failures);
    return failures;
}
