/**
 * @file JadeLookAndFeel.h
 * @author J. Bitzer at Jade Hochschule
 * @brief implements the look and feel for the Jade PlugINs (color schemes)
 * @version 0.1
 * @date 2021-01-11
 * 
 * @copyright Copyright (c) 2021
 * 
 */
/* ToDo:
	1. Implement more custom GUIS 
*/

#pragma once

//#include <JuceHeader.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @brief define the Jade color scheme given by the CD 
 */
const auto JadeWhite(juce::Colour::fromFloatRGBA(0.95f, 0.95f, 0.95f, 1.f));
const auto JadeRed(juce::Colour::fromFloatRGBA(0.890196078431373f, 0.023529411764706f, 0.074509803921569f, 1.f));
const auto JadeTeal(juce::Colour::fromFloatRGBA(0.352941176470588f, 0.372549019607843f, 0.337254901960784f, 1.f));
const auto JadeGray(juce::Colour::fromFloatRGBA(0.356862745098039f, 0.372549019607843f, 0.341176470588235f, 1.f));

const auto JadeLightRed1(juce::Colour::fromFloatRGBA(0.956862745098039f, 0.631372549019608f, 0.552941176470588f, 1.f));
const auto JadeLightRed2(juce::Colour::fromFloatRGBA(0.968627450980392f, 0.733333333333333f, 0.682352941176471f, 1.f));
const auto JadeLightRed3(juce::Colour::fromFloatRGBA(0.980392156862745f, 0.807843137254902f, 0.776470588235294f, 1.f));
// JadeLightRed4 = [0.988235294117647 0.890196078431373 0.870588235294118];
#define MIN_COMBO_WITH_PRESET 120
#define MIN_ELEMENT_DIST_PRESET 10
#define MIN_BUTTON_WIDTH_PRESET 40
#define MIN_ELEMENT_HEIGHT_PRESET 20
// Category Lists for different usage (max number of Cat is 20);
const juce::StringArray JadeSynthCategories("Unknown", "Lead", "Brass", "Template", "Bass",
	"Key", "Organ" , "Pad", "Drums_Perc", "SpecialEffect","Sequence", "String" );
/**
 * @brief the Look and Feel implements the rotary knob in a special way and adjust the font size according to scale
 * 
 */
class JadeLookAndFeel : public juce::LookAndFeel_V4
{
public:
	JadeLookAndFeel();
	void setFontSize(float newFontSize){m_fontSize = newFontSize;};

private:
	float m_fontSize;
	void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
		const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override;

void drawButtonText (juce::Graphics& g, juce::TextButton& button,
		bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
	{
		// Font font (button.getHeight () * 0.6f);
		juce::Font font (juce::FontOptions().withHeight (m_fontSize));

		g.setFont (font);
		g.setColour (button.findColour (button.getToggleState () ? juce::TextButton::textColourOnId
			: juce::TextButton::textColourOffId)
			.withMultipliedAlpha (button.isEnabled () ? 1.0f : 0.5f));

		const int yIndent = button.proportionOfHeight (0.1f);
		const int cornerSize = juce::jmin (button.getHeight (), button.getWidth ()) / 2;

		const int leftIndent = cornerSize / (button.isConnectedOnLeft () ?
                  yIndent * 2 : yIndent);
		const int rightIndent = cornerSize / (button.isConnectedOnRight () ? 
                  yIndent * 2 : yIndent);
		const int textWidth = button.getWidth () - leftIndent - rightIndent;

		if (textWidth > 0)
			g.drawFittedText (button.getButtonText (),
				leftIndent, yIndent, textWidth, button.getHeight () - yIndent * 2,
				juce::Justification::centred, 2, 0.5f);
	}	
	juce::Font getComboBoxFont (juce::ComboBox &c)
	{
		juce::ignoreUnused (c);
		//return Font(c.getHeight () * 0.6f);
		return juce::Font(juce::FontOptions().withHeight(m_fontSize));

	}	
	juce::Font 	getPopupMenuFont ()
	{
		return juce::Font(juce::FontOptions().withHeight(m_fontSize));
	}
	juce::Font getLabelFont()
	{
		return juce::Font(juce::FontOptions().withHeight(m_fontSize));
	}
	juce::Font getLabelFont(juce::Label &l)
	{
		l.setFont(juce::Font(juce::FontOptions().withHeight(m_fontSize)));
		return juce::Font(juce::FontOptions().withHeight(m_fontSize));
	}


};

