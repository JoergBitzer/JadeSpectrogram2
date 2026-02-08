#include "JadeLookAndFeel.h"

JadeLookAndFeel::JadeLookAndFeel()
{
	setColour(juce::ResizableWindow::backgroundColourId, JadeWhite);

	setColour(juce::Slider::ColourIds::backgroundColourId, JadeGray);
	setColour(juce::Slider::ColourIds::thumbColourId, JadeRed);
	setColour(juce::Slider::ColourIds::trackColourId, JadeGray);
	setColour(juce::Slider::ColourIds::textBoxTextColourId, JadeGray);

	setColour(juce::Label::ColourIds::textColourId, JadeGray);

	setColour(juce::TextButton::ColourIds::buttonColourId, JadeGray);
	setColour(juce::TextButton::ColourIds::buttonOnColourId, juce::Colours::darkgrey);
	setColour(juce::TextButton::ColourIds::textColourOnId, JadeLightRed1);
	setColour(juce::TextButton::ColourIds::textColourOffId, JadeWhite);
	m_fontSize = 12;

	setDefaultLookAndFeel(this);
}

void JadeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider)
{
	juce::ignoreUnused (slider);
	auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
	auto centreX = x + width * 0.5f;
	auto centreY = y + height * 0.5f;
	auto rx = centreX - radius;
	auto ry = centreY - radius;
	auto rw = 2.0f * radius;
	auto angle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * sliderPos;

	// fill
	g.setColour(JadeGray);
	g.fillEllipse(rx, ry, rw, rw);

	// outline
	juce::Colour c1 = juce::Colour::fromFloatRGBA(1.3*JadeRed.getFloatRed()*sliderPos,3.0*JadeRed.getFloatGreen()*sliderPos,3.0*JadeRed.getFloatBlue()*sliderPos,1.0);
	g.setColour(c1);
	//g.drawEllipse(rx, ry, rw, rw, 5.0);
	g.drawEllipse(rx, ry, rw, rw, juce::jmax(int(width*0.07),5));

	// Point
	int PointSize = juce::jmax(width/6,10);
	juce::Path p;
	p.addEllipse(-PointSize / 2, -0.95 * radius, PointSize, PointSize);
	p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

	g.setColour(JadeRed);
	g.fillPath(p);

}
