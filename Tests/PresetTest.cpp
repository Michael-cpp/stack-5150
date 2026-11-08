// Round-trips presets through the real processor. Exit code is the number of failures.
#include <cstdio>

#include "PluginProcessor.h"

static int failures = 0;

static void check (const char* name, bool ok)
{
    std::printf ("%-58s %s\n", name, ok ? "ok" : "FAIL");
    failures += ok ? 0 : 1;
}

static void setValue (Stack5150Processor& p, const char* id, float plain)
{
    auto* param = p.params.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

static float value (Stack5150Processor& p, const char* id)
{
    return p.params.getRawParameterValue (id)->load();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("stack5150_presettest");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto file = dir.getChildFile ("Tone.s5150");

    Stack5150Processor p;
    setValue (p, "pre", 0.73f);
    setValue (p, "low", 0.21f);
    setValue (p, "presence", 0.88f);
    setValue (p, "calibration", -6.5f);
    setValue (p, "channel", 0.0f);
    setValue (p, "crunch", 1.0f);
    setValue (p, "cabmode", 0.0f);

    check ("save succeeds", p.savePreset (file).isEmpty());
    check ("preset file written", file.existsAsFile());

    setValue (p, "pre", 0.1f);
    setValue (p, "low", 0.9f);
    setValue (p, "presence", 0.2f);
    setValue (p, "calibration", 3.0f);
    setValue (p, "channel", 1.0f);
    setValue (p, "crunch", 0.0f);
    setValue (p, "cabmode", 2.0f);

    check ("load succeeds", p.loadPreset (file).isEmpty());
    check ("knob restored", std::abs (value (p, "pre") - 0.73f) < 0.001f);
    check ("second knob restored", std::abs (value (p, "low") - 0.21f) < 0.001f);
    check ("third knob restored", std::abs (value (p, "presence") - 0.88f) < 0.001f);
    check ("calibration restored", std::abs (value (p, "calibration") + 6.5f) < 0.11f);
    check ("channel restored", value (p, "channel") < 0.5f);
    check ("crunch restored", value (p, "crunch") > 0.5f);
    check ("cabinet mode restored", value (p, "cabmode") < 0.5f);

    // A preset opened in a fresh instance, as after restarting the DAW.
    Stack5150Processor other;
    check ("loads in a new instance", other.loadPreset (file).isEmpty());
    check ("new instance has the setting", std::abs (value (other, "pre") - 0.73f) < 0.001f);

    const float before = value (p, "pre");
    const auto notXml = dir.getChildFile ("bad.s5150");
    notXml.replaceWithText ("this is not a preset");
    check ("non-XML file rejected", p.loadPreset (notXml).isNotEmpty());

    const auto wrong = dir.getChildFile ("wrong.s5150");
    wrong.replaceWithText ("<OTHER a=\"1\"/>");
    check ("XML from something else rejected", p.loadPreset (wrong).isNotEmpty());

    check ("missing file rejected", p.loadPreset (dir.getChildFile ("nope.s5150")).isNotEmpty());
    check ("rejected files leave settings alone", std::abs (value (p, "pre") - before) < 0.001f);

    // The impulse path travels with the preset.
    {
        juce::MemoryOutputStream o;
        const int frames = 2000;
        o.write ("RIFF", 4); o.writeInt (36 + frames * 2); o.write ("WAVEfmt ", 8);
        o.writeInt (16); o.writeShort (1); o.writeShort (1); o.writeInt (44100); o.writeInt (88200); o.writeShort (2); o.writeShort (16);
        o.write ("data", 4); o.writeInt (frames * 2);
        for (int i = 0; i < frames; ++i)
            o.writeShort ((short) (20000.0 * std::exp (-i / 200.0)));
        const auto ir = dir.getChildFile ("My Cab.wav");
        ir.replaceWithData (o.getData(), o.getDataSize());

        Stack5150Processor withIr;
        check ("impulse loads", withIr.loadImpulse (ir).isEmpty());
        const auto presetFile = dir.getChildFile ("WithIr.s5150");
        check ("preset with impulse saves", withIr.savePreset (presetFile).isEmpty());
        check ("preset file contains the path", presetFile.loadFileAsString().contains ("My Cab.wav"));

        Stack5150Processor fresh;
        check ("preset with impulse loads", fresh.loadPreset (presetFile).isEmpty());
        check ("impulse restored from preset", fresh.getImpulseName() == "My Cab.wav" && fresh.getImpulseError().isEmpty());

        ir.deleteFile();
        Stack5150Processor missing;
        check ("preset still loads if the impulse moved", missing.loadPreset (presetFile).isEmpty());
        check ("moved impulse is reported", missing.getImpulseError().isNotEmpty() && missing.getImpulseName() == "My Cab.wav");
    }

    // Power and Standby really silence the amp, and presets leave them alone.
    {
        Stack5150Processor amp;
        amp.prepareToPlay (44100.0, 512);
        juce::MidiBuffer midi;
        const auto level = [&]
        {
            juce::AudioBuffer<float> b (2, 512);
            float rms = 0.0f;
            for (int block = 0; block < 12; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto v = 0.1f * (float) std::sin (0.03 * i);
                    b.setSample (0, i, v);
                    b.setSample (1, i, v);
                }
                amp.processBlock (b, midi);
                rms = b.getRMSLevel (0, 0, 512);
            }
            return rms;
        };

        check ("amp makes sound when on", level() > 0.001f);
        setValue (amp, "standby", 0.0f);
        check ("standby off is silent", level() == 0.0f);
        setValue (amp, "standby", 1.0f);
        check ("standby on restores sound", level() > 0.001f);
        setValue (amp, "power", 0.0f);
        check ("power off is silent", level() == 0.0f);

        const auto presetFile = dir.getChildFile ("Sound.s5150");
        setValue (amp, "power", 1.0f);
        amp.savePreset (presetFile);
        setValue (amp, "standby", 0.0f);
        amp.loadPreset (presetFile);
        check ("preset does not change standby", value (amp, "standby") < 0.5f);
    }

    // Value popups read on the panel's 0-10 scale.
    {
        Stack5150Processor amp;
        setValue (amp, "low", 0.4f);
        auto* low = amp.params.getParameter ("low");
        check ("0.40 displays as 4.0", low->getCurrentValueAsText() == "4.0");
        check ("typed 7.5 sets 0.75", std::abs (low->getValueForText ("7.5") - 0.75f) < 0.001f);
        check ("stored value stays 0..1", std::abs (value (amp, "low") - 0.4f) < 0.001f);
    }

    check ("unsafe name is cleaned", Stack5150Processor::presetFileForName (dir, "a/b:c").getParentDirectory() == dir);

    dir.deleteRecursively();
    std::printf ("\n%d check(s) failed\n", failures);
    return failures;
}
