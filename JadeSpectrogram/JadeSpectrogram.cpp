#include <math.h>
#include <cassert>
#include "JadeSpectrogram.h"

#include "PluginProcessor.h"
const float g_minValForLogSpectrogram = 1e-10f;

JadeSpectrogramAudio::JadeSpectrogramAudio(JadeSpectrogramAudioProcessor* processor)
:SynchronBlockProcessor(), m_processor(processor),m_fftsize(2048)
{
    m_mixMode = JadeSpectrogramAudio::ChannelMixMode::AbsMean;
    m_windowChoice = SpectrumAnalyzer::WindowType::Hann;

}

void JadeSpectrogramAudio::prepareToPlay(double sampleRate, int max_samplesPerBlock, int max_channels)
{
    juce::ignoreUnused(max_samplesPerBlock,max_channels);
    m_channels = static_cast<size_t>(max_channels);
    m_fftsize = 2048; 
    //synchronblocksize = m_fftsize/2; // for 50% overlap
    size_t synchronblocksize;
    // here your code
    m_fs = static_cast<float> (sampleRate);
    m_leftAnalyzer.setBlockSize(m_fftsize);
    m_rightAnalyzer.setBlockSize(m_fftsize);
    m_leftAnalyzer.setSampleRate(sampleRate);
    m_rightAnalyzer.setSampleRate(sampleRate);
    m_leftAnalyzer.setFFTSize(m_fftsize);
    m_rightAnalyzer.setFFTSize(m_fftsize);
    m_leftAnalyzer.setOverlap(SpectrumAnalyzer::OverlapPercentage::perc50);
    m_rightAnalyzer.setOverlap(SpectrumAnalyzer::OverlapPercentage::perc50);
    m_leftAnalyzer.setWindowType(m_windowChoice);
    m_rightAnalyzer.setWindowType(m_windowChoice);
    // synchronblocksize should be the same as the hop size of the analyzers, which is determined by the block size and the overlap percentage
    synchronblocksize = m_leftAnalyzer.getHopSize();


    //synchronblocksize = static_cast<int>(round(g_desired_blocksize_ms * sampleRate * 0.001)); // 0.001 to transform ms to seconds;
    //if (g_forcePowerOf2)
    //{
     //   int nextpowerof2 = int(log2(synchronblocksize))+1;
     //   synchronblocksize = int(pow(2,nextpowerof2));
    //}
    prepareSynchronProcessing(max_channels,static_cast<int>(synchronblocksize));
    m_Latency += static_cast<int>(synchronblocksize);

    // reserve memory for the analyzers and the FIFO
    m_timeInLeft.resize(synchronblocksize);
    m_timeInRight.resize(synchronblocksize);
    m_freqsize = static_cast<size_t>(m_fftsize/2)+1;
    m_perLeft.resize(m_freqsize);
    m_perRight.resize(m_freqsize);
    m_power.resize(m_freqsize);
    m_fifo.setMaxCapacity(1000, m_freqsize); // 1000 time slices should be enough
    m_fifo.setActSize(999, m_freqsize); // we push one time slice after the other
    m_fifo.reset();
    m_fifo.fill(10.f*log10f(g_minValForLogSpectrogram)); // fill with very low values
}

int JadeSpectrogramAudio::processSynchronBlock(juce::AudioBuffer<float> & buffer, juce::MidiBuffer &midiMessages, int NrOfBlocksSinceLastProcessBlock)
{
    juce::ignoreUnused(midiMessages, NrOfBlocksSinceLastProcessBlock);

    size_t numSamples = static_cast<size_t>(buffer.getNumSamples());
    size_t numChannels = static_cast<size_t>(buffer.getNumChannels());

    assert(numChannels == m_channels && "number of channels should be the same as set in prepareToPlay");
    assert(numSamples == m_timeInLeft.size() && numSamples == m_timeInRight.size() && "number of samples should be the same as set in prepareToPlay");
    auto data = buffer.getArrayOfReadPointers();

    // copy audiobuffer into m_timeInLeft and m_timeInRight (or directly into the internal mem of the analyzers)
    for (size_t kk = 0; kk < numSamples ; ++kk) // copy in block to internal mem
    {
        for (size_t cc = 0 ; cc < numChannels ; ++cc)
        {
            if (cc == 0)
                m_timeInLeft[kk] = data[cc][kk];
            else if (cc == 1)
                if (m_mixMode == JadeSpectrogramAudio::ChannelMixMode::TimeMean)
                {   // compute the mean and use for both channels
                    m_timeInLeft[kk] += data[cc][kk];
                    m_timeInLeft[kk] /= 2.f;
                    m_timeInRight[kk] = m_timeInLeft[kk];
                }
                else
                {
                    m_timeInRight[kk] = data[cc][kk];
                }
        }
    }
    // Compute Periodograms for each channel
    m_leftAnalyzer.getPeriodogram(m_timeInLeft, m_perLeft);
    if (numChannels>1)
    {
        m_rightAnalyzer.getPeriodogram(m_timeInRight, m_perRight);
        switch (m_mixMode)
        {
            case JadeSpectrogramAudio::ChannelMixMode::AbsMean:
                for (size_t kk = 0; kk < m_freqsize ; ++kk)
                {
                    m_power[kk] = 0.5f*(m_perLeft[kk] + m_perRight[kk]);
                }
                break;
            case JadeSpectrogramAudio::ChannelMixMode::Max:
                for (size_t kk = 0; kk < m_freqsize ; ++kk)
                {
                    m_power[kk] = std::max(m_perLeft[kk], m_perRight[kk]);
                }
                break;
            case JadeSpectrogramAudio::ChannelMixMode::Min:
                for (size_t kk = 0; kk < m_freqsize ; ++kk)
                {
                    m_power[kk] = std::min(m_perLeft[kk], m_perRight[kk]);
                }
                break;
            case JadeSpectrogramAudio::ChannelMixMode::Left:
                m_power = m_perLeft;
                break;
            case JadeSpectrogramAudio::ChannelMixMode::Right:
                m_power = m_perRight;
                break;
        }
    }
    else
    {
        m_power = m_perLeft;
    }
    // convert to dB
    for (size_t kk = 0; kk < m_freqsize ; ++kk)
    {
        m_power[kk] = 10.f*log10f(m_power[kk] + g_minValForLogSpectrogram);
    }
    // save into mem
    m_fifo.push(m_power);




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
    ));
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
    float firstguessFFTSize = (fftsize_ms*0.001f*m_fs);
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
