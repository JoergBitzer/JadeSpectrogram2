#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// A button that looks like a TextButton (same background, colours and toggle highlight from the
// LookAndFeel) but shows a drawn icon instead of text. drawIcon gets the area for the icon and the
// colour to use (TextButton::textColourOnId / textColourOffId, depending on the toggle state).
class IconButton : public juce::Button
{
public:
    explicit IconButton(const juce::String& name = {}) : juce::Button(name) {}

    std::function<void(juce::Graphics&, juce::Rectangle<float>, juce::Colour)> drawIcon;

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto& lf = getLookAndFeel();
        const auto background = findColour(getToggleState() ? juce::TextButton::buttonOnColourId
                                                             : juce::TextButton::buttonColourId);
        lf.drawButtonBackground(g, *this, background, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        if (drawIcon)
        {
            const auto colour = findColour(getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                                    .withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f);
            const float size = 0.6f*static_cast<float>(juce::jmin(getWidth(), getHeight()));
            drawIcon(g, getLocalBounds().toFloat().withSizeKeepingCentre(size, size), colour);
        }
    }
};
