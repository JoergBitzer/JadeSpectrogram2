#include <math.h>
#include "JadeSpectrogram.h"

#include "PluginProcessor.h"

JadeSpectrogramAudio::JadeSpectrogramAudio(JadeSpectrogramAudioProcessor* processor)
:SynchronBlockProcessor(), m_processor(processor),m_fftsize(1024)
{
    m_mode = JadeSpectrogramAudio::ChannelMixMode::AbsMean;
    m_windowChoice = SpectrumAnalyzer::WindowType::Hann;

}

void JadeSpectrogramAudio::prepareToPlay(double sampleRate, int max_samplesPerBlock, int max_channels)
{
    juce::ignoreUnused(max_samplesPerBlock,max_channels);
    int synchronblocksize;
    synchronblocksize = static_cast<int>(round(g_desired_blocksize_ms * sampleRate * 0.001)); // 0.001 to transform ms to seconds;
    if (g_forcePowerOf2)
    {
        int nextpowerof2 = int(log2(synchronblocksize))+1;
        synchronblocksize = int(pow(2,nextpowerof2));
    }
    prepareSynchronProcessing(max_channels,synchronblocksize);
    m_Latency += synchronblocksize;
    // here your code
    m_fs = static_cast<float> (sampleRate);


}

int JadeSpectrogramAudio::processSynchronBlock(juce::AudioBuffer<float> & buffer, juce::MidiBuffer &midiMessages, int NrOfBlocksSinceLastProcessBlock)
{
    juce::ignoreUnused(buffer, midiMessages, NrOfBlocksSinceLastProcessBlock);
    return 0;
}

void JadeSpectrogramAudio::addParameter(std::vector<std::unique_ptr<juce::RangedAudioParameter>> &paramVector)
{
    // this is just a placeholder (necessary for compiling/testing the template)
        paramVector.push_back(std::make_unique<AudioParameterFloat>(paramDisplayMinFreq.ID,
		paramDisplayMinFreq.name,
		NormalisableRange<float>(paramDisplayMinFreq.minValue, paramDisplayMinFreq.maxValue),
		paramDisplayMinFreq.defaultValue,
        AudioParameterFloatAttributes().withLabel (paramDisplayMinFreq.unitName)
                                        .withCategory (juce::AudioProcessorParameter::genericParameter)
                                        // or two additional lines with lambdas to convert data for display
                                        .withStringFromValueFunction (std::move ([](float value, int MaxLen) { return (String(0.1*int(exp(value)*10 + 0.5), MaxLen)); }))
                                        .withValueFromStringFunction (std::move ([](const String& text) {return text.getFloatValue(); }))
        ));

       	paramVector.push_back(std::make_unique<AudioParameterFloat>(paramDisplayMaxFreq.ID,
		paramDisplayMaxFreq.name,
		NormalisableRange<float>(paramDisplayMaxFreq.minValue, paramDisplayMaxFreq.maxValue),
		paramDisplayMaxFreq.defaultValue,
        AudioParameterFloatAttributes().withLabel (paramDisplayMaxFreq.unitName)
                                        .withCategory (juce::AudioProcessorParameter::genericParameter)
                                        // or two additional lines with lambdas to convert data for display
                                        .withStringFromValueFunction (std::move ([](float value, int MaxLen) { return (String(0.1*int(exp(value)*10 + 0.5), MaxLen)); }))
                                        .withValueFromStringFunction (std::move ([](const String& text) {return text.getFloatValue(); }))
        ));

       	paramVector.push_back(std::make_unique<AudioParameterFloat>(paramDisplayMinColor.ID,
		paramDisplayMinColor.name,
		NormalisableRange<float>(paramDisplayMinColor.minValue, paramDisplayMinColor.maxValue),
		paramDisplayMinColor.defaultValue,
        AudioParameterFloatAttributes().withLabel (paramDisplayMinColor.unitName)
                                        .withCategory (juce::AudioProcessorParameter::genericParameter)
                                        // or two additional lines with lambdas to convert data for display
                                        .withStringFromValueFunction (std::move ([](float value, int MaxLen) { return (String(1.0*int((value) + 0.5), MaxLen)); }))
                                        .withValueFromStringFunction (std::move ([](const String& text) {return text.getFloatValue(); }))
        ));
       	paramVector.push_back(std::make_unique<AudioParameterFloat>(paramDisplayMaxColor.ID,
		paramDisplayMaxColor.name,
		NormalisableRange<float>(paramDisplayMaxColor.minValue, paramDisplayMaxColor.maxValue),
		paramDisplayMaxColor.defaultValue,
        AudioParameterFloatAttributes().withLabel (paramDisplayMaxColor.unitName)
                                        .withCategory (juce::AudioProcessorParameter::genericParameter)
                                        // or two additional lines with lambdas to convert data for display
                                        .withStringFromValueFunction (std::move ([](float value, int MaxLen) { return (String(1.0*int((value) + 0.5), MaxLen)); }))
                                        .withValueFromStringFunction (std::move ([](const String& text) {return text.getFloatValue(); }))
        ))
}

void JadeSpectrogramAudio::prepareParameter(std::unique_ptr<juce::AudioProcessorValueTreeState> &vts)
{
    m_DisplayMinFreq.prepareParameter(vts->getRawParameterValue(paramDisplayMinFreq.ID));
    m_DisplayMaxFreq.prepareParameter(vts->getRawParameterValue(paramDisplayMaxFreq.ID));
    m_DisplayMinColor.prepareParameter(vts->getRawParameterValue(paramDisplayMinColor.ID));
    m_DisplayMaxColor.prepareParameter(vts->getRawParameterValue(paramDisplayMaxColor.ID));
}

void JadeSpectrogramAudio::setFFTSize(size_t newFFTSize)
{
}

void JadeSpectrogramAudio::setclosestFFTSize_ms(float fftsize_ms)
{
    m_fftsize = getnextpowerof2(fftsize_ms);    
}

void JadeSpectrogramAudio::setmemoryTime_s(float memsize_s)
{
}

size_t JadeSpectrogramAudio::getnextpowerof2(float fftsize_ms)
{
    float firstguessFFTSize = (fftsize_ms*0.001*m_fs);
    int nextpowerof2 = int(log(firstguessFFTSize)/log(2.f))+1;
    return size_t(pow(2.f,nextpowerof2));
}

JadeSpectrogramGUI::JadeSpectrogramGUI(JadeSpectrogramAudioProcessor& p, juce::AudioProcessorValueTreeState& apvts)
:m_processor(p) ,m_apvts(apvts)
{
    
}

void JadeSpectrogramGUI::paint(juce::Graphics &g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter(0.3f));

    g.setColour (juce::Colours::white);
    g.setFont (12.0f);
    
    juce::String text2display = "JadeSpectrogram V " + juce::String(PLUGIN_VERSION_MAJOR) + "." + juce::String(PLUGIN_VERSION_MINOR) + "." + juce::String(PLUGIN_VERSION_PATCH);
    g.drawFittedText (text2display, getLocalBounds(), juce::Justification::bottomLeft, 1);

}

void JadeSpectrogramGUI::resized()
{
	auto r = getLocalBounds();
    
    // if you have to place several components, use scaleFactor
    //int width = r.getWidth();
	//float scaleFactor = float(width)/g_minGuiSize_x;

    // use the given canvas in r
    juce::ignoreUnused(r);
}
