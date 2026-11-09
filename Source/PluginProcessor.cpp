#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Id = juce::ParameterID;
    // Knobs store 0..1 but read 0..10, matching the numbers printed on the panel.
    const auto dial = juce::AudioParameterFloatAttributes()
                          .withStringFromValueFunction ([] (float v, int) { return juce::String (v * 10.0f, 1); })
                          .withValueFromStringFunction ([] (const juce::String& s) { return juce::jlimit (0.0f, 1.0f, s.getFloatValue() / 10.0f); });

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Float> (Id { "pre", 1 }, "Pre", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "rhythmpre", 1 }, "Rhythm Pre", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "rhythmpost", 1 }, "Rhythm Post", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f, dial));
    layout.add (std::make_unique<juce::AudioParameterChoice> (Id { "channel", 1 }, "Channel",
                                                              juce::StringArray { "Rhythm", "Lead" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (Id { "crunch", 1 }, "Crunch", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (Id { "power", 1 }, "Power", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (Id { "standby", 1 }, "Standby", true));
    layout.add (std::make_unique<Float> (Id { "low", 1 }, "Low", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "mid", 1 }, "Mid", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "high", 1 }, "High", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "post", 1 }, "Post", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f, dial));
    layout.add (std::make_unique<Float> (Id { "resonance", 1 }, "Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "presence", 1 }, "Presence", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, dial));
    layout.add (std::make_unique<Float> (Id { "calibration", 1 }, "Input Calibration",
                                         juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (Id { "cabmode", 1 }, "Cabinet",
                                                              juce::StringArray { "Off", "Built-in", "Impulse" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (Id { "tuner", 1 }, "Tuner", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (Id { "tunermute", 1 }, "Tuner Mute", true));
    layout.add (std::make_unique<Float> (Id { "a4", 1 }, "Tuner A4", juce::NormalisableRange<float> (430.0f, 450.0f, 1.0f), 440.0f));
    return layout;
}
} // namespace

Stack5150Processor::Stack5150Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS", createLayout())
{
}

bool Stack5150Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

amp::Controls Stack5150Processor::readControls() const
{
    const auto value = [this] (const char* id) { return (double) params.getRawParameterValue (id)->load(); };
    amp::Controls c;
    c.pre = value ("pre");
    c.low = value ("low");
    c.mid = value ("mid");
    c.high = value ("high");
    c.post = value ("post");
    c.resonance = value ("resonance");
    c.presence = value ("presence");
    c.rhythmPre = value ("rhythmpre");
    c.rhythmPost = value ("rhythmpost");
    c.lead = value ("channel") > 0.5;
    c.crunch = value ("crunch") > 0.5;
    // Impulse mode uses the built-in cabinet until an impulse has been loaded.
    const int mode = juce::roundToInt (value ("cabmode"));
    c.cab = mode == 1 || (mode == 2 && ! impulseReady.load());
    return c;
}

void Stack5150Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    oversampling->reset();
    setLatencySamples ((int) std::lround (oversampling->getLatencyInSamples()));

    mono.setSize (1, samplesPerBlock);
    model.prepare (sampleRate * 4.0);
    model.setControls (readControls());
    convolution.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 1 });
    tuner.prepare (sampleRate);
    ampGate.reset (sampleRate, 0.03);
    ampGate.setCurrentAndTargetValue (isOutputOpen() ? 1.0f : 0.0f);
}

// Power and Standby both have to be on for the amp to make any sound, and the tuner can mute it.
bool Stack5150Processor::isOutputOpen() const
{
    const auto on = [this] (const char* id) { return params.getRawParameterValue (id)->load() > 0.5f; };
    return on ("power") && on ("standby") && ! (on ("tuner") && on ("tunermute"));
}

juce::String Stack5150Processor::loadImpulse (const juce::File& file)
{
    auto loaded = readImpulseFile (file);

    if (loaded.error.isNotEmpty())
    {
        impulseError = loaded.error;
        return loaded.error;
    }

    convolution.loadImpulseResponse (std::move (loaded.buffer), loaded.sampleRate,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     juce::dsp::Convolution::Normalise::yes);
    params.state.setProperty ("impulsePath", file.getFullPathName(), nullptr);
    impulseName = file.getFileName();
    impulseError.clear();
    impulseReady = true;
    return {};
}

void Stack5150Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int inputs = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (inputs == 0 || numSamples == 0)
        return;

    mono.setSize (1, numSamples, false, false, true);
    mono.copyFrom (0, 0, buffer, 0, 0, numSamples);
    for (int ch = 1; ch < inputs; ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, numSamples);
    if (inputs > 1)
        mono.applyGain (1.0f / (float) inputs);

    if (params.getRawParameterValue ("tuner")->load() > 0.5f)
        tuner.push (mono.getReadPointer (0), numSamples);

    const auto cal = juce::Decibels::decibelsToGain ((double) params.getRawParameterValue ("calibration")->load());

    juce::dsp::AudioBlock<float> block (mono);
    auto up = oversampling->processSamplesUp (block);
    auto* samples = up.getChannelPointer (0);
    const int osSamples = (int) up.getNumSamples();

    for (int n = 0; n < osSamples; ++n)
    {
        if (n % (controlInterval * 4) == 0)
            model.setControls (readControls());
        samples[n] = (float) model.process (samples[n] * cal);
    }

    oversampling->processSamplesDown (block);

    if (juce::roundToInt (params.getRawParameterValue ("cabmode")->load()) == 2 && impulseReady.load())
    {
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        block.multiplyBy (4.0f); // JUCE normalises impulses to a low level; +12 dB restores it
    }

    ampGate.setTargetValue (isOutputOpen() ? 1.0f : 0.0f);
    auto* out = mono.getWritePointer (0);
    for (int n = 0; n < numSamples; ++n)
        out[n] *= ampGate.getNextValue();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, mono, 0, 0, numSamples);
}

juce::AudioProcessorEditor* Stack5150Processor::createEditor()
{
    return new Stack5150Editor (*this);
}

void Stack5150Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void Stack5150Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            restoreState (*xml);
}

void Stack5150Processor::restoreState (const juce::XmlElement& xml)
{
    params.replaceState (juce::ValueTree::fromXml (xml));
    impulseReady = false;
    impulseName.clear();
    impulseError.clear();

    const juce::String path = params.state.getProperty ("impulsePath").toString();
    if (path.isNotEmpty() && loadImpulse (juce::File (path)).isNotEmpty())
        impulseName = juce::File (path).getFileName();
}

juce::File Stack5150Processor::getPresetFolder()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Stack 5150 Presets");
    folder.createDirectory();
    return folder;
}

juce::File Stack5150Processor::presetFileForName (const juce::File& folder, const juce::String& name)
{
    return folder.getChildFile (juce::File::createLegalFileName (name)).withFileExtension ("s5150");
}

juce::String Stack5150Processor::savePreset (const juce::File& file)
{
    const auto xml = params.copyState().createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return "The preset file could not be written";
    return {};
}

juce::String Stack5150Processor::loadPreset (const juce::File& file)
{
    if (! file.existsAsFile())
        return "Preset file not found";

    const auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return "This is not a Stack 5150 preset";

    // Loading a sound should never switch the amp off or change the tuner.
    const std::array<const char*, 5> keep { "power", "standby", "tuner", "tunermute", "a4" };
    std::array<float, 5> kept {};
    for (size_t i = 0; i < keep.size(); ++i)
        kept[i] = params.getParameter (keep[i])->getValue();

    restoreState (*xml);

    for (size_t i = 0; i < keep.size(); ++i)
        params.getParameter (keep[i])->setValueNotifyingHost (kept[i]);
    return {};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Stack5150Processor();
}
