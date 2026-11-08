#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "AmpModel.h"
#include "IrLoad.h"

class Stack5150Processor : public juce::AudioProcessor
{
public:
    Stack5150Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Stack 5150"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message thread. Returns an error message, or an empty string on success.
    juce::String loadImpulse (const juce::File&);
    juce::String getImpulseName() const { return impulseName; }
    juce::String getImpulseError() const { return impulseError; }

    // Presets hold every parameter and the impulse path. These return an error message, or an empty string.
    juce::String savePreset (const juce::File&);
    juce::String loadPreset (const juce::File&);
    static juce::File getPresetFolder();
    static juce::File presetFileForName (const juce::File& folder, const juce::String& name);

    juce::AudioProcessorValueTreeState params;

private:
    static constexpr int controlInterval = 32;

    amp::Controls readControls() const;
    void restoreState (const juce::XmlElement&);

    amp::Amp model;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> mono;
    juce::dsp::Convolution convolution;
    juce::SmoothedValue<float> ampGate;
    std::atomic<bool> impulseReady { false };
    juce::String impulseName, impulseError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Stack5150Processor)
};
