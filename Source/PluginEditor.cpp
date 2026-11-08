#include "PluginEditor.h"

#include "AmpFace.h"

namespace
{
const std::array<const char*, 9> ids { "rhythmpre", "pre", "low", "mid", "high", "rhythmpost", "post", "resonance", "presence" };

juce::Point<float> polar (juce::Point<float> c, float r, float a)
{
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

juce::Font faceFont (float size, bool bold = true)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

bool isImpulseFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac");
}

float knobStep (float a, float b, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Half-width of the knob at position v along its length. The tip is at v = -1.14.
float knobHalfWidth (float v)
{
    if (v < -1.14f || v > 0.92f)
        return 0.0f;
    if (v < -0.02f)
        return 0.64f * std::pow ((v + 1.14f) / 1.12f, 1.8f);
    const float s = (v + 0.02f) / 0.94f;
    return 0.64f + 0.24f * std::sin (juce::MathConstants<float>::pi * s * 0.9f) - 0.22f * s * s;
}

// Height of the moulded surface: a dome either side of a raised central spine.
float knobHeight (float u, float v)
{
    const float w = knobHalfWidth (v);
    if (w <= 0.001f)
        return 0.0f;
    const float a = std::abs (u) / w;
    if (a >= 1.0f)
        return 0.0f;
    const float dome = std::sqrt (1.0f - a * a);
    const float base = (0.30f + 0.62f * knobStep (-1.14f, -0.15f, v)) * (1.0f - 0.25f * knobStep (0.55f, 0.92f, v));
    const float spineWidth = 0.06f + 0.13f * (v + 1.14f) / 2.06f;
    const float spine = (1.0f - knobStep (0.55f * spineWidth, spineWidth, std::abs (u))) * 0.09f * knobStep (-1.10f, -0.60f, v);
    return base * dome + spine * std::sqrt (dome);
}

// A photo studio seen in the gloss: a large soft box, a side strip, a warm room below.
void knobEnvironment (float rx, float ry, float& r, float& g, float& b)
{
    const float x = rx + 0.05f * std::sin (ry * 11.0f + rx * 5.0f);
    const float y = ry + 0.05f * std::sin (rx * 9.0f);
    const auto box = [] (float d, float half, float soft) { return 1.0f - knobStep (half - soft, half + soft, std::abs (d)); };

    const float main = box (x + 0.34f, 0.34f, 0.10f) * box (y + 0.30f, 0.30f, 0.10f) * 1.15f;
    const float side = box (x - 0.50f, 0.09f, 0.06f) * box (y + 0.05f, 0.42f, 0.10f) * 0.5f;
    const float floor = knobStep (0.25f, 1.25f, y) * 0.55f;

    r = 0.02f + main * 1.0f + side * 0.95f + floor * 0.30f;
    g = 0.02f + main * 0.97f + side * 0.92f + floor * 0.20f;
    b = 0.022f + main * 0.92f + side * 0.86f + floor * 0.13f;
}

// Chicken-head footprint, pointing up.
juce::Path chickenBody (float r)
{
    juce::Path right, p;
    p.startNewSubPath (0.0f, -r * 1.14f);
    for (int i = 1; i <= 40; ++i)
    {
        const float v = -1.14f + 2.06f * (float) i / 40.0f;
        p.lineTo (knobHalfWidth (v) * r, v * r);
    }
    for (int i = 40; i >= 1; --i)
    {
        const float v = -1.14f + 2.06f * (float) i / 40.0f;
        p.lineTo (-knobHalfWidth (v) * r, v * r);
    }
    p.closeSubPath();
    return p;
}
} // namespace

void AmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.43f;
    const auto centre = area.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const auto world = slider.getBoundsInParent().getCentre().toFloat();
    const float lit = juce::jlimit (0.25f, 1.0f, face::lightAt (world.x, world.y) * 1.4f);
    const float co = std::cos (angle), si = std::sin (angle);

    auto outline = chickenBody (radius);
    outline.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    for (int i = 3; i >= 1; --i)
    {
        auto shadow = outline;
        shadow.applyTransform (juce::AffineTransform::translation (0.9f * (float) i, 1.4f * (float) i));
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillPath (shadow);
    }

    // Each pixel is shaded from the surface normal, so the reflections slide over the shape as it turns.
    const int scale = 2, w = width * scale, h = height * scale;
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        const float rp = radius * (float) scale, cx = (centre.x - (float) x) * (float) scale, cy = (centre.y - (float) y) * (float) scale;
        const float e = 0.012f;

        for (int py = 0; py < h; ++py)
        {
            for (int px = 0; px < w; ++px)
            {
                const float dx = ((float) px + 0.5f - cx) / rp, dy = ((float) py + 0.5f - cy) / rp;
                const float u = dx * co + dy * si, v = -dx * si + dy * co;
                const float hw = knobHalfWidth (v);
                const float edge = juce::jmin (hw - std::abs (u), 0.92f - v, v + 1.14f);
                if (edge <= 0.0f)
                    continue;

                const float nu = -(knobHeight (u + e, v) - knobHeight (u - e, v)) / (2.0f * e);
                const float nv = -(knobHeight (u, v + e) - knobHeight (u, v - e)) / (2.0f * e);
                const float sx0 = co * nu - si * nv, sy0 = si * nu + co * nv;
                const float len = 1.0f / std::sqrt (sx0 * sx0 + sy0 * sy0 + 1.0f);
                const float nx = sx0 * len, ny = sy0 * len, nz = len;

                float er, eg, eb;
                knobEnvironment (2.0f * nz * nx, 2.0f * nz * ny, er, eg, eb);

                const float fresnel = 0.16f + 0.84f * std::pow (1.0f - nz, 3.0f);
                const float amount = fresnel * (0.5f + 0.7f * lit);
                float r = 0.010f + er * amount, gr = 0.010f + eg * amount, b = 0.011f + eb * amount;

                // White inlay near the tip, just to one side of the spine's crest.
                const float line = (1.0f - knobStep (0.018f, 0.038f, std::abs (u))) * knobStep (-1.12f, -1.05f, v) * (1.0f - knobStep (-0.62f, -0.48f, v));
                const float ink = 0.72f * (0.5f + 0.6f * lit);
                r += (ink - r) * line;
                gr += (ink * 0.97f - gr) * line;
                b += (ink * 0.86f - b) * line;

                const float alpha = juce::jlimit (0.0f, 1.0f, edge * rp);
                const auto encode = [] (float value) { return (juce::uint8) juce::roundToInt (255.0f * std::sqrt (juce::jlimit (0.0f, 1.0f, value))); };
                juce::PixelARGB pixel (encode (alpha * alpha), encode (r), encode (gr), encode (b));
                pixel.setAlpha ((juce::uint8) juce::roundToInt (alpha * 255.0f));
                pixel.premultiply();
                *(juce::PixelARGB*) data.getPixelPointer (px, py) = pixel;
            }
        }
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (1.0f); // drawImage uses the opacity left by the shadow colour
    g.drawImage (image, area);

    // Handling scuffs turn with the knob.
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (outline);
    juce::Random wear ((juce::int64) (world.x * 13.0f));
    for (int i = 0; i < 30; ++i)
    {
        const float a = angle + wear.nextFloat() * juce::MathConstants<float>::twoPi;
        const auto from = polar (centre, wear.nextFloat() * radius * 0.95f, a);
        const auto to = polar (from, 1.5f + wear.nextFloat() * radius * 0.45f, a + 1.2f + wear.nextFloat());
        g.setColour (juce::Colours::white.withAlpha ((0.04f + wear.nextFloat() * 0.13f) * lit));
        g.drawLine ({ from, to }, 0.45f);
    }
}

void RockerSwitch::paintButton (juce::Graphics& g, bool, bool down)
{
    auto b = getLocalBounds().toFloat();
    const auto world = getBoundsInParent().toFloat();
    const float lit = juce::jlimit (0.25f, 1.0f, face::lightAt (world.getCentreX(), world.getCentreY()) * 1.4f);
    const bool on = getToggleState();

    g.setColour (juce::Colour (0xff060607));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (b.reduced (0.5f), 2.0f, 0.8f);

    // The pressed half sits low and in shadow; the raised half catches the light.
    auto cap = b.reduced (4.0f, 4.0f);
    auto upper = cap.removeFromTop (cap.getHeight() * 0.5f);
    auto lower = cap;
    const float raised = (down ? 0.20f : 0.26f) * (0.45f + 0.75f * lit), sunk = 0.05f * (0.5f + 0.7f * lit);
    const float topLevel = on ? raised : sunk, bottomLevel = on ? sunk : raised;

    g.setGradientFill (juce::ColourGradient (juce::Colour::greyLevel (topLevel * 1.5f), upper.getX(), upper.getY(),
                                             juce::Colour::greyLevel (topLevel * 0.5f), upper.getX(), upper.getBottom(), false));
    g.fillRoundedRectangle (upper, 1.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour::greyLevel (bottomLevel * 0.7f), lower.getX(), lower.getY(),
                                             juce::Colour::greyLevel (bottomLevel * 1.3f), lower.getX(), lower.getBottom(), false));
    g.fillRoundedRectangle (lower, 1.5f);

    g.setColour (juce::Colours::black);
    g.drawHorizontalLine ((int) lower.getY(), lower.getX(), lower.getRight());

    g.setColour (juce::Colours::white.withAlpha (0.75f * lit));
    g.setFont (faceFont (10.0f));
    g.drawText ("I", upper, juce::Justification::centred);
    g.drawText ("O", lower, juce::Justification::centred);
}

void CreamButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const bool pressed = down || getToggleState();
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto world = getBoundsInParent().toFloat();
    const float lit = juce::jlimit (0.2f, 1.0f, face::lightAt (world.getCentreX(), world.getCentreY()) * 1.4f);

    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRoundedRectangle (b, 1.5f);

    b = b.reduced (1.5f).translated (0.0f, pressed ? 1.0f : -1.0f);
    const float v = (pressed ? 0.55f : 0.72f) * (0.45f + 0.75f * lit);
    const auto top = juce::Colour::fromFloatRGBA (v * 1.08f, v * 1.04f, v * 0.88f, 1.0f);
    const auto bottom = juce::Colour::fromFloatRGBA (v * 0.74f, v * 0.68f, v * 0.56f, 1.0f);
    g.setGradientFill (juce::ColourGradient (top, b.getX(), b.getY(), bottom, b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 1.5f);

    // Grime in the finish
    juce::Random dirt (73);
    for (int i = 0; i < 26; ++i)
    {
        g.setColour (juce::Colour (0xff3a2d18).withAlpha (0.05f + dirt.nextFloat() * 0.16f));
        const float px = b.getX() + dirt.nextFloat() * b.getWidth(), py = b.getY() + dirt.nextFloat() * b.getHeight();
        g.fillEllipse (px, py, 1.2f, 1.2f);
    }
    g.setColour (juce::Colours::white.withAlpha (pressed ? 0.15f : 0.4f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + 2.0f, b.getRight() - 2.0f);
}

void Stack5150Editor::addKnob (Knob& knob, const char* paramId)
{
    auto& s = knob.slider;
    s.setLookAndFeel (&lookAndFeel);
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setRotaryParameters (face::rotaryStart, face::rotaryEnd, true);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setPopupDisplayEnabled (true, false, this);
    addAndMakeVisible (s);
    knob.attachment = std::make_unique<SliderAttachment> (processor.params, paramId, s);
}

Stack5150Editor::Stack5150Editor (Stack5150Processor& p) : AudioProcessorEditor (p), processor (p)
{
    for (size_t i = 0; i < knobs.size(); ++i)
        addKnob (knobs[i], ids[i]);
    addKnob (calibration, "calibration");

    addAndMakeVisible (standbySwitch);
    addAndMakeVisible (powerSwitch);
    standbyAttachment = std::make_unique<ButtonAttachment> (p.params, "standby", standbySwitch);
    powerAttachment = std::make_unique<ButtonAttachment> (p.params, "power", powerSwitch);

    addAndMakeVisible (channelSwitch);
    addAndMakeVisible (crunchSwitch);
    channelSwitch.setTooltip ("Channel: pressed = Lead");
    crunchSwitch.setTooltip ("Crunch (rhythm channel)");
    channelAttachment = std::make_unique<ButtonAttachment> (p.params, "channel", channelSwitch);
    crunchAttachment = std::make_unique<ButtonAttachment> (p.params, "crunch", crunchSwitch);

    cabMode.addItemList ({ "Off", "Built-in", "Impulse" }, 1);
    cabMode.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2b2e));
    cabMode.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff050505));
    addAndMakeVisible (cabMode);
    cabAttachment = std::make_unique<ComboAttachment> (p.params, "cabmode", cabMode);

    loadButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2b2e));
    loadButton.onClick = [this] { chooseImpulse(); };
    addAndMakeVisible (loadButton);

    presetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2b2e));
    presetBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff050505));
    presetBox.setTextWhenNothingSelected ("No preset");
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);
    savePreset.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2b2e));
    deletePreset.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2b2e));
    savePreset.onClick = [this] { promptSave(); };
    deletePreset.onClick = [this] { promptDelete(); };
    addAndMakeVisible (savePreset);
    addAndMakeVisible (deletePreset);
    refreshPresets ({});

    impulseLabel.setFont (faceFont (12.0f, false));
    impulseLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff070808));
    impulseLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff030303));
    impulseLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (impulseLabel);

    setSize (1200, 470);
    startTimerHz (10);
    timerCallback();
}

Stack5150Editor::~Stack5150Editor()
{
    for (auto& k : knobs)
        k.slider.setLookAndFeel (nullptr);
    calibration.slider.setLookAndFeel (nullptr);
}

void Stack5150Editor::timerCallback()
{
    const bool lead = processor.params.getRawParameterValue ("channel")->load() > 0.5f;
    const bool power = processor.params.getRawParameterValue ("power")->load() > 0.5f;
    const bool standby = processor.params.getRawParameterValue ("standby")->load() > 0.5f;
    if (lead != leadOn || power != powerOn || standby != standbyOn)
    {
        leadOn = lead;
        powerOn = power;
        standbyOn = standby;
        if (! powerOn && ! faceImageDark.isValid())
            faceImageDark = face::render (faceLayout, 2.0f, false);
        repaint();
    }

    const auto error = processor.getImpulseError();
    const auto name = processor.getImpulseName();
    if (error.isNotEmpty())
    {
        impulseLabel.setColour (juce::Label::textColourId, juce::Colour (0xffff7a66));
        impulseLabel.setText (name.isNotEmpty() ? name + ": " + error : error, juce::dontSendNotification);
    }
    else
    {
        impulseLabel.setColour (juce::Label::textColourId, juce::Colour (0xff66d68a));
        impulseLabel.setText (name.isNotEmpty() ? name : "Drop an impulse file here, or press LOAD IR", juce::dontSendNotification);
    }
}

void Stack5150Editor::chooseImpulse()
{
    chooser = std::make_unique<juce::FileChooser> ("Load impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  load (file);
                          });
}

void Stack5150Editor::load (const juce::File& file)
{
    if (processor.loadImpulse (file).isEmpty())
        cabMode.setSelectedId (3, juce::sendNotificationSync);
    timerCallback();
}

void Stack5150Editor::refreshPresets (const juce::String& select)
{
    presetFiles = Stack5150Processor::getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.s5150");
    presetFiles.sort();

    presetBox.clear (juce::dontSendNotification);
    int selectedId = 0;
    for (int i = 0; i < presetFiles.size(); ++i)
    {
        const auto name = presetFiles[i].getFileNameWithoutExtension();
        presetBox.addItem (name, i + 1);
        if (name == select)
            selectedId = i + 1;
    }
    presetBox.setSelectedId (selectedId, juce::dontSendNotification);
    deletePreset.setEnabled (selectedId != 0);
}

void Stack5150Editor::presetChosen()
{
    const int index = presetBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, presetFiles.size()))
        return;

    deletePreset.setEnabled (true);
    const auto error = processor.loadPreset (presetFiles[index]);
    if (error.isNotEmpty())
        showDialog ("Could not load preset", error, false, nullptr);
    timerCallback();
}

void Stack5150Editor::showDialog (const juce::String& title, const juce::String& message, bool askConfirm, std::function<void()> onConfirm)
{
    dialog = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon, this);
    dialog->addButton (askConfirm ? "Yes" : "OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (askConfirm)
        dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<Stack5150Editor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, onConfirm] (int result)
    {
        if (safe == nullptr)
            return;
        safe->dialog->setVisible (false);
        if (result == 1 && onConfirm != nullptr)
            onConfirm();
    }), false);
}

void Stack5150Editor::promptSave()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", presetBox.getText());
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<Stack5150Editor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr)
            return;
        safe->dialog->setVisible (false);
        if (result != 1)
            return;

        const auto name = safe->dialog->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;

        juce::MessageManager::callAsync ([safe, name]
        {
            if (safe == nullptr)
                return;
            const auto file = Stack5150Processor::presetFileForName (Stack5150Processor::getPresetFolder(), name);
            if (file.existsAsFile())
                safe->showDialog ("Replace preset?", "\"" + file.getFileNameWithoutExtension() + "\" already exists.", true,
                                  [safe, file] { if (safe != nullptr) safe->commitSave (file); });
            else
                safe->commitSave (file);
        });
    }), false);
}

void Stack5150Editor::commitSave (const juce::File& file)
{
    const auto error = processor.savePreset (file);
    if (error.isNotEmpty())
    {
        juce::Component::SafePointer<Stack5150Editor> safe (this);
        juce::MessageManager::callAsync ([safe, error]
        {
            if (safe != nullptr)
                safe->showDialog ("Could not save preset", error, false, nullptr);
        });
        return;
    }
    refreshPresets (file.getFileNameWithoutExtension());
}

void Stack5150Editor::promptDelete()
{
    const int index = presetBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, presetFiles.size()))
        return;

    const auto file = presetFiles[index];
    juce::Component::SafePointer<Stack5150Editor> safe (this);
    showDialog ("Delete preset?", "Delete \"" + file.getFileNameWithoutExtension() + "\"? This cannot be undone.", true,
                [safe, file]
                {
                    file.deleteFile();
                    if (safe != nullptr)
                        safe->refreshPresets ({});
                });
}

bool Stack5150Editor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isImpulseFile (files[0]);
}

void Stack5150Editor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint();
}

void Stack5150Editor::fileDragExit (const juce::StringArray&)
{
    dragActive = false;
    repaint();
}

void Stack5150Editor::filesDropped (const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();
    load (juce::File (files[0]));
}

void Stack5150Editor::drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on) const
{
    const auto c = r.getCentre();
    g.setColour (juce::Colour (0xff060606));
    g.fillRoundedRectangle (r.expanded (2.5f), 1.5f);

    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f), c.x, c.y, colour.withAlpha (0.0f), c.x + 22.0f, c.y, true));
        g.fillEllipse (c.x - 22.0f, c.y - 22.0f, 44.0f, 44.0f);
    }
    g.setGradientFill (juce::ColourGradient (on ? colour.brighter (0.9f) : colour.withMultipliedBrightness (0.28f), c.x - 2.0f, c.y - 2.0f,
                                             on ? colour : colour.withMultipliedBrightness (0.12f), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.65f : 0.25f));
    g.fillRect (r.getX() + 2.0f, r.getY() + 1.5f, r.getWidth() * 0.45f, 1.2f);
}

void Stack5150Editor::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (powerOn ? faceImage : faceImageDark, getLocalBounds().toFloat());

    const auto red = juce::Colour (0xffff2a18), green = juce::Colour (0xff3cff58);
    drawLed (g, { 170.0f, 328.0f, 14.0f, 7.0f }, green, powerOn && ! leadOn);
    drawLed (g, { 288.0f, 289.0f, 14.0f, 7.0f }, red, powerOn && leadOn);
    drawLed (g, { 1000.0f, 304.0f, 14.0f, 7.0f }, red, powerOn);
    drawLed (g, { 955.0f, 304.0f, 14.0f, 7.0f }, green, powerOn && standbyOn);

    g.setColour (juce::Colour (0xffb9b5a8));
    g.setFont (faceFont (10.0f));
    g.drawText ("PRESET", presetBox.getBounds().translated (-62, 0).withWidth (56), juce::Justification::centredRight); g.drawText ("CABINET", cabMode.getBounds().translated (-66, 0).withWidth (60), juce::Justification::centredRight);
    g.drawText ("INPUT CAL", calibration.slider.getBounds().translated (-70, 0).withWidth (62), juce::Justification::centredRight);
}

void Stack5150Editor::paintOverChildren (juce::Graphics& g)
{
    if (dragActive)
    {
        g.setColour (juce::Colour (0x3366d68a));
        g.fillRect (getLocalBounds());
        g.setColour (juce::Colour (0xff66d68a));
        g.drawRect (getLocalBounds(), 3);
    }
}

void Stack5150Editor::resized()
{
    const int size = 54, cy = 332;
    const std::array<int, 9> centres { 240, 326, 412, 486, 560, 646, 720, 810, 884 };
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i].slider.setBounds (centres[i] - size / 2, cy - size / 2, size, size);

    standbySwitch.setBounds (1047, 308, 26, 44);
    powerSwitch.setBounds (1105, 308, 26, 44);
    channelSwitch.setBounds (164, 306, 26, 18);
    crunchSwitch.setBounds (164, 340, 26, 18);

    const int rowY = 425, plateH = 28;
    presetBox.setBounds (78, rowY, 164, plateH); savePreset.setBounds (250, rowY, 54, plateH); deletePreset.setBounds (310, rowY, 60, plateH); cabMode.setBounds (446, rowY, 104, plateH);
    loadButton.setBounds (558, rowY, 80, plateH);
    impulseLabel.setBounds (646, rowY, 382, plateH);
    calibration.slider.setBounds (1110, 419, 40, 40);

    auto& layout = faceLayout;
    layout.width = getWidth();
    layout.height = getHeight();
    for (size_t i = 0; i < knobs.size(); ++i)
        layout.knobs[i] = knobs[i].slider.getBounds();
    layout.channelButton = channelSwitch.getBounds();
    layout.crunchButton = crunchSwitch.getBounds();
    layout.utility = juce::Rectangle<int> (8, 415, 1184, 50);

    faceImage = face::render (layout, 2.0f, true);
    faceImageDark = {};
}
