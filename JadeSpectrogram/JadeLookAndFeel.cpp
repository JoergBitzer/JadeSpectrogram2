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
	// switched on: dark grey background with a white symbol (no pink)
	setColour(juce::TextButton::ColourIds::buttonOnColourId, JadeDarkGray);
	setColour(juce::TextButton::ColourIds::textColourOnId, JadeWhite);
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
	juce::Colour c1 = juce::Colour::fromFloatRGBA(1.3f*JadeRed.getFloatRed()*sliderPos,3.f*JadeRed.getFloatGreen()*sliderPos,3.f*JadeRed.getFloatBlue()*sliderPos,1.f);
	g.setColour(c1);
	//g.drawEllipse(rx, ry, rw, rw, 5.0);
	g.drawEllipse(rx, ry, rw, rw, juce::jmax((width*0.07f),5.f));

	// Point
	float PointSize = juce::jmax(static_cast<float>(width)/6.f,10.f);
	juce::Path p;
	p.addEllipse(-PointSize / 2.f, -0.95f * radius, PointSize, PointSize);
	p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

	g.setColour(JadeRed);
	g.fillPath(p);

}

static bool isTwoValue(juce::Slider::SliderStyle style)
{
	return style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical;
}

int JadeLookAndFeel::getSliderThumbRadius(juce::Slider& slider)
{
	if (!isTwoValue(slider.getSliderStyle()))
		return LookAndFeel_V4::getSliderThumbRadius(slider);
	// the triangles lie outside the range: reserve their length at both ends of the slider
	const float across = slider.isVertical() ? static_cast<float>(slider.getWidth()) : static_cast<float>(slider.getHeight());
	return juce::jmax(4, static_cast<int>(0.62f*0.9f*across));
}

void JadeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
	float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
	if (!isTwoValue(style))
	{
		LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
		return;
	}
	const bool vertical = slider.isVertical();
	const float across = vertical ? static_cast<float>(width) : static_cast<float>(height);
	const float centre = vertical ? static_cast<float>(x) + 0.5f*static_cast<float>(width) : static_cast<float>(y) + 0.5f*static_cast<float>(height);
	const float start = vertical ? static_cast<float>(y) : static_cast<float>(x);
	const float end = vertical ? static_cast<float>(y + height) : static_cast<float>(x + width);
	auto line = [&](float a, float b, float thickness)
	{
		if (vertical) g.drawLine(centre, a, centre, b, thickness);
		else          g.drawLine(a, centre, b, centre, thickness);
	};
	// whole range thin, the selected range thicker and dark
	g.setColour(slider.findColour(juce::Slider::backgroundColourId));
	line(start, end, juce::jmax(2.f, 0.2f*across));
	g.setColour(slider.findColour(juce::Slider::trackColourId));
	line(minSliderPos, maxSliderPos, juce::jmax(3.f, 0.4f*across));
	// thumbs: triangles with the apex at the value, pointing into the range
	const float w = 0.9f*across;       // across the slider
	const float l = 0.62f*w;           // along the slider
	g.setColour(slider.findColour(juce::Slider::thumbColourId));
	auto triangle = [&](float pos, float direction) // direction: +1 thumb extends to larger coordinates
	{
		juce::Path p;
		if (vertical) p.addTriangle(centre, pos, centre - 0.5f*w, pos + direction*l, centre + 0.5f*w, pos + direction*l);
		else          p.addTriangle(pos, centre, pos + direction*l, centre - 0.5f*w, pos + direction*l, centre + 0.5f*w);
		g.fillPath(p);
	};
	// vertical: the minimum is at the bottom (larger y), its thumb below; horizontal: the minimum is left
	triangle(minSliderPos, vertical ? +1.f : -1.f);
	triangle(maxSliderPos, vertical ? -1.f : +1.f);
}
