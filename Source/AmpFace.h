// Procedural, physically lit front of the amp. Generated from fixed seeds,
// so no image files are needed and the wear is the same on every open.
#pragma once

#include <array>
#include <cmath>

#include <juce_graphics/juce_graphics.h>

namespace face
{
constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.2f;
constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.8f;

constexpr float lightX = 500.0f, lightY = -150.0f, lightZ = 640.0f;

inline float lightAt (float x, float y)
{
    const float dx = x - lightX, dy = y - lightY;
    const float d2 = dx * dx + dy * dy + lightZ * lightZ;
    return (lightZ * lightZ / d2) * (lightZ / std::sqrt (d2));
}

struct Layout
{
    int width = 0, height = 0;
    std::array<juce::Rectangle<int>, 9> knobs;
    juce::Rectangle<int> channelButton, crunchButton, utility;
};

juce::Image render (const Layout&, float scale, bool tubesLit);
} // namespace face
