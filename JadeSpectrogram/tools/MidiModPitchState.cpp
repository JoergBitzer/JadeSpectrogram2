#include "MidiModPitchState.h"


MidiModPitchBendState::MidiModPitchBendState()
:m_PitchBendState(8192), m_ModState(0)
{

}

void MidiModPitchBendState::processNextMidiBuffer (MidiBuffer& buffer, bool allowNewEvents)
{
    for (const auto metadata : buffer)
    {    
        MidiMessage msg = metadata.getMessage();
        if (msg.isPitchWheel())
        {
            auto pitchbendval = msg.getPitchWheelValue();
            m_PitchBendState = pitchbendval;
        }
        if (msg.isControllerOfType(1)) // 1 = ModWheel 
        {
            auto modval = msg.getControllerValue();
            m_ModState = modval;
        }
    }

    // insert Midi 
    if (allowNewEvents)
    {
        if (m_ModChanged == true)
        {
            m_ModChanged = false;
            auto message = juce::MidiMessage::controllerEvent (1, 1, m_ModState);
            buffer.addEvent(message,0);
        }
        if (m_PitchBendChanged == true)
        {
            m_PitchBendChanged = false;
            //MidiMessage newmsg;
            auto message = juce::MidiMessage::pitchWheel(1, m_PitchBendState);
            //newmsg.pitchWheel(1,m_PitchBendState);
            buffer.addEvent(message,0);
        }

    }

}


MidiModPitchBendStateComponent::MidiModPitchBendStateComponent(MidiModPitchBendState &state)
:m_state(state), m_scaleFactor(1.f)
{
   	m_ModLabel.setText("M", NotificationType::dontSendNotification);
	m_ModLabel.setJustificationType(Justification::centred);
	m_ModLabel.attachToComponent (&m_ModSlider, false);
	addAndMakeVisible(m_ModLabel);

    m_ModSlider.setRange (0, 127);          // [1]
    //m_ModSlider.setTextValueSuffix (" dB");     // [2]
    m_ModSlider.setSliderStyle(Slider::SliderStyle::LinearBarVertical);
    m_ModSlider.setTextBoxStyle(Slider::TextEntryBoxPosition::NoTextBox, true, 60, 20);
    m_ModSlider.setValue(0.0);
    m_ModSlider.onValueChange = [this]{m_state.setModState(static_cast<int>(m_ModSlider.getValue()));};
    addAndMakeVisible (m_ModSlider);

    //m_PitchBendLabel.setFont(Font(3,0));
   	m_PitchBendLabel.setText("P", NotificationType::dontSendNotification);
	m_PitchBendLabel.setJustificationType(Justification::centred);
	m_PitchBendLabel.attachToComponent (&m_PitchBendSlider, false);
	addAndMakeVisible(m_PitchBendLabel);

    m_PitchBendSlider.setRange (0, 16383);          // [1]
    //m_PitchBendSlider.setTextValueSuffix (" dB");     // [2]
    m_PitchBendSlider.setSliderStyle(Slider::SliderStyle::LinearBarVertical);
    m_PitchBendSlider.setTextBoxStyle(Slider::TextEntryBoxPosition::NoTextBox, true, 60, 20);
    m_PitchBendSlider.setValue(8192.0);
    m_PitchBendSlider.onValueChange = [this]{m_state.setPitchBendState(static_cast<int>(m_PitchBendSlider.getValue()));};
    m_PitchBendSlider.onDragEnd = [this]{m_state.setPitchBendState(8192);};
    addAndMakeVisible (m_PitchBendSlider);
    startTimer (50);    

}
void MidiModPitchBendStateComponent::paint(Graphics& g)
{
	g.fillAll((getLookAndFeel().findColour(ResizableWindow::backgroundColourId)).darker(0.2f));
    
}
void MidiModPitchBendStateComponent::resized()
{
	//float scaleFactor = m_ScaleFactor;
	auto r = getLocalBounds();
	//r.reduce(scaleFactor*ENV_MIN_DISTANCE,scaleFactor*ENV_MIN_DISTANCE);
	auto s = r;
	auto t = r;
    
    m_PitchBendSlider.setBounds(10,20,r.getWidth()/2-20,r.getHeight()-30);
    m_ModSlider.setBounds(10+getWidth()/2,20,r.getWidth()/2-20,r.getHeight()-30);

}
