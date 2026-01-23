#pragma once

#include <JuceHeader.h>
class MidiModPitchBendState
{
public:
    MidiModPitchBendState();

    void processNextMidiBuffer (MidiBuffer& buffer,
                                bool allowNewEvents);
    void setPitchBendState(int pitchBend){m_PitchBendState = pitchBend; m_PitchBendChanged = true;};
    void setModState(int mod){m_ModState = mod;m_ModChanged = true;};
    int getPitchBendState(){return m_PitchBendState; };
    int getModState(){return m_ModState;};    
private:
    int m_PitchBendState;
    int m_ModState;
    bool m_ModChanged = false;
    bool m_PitchBendChanged = false;

};

class MidiModPitchBendStateComponent : public Component, public Timer
{
public:
    MidiModPitchBendStateComponent(MidiModPitchBendState &state);
	void paint(Graphics& g) override;
	void resized() override;
    void setScaleFactor(float newscale){m_scaleFactor = newscale;};
    void timerCallback() override
    {
        auto modval = m_state.getModState();
        auto pbval = m_state.getPitchBendState();
        m_ModSlider.setValue(modval,juce::NotificationType::dontSendNotification);
        m_PitchBendSlider.setValue(pbval,juce::NotificationType::dontSendNotification);
        //DBG(pbval);
        repaint();
    };

private:
    MidiModPitchBendState& m_state;
    float m_scaleFactor;
	Label m_ModLabel;
	Slider m_ModSlider;
	Label m_PitchBendLabel;
	Slider m_PitchBendSlider;

};
