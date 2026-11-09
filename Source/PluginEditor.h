#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

#include "AmpFace.h"
#include "PluginProcessor.h"

class AmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float position, float startAngle, float endAngle, juce::Slider&) override;
};

class CreamButton : public juce::Button
{
public:
    CreamButton() : juce::Button ("switch") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class RockerSwitch : public juce::Button
{
public:
    RockerSwitch() : juce::Button ("rocker") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// Invisible layer over the amp while the tuner is open, so an outside click only closes it.
class ClickCatcher : public juce::Component
{
public:
    void mouseDown (const juce::MouseEvent&) override { if (onClick != nullptr) onClick(); }
    std::function<void()> onClick;
};

class TunerPanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit TunerPanel (Stack5150Processor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void()> onClose;

private:
    void timerCallback() override;

    Stack5150Processor& processor;
    tools::TunerReading reading;
    bool analyseThisTick = false;
    juce::ToggleButton mute { "Mute output" };
    juce::Slider a4;
    juce::AudioProcessorValueTreeState::ButtonAttachment muteAttachment;
    juce::AudioProcessorValueTreeState::SliderAttachment a4Attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerPanel)
};

class Stack5150Editor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit Stack5150Editor (Stack5150Processor&);
    ~Stack5150Editor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void chooseImpulse();
    void load (const juce::File&);
    void addKnob (Knob&, const char* paramId);
    void refreshPresets (const juce::String& select);
    void presetChosen();
    void promptSave();
    void commitSave (const juce::File&);
    void promptDelete();
    void showDialog (const juce::String& title, const juce::String& message, bool askConfirm, std::function<void()> onConfirm);
    void drawLed (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool on) const;

    Stack5150Processor& processor;
    AmpLookAndFeel lookAndFeel;
    std::array<Knob, 9> knobs;
    Knob calibration;
    CreamButton channelSwitch, crunchSwitch;
    RockerSwitch standbySwitch, powerSwitch;
    std::unique_ptr<ButtonAttachment> standbyAttachment, powerAttachment;
    std::unique_ptr<ButtonAttachment> channelAttachment, crunchAttachment;
    juce::ComboBox cabMode;
    std::unique_ptr<ComboAttachment> cabAttachment;
    juce::ComboBox presetBox;
    juce::TextButton savePreset { "SAVE" }, deletePreset { "DELETE" };
    juce::Array<juce::File> presetFiles;
    std::unique_ptr<juce::AlertWindow> dialog;
    juce::TextButton loadButton { "LOAD IR" };
    juce::Label impulseLabel;
    juce::TextButton tunerButton { "TUNER" };
    std::unique_ptr<ButtonAttachment> tunerAttachment;
    ClickCatcher tunerBackdrop;
    TunerPanel tunerPanel { processor };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image faceImage, faceImageDark;
    face::Layout faceLayout;
    bool dragActive = false, leadOn = true, powerOn = true, standbyOn = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Stack5150Editor)
};
