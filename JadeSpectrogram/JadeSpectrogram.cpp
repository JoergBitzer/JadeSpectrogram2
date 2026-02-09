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
    m_PauseMode = false;

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
    m_leftAnalyzer.setSampleRate(sampleRate);
    m_rightAnalyzer.setSampleRate(sampleRate);
    setFFTSize(m_fftsize);
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
        m_power[kk] = 10.f*log10f(2.f*m_power[kk]/m_fs + g_minValForLogSpectrogram);
    }
    // save into mem
    if (!m_PauseMode)
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
    juce::ScopedLock;
    m_fftsize = newFFTSize;
    size_t synchronblocksize;
    m_leftAnalyzer.setBlockSize(m_fftsize);
    m_rightAnalyzer.setBlockSize(m_fftsize);
    m_leftAnalyzer.setFFTSize(m_fftsize);
    m_rightAnalyzer.setFFTSize(m_fftsize);
    m_leftAnalyzer.setOverlap(SpectrumAnalyzer::OverlapPercentage::perc50);
    m_rightAnalyzer.setOverlap(SpectrumAnalyzer::OverlapPercentage::perc50);
    m_leftAnalyzer.setWindowType(m_windowChoice);
    m_rightAnalyzer.setWindowType(m_windowChoice);
    // synchronblocksize should be the same as the hop size of the analyzers, which is determined by the block size and the overlap percentage
    synchronblocksize = m_leftAnalyzer.getHopSize();


    prepareSynchronProcessing(m_channels,static_cast<int>(synchronblocksize));
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

float g_pastTimeMemLen_s = 8.0;
JadeSpectrogramGUI::JadeSpectrogramGUI(JadeSpectrogramAudioProcessor& p, juce::AudioProcessorValueTreeState& apvts)
:m_processor(p) ,m_apvts(apvts),
m_internalImg(Image::RGB,1,1,true),
m_internalHeight(1), m_recomputeAll(true),m_maxColorVal(g_maxColorVal),m_minColorVal(g_minColorVal),
m_colorpalette(256,CColorPalette::PaletteName::kHot),m_maxDisplayFreq(20000.f),m_minDisplayFreq(0.f),
//somethingChanged(nullptr),
m_isPaused(false),m_isRunningDisplay(false),m_hideFFTSizeCombobox(false)
//,m_editor(editor)
{

    srand (time(NULL));
    m_internalHeight = m_processor.m_algo.getSpectrumSize();
    float fs = m_processor.m_algo.getSamplerate();
    m_internalWidth = static_cast<size_t>(g_pastTimeMemLen_s * fs/(m_internalHeight-1)); ///(m_internalHeigt-1) is hopsize

    m_exchangeSpectrum.resize(m_internalHeight);

    m_displaymem.resize(m_internalWidth);
    for (auto &vec : m_displaymem)
    {
        vec.resize(m_internalHeight);
        std::fill(vec.begin(), vec.end(), 10.f*log10f(g_minValForLogSpectrogram));
    }
    m_internalImg = m_internalImg.rescaled(m_internalWidth,m_internalHeight);

    // GUI Elements
    m_colorpalette.setValueRange(m_minColorVal,m_maxColorVal);

// UI Elements
	m_DisplayMinFreqLabel.setText("Freq.", NotificationType::dontSendNotification);
	m_DisplayMinFreqLabel.setJustificationType(Justification::centred);
	//m_DisplayMinFreqLabel.attachToComponent (&m_DisplayMinFreqSlider, false);
	//addAndMakeVisible(m_DisplayMinFreqLabel);
	m_DisplayMinFreqSlider.setSliderStyle(Slider::SliderStyle::LinearVertical);
	m_DisplayMinFreqAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(m_apvts, paramDisplayMinFreq.ID, m_DisplayMinFreqSlider);
	addAndMakeVisible(m_DisplayMinFreqSlider);
	m_DisplayMinFreqSlider.onValueChange = [this]() {if (somethingChanged != nullptr) somethingChanged(); };

	m_DisplayMaxFreqLabel.setText("Freq.", NotificationType::dontSendNotification);
	m_DisplayMaxFreqLabel.setJustificationType(Justification::centred);
	//m_DisplayMaxFreqLabel.attachToComponent (&m_DisplayMaxFreqSlider, false);
	//addAndMakeVisible(m_DisplayMaxFreqLabel);
	m_DisplayMaxFreqSlider.setSliderStyle(Slider::SliderStyle::LinearVertical);
	m_DisplayMaxFreqAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(m_apvts, paramDisplayMaxFreq.ID, m_DisplayMaxFreqSlider);
	addAndMakeVisible(m_DisplayMaxFreqSlider);
	m_DisplayMaxFreqSlider.onValueChange = [this]() {if (somethingChanged != nullptr) somethingChanged(); };

	m_DisplayMinColorLabel.setText("Color.", NotificationType::dontSendNotification);
	m_DisplayMinColorLabel.setJustificationType(Justification::centred);
	//m_DisplayMinColorLabel.attachToComponent (&m_DisplayMinColorSlider, false);
	//addAndMakeVisible(m_DisplayMinColorLabel);
	m_DisplayMinColorSlider.setSliderStyle(Slider::SliderStyle::LinearVertical);
	m_DisplayMinColorAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(m_apvts, paramDisplayMinColor.ID, m_DisplayMinColorSlider);
	addAndMakeVisible(m_DisplayMinColorSlider);
	m_DisplayMinColorSlider.onValueChange = [this]() {m_recomputeAll = true; if (somethingChanged != nullptr) somethingChanged(); };

	m_DisplayMaxColorLabel.setText("Color.", NotificationType::dontSendNotification);
	m_DisplayMaxColorLabel.setJustificationType(Justification::centred);
	//m_DisplayMaxColorLabel.attachToComponent (&m_DisplayMaxColorSlider, false);
	//addAndMakeVisible(m_DisplayMaxColorLabel);
	m_DisplayMaxColorSlider.setSliderStyle(Slider::SliderStyle::LinearVertical);
	m_DisplayMaxColorAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(m_apvts, paramDisplayMaxColor.ID, m_DisplayMaxColorSlider);
	addAndMakeVisible(m_DisplayMaxColorSlider);
	m_DisplayMaxColorSlider.onValueChange = [this]() { m_recomputeAll = true; if (somethingChanged != nullptr) somethingChanged(); };

    m_pauseButton.setButtonText("Pause");
    m_pauseButton.setToggleState(false,NotificationType::dontSendNotification);
    m_pauseButton.onClick = [this](){pauseClicked();};
    addAndMakeVisible(m_pauseButton);

    m_runModeButton.setButtonText("Fix");
    m_runModeButton.setToggleState(false,NotificationType::dontSendNotification);
    m_runModeButton.onClick = [this](){runClicked();};
    addAndMakeVisible(m_runModeButton);

    m_colorScheme.addItem("Mono",1);
    m_colorScheme.addItem("BW",2);
    m_colorScheme.addItem("Hot",3);
    m_colorScheme.addItem("Rainbow",4);
    m_colorScheme.addItem("Viridis",5);
    m_colorScheme.addItem("Plasma",6);
    m_colorScheme.addItem("Jade",7);
    m_colorScheme.setSelectedItemIndex(6,NotificationType::dontSendNotification);
    m_colorScheme.setColour(juce::ComboBox::ColourIds::backgroundColourId,JadeTeal);
    m_colorScheme.onChange = [this](){m_recomputeAll = true; m_colorpalette.setColorScheme(static_cast<CColorPalette::PaletteName>(m_colorScheme.getSelectedItemIndex()));};
    addAndMakeVisible(m_colorScheme);
    m_windowFktCombo.addItem("Rectangular",1);
    m_windowFktCombo.addItem("Hann",2);
    m_windowFktCombo.addItem("Hamming",3);
    m_windowFktCombo.addItem("BlackmanHarris",4);
    m_windowFktCombo.addItem("FlatTop",5);
    m_windowFktCombo.addItem("HannPoisson",6);
    m_windowFktCombo.setColour(juce::ComboBox::ColourIds::backgroundColourId,JadeTeal);
    m_windowFktCombo.onChange = [this](){m_processor.m_algo.setWindowType(static_cast<SpectrumAnalyzer::WindowType> (m_windowFktCombo.getSelectedItemIndex()));};
    m_windowFktCombo.setSelectedItemIndex(1,NotificationType::dontSendNotification);
    addAndMakeVisible(m_windowFktCombo);

    m_fftSizeCombo.addItem("512",1);
    m_fftSizeCombo.addItem("1024",2);
    m_fftSizeCombo.addItem("2048",3);
    m_fftSizeCombo.addItem("4096",4);
    m_fftSizeCombo.addItem("8192",5);
    m_fftSizeCombo.setColour(juce::ComboBox::ColourIds::backgroundColourId,JadeTeal);
    m_fftSizeCombo.onChange = [this](){changeFFTSize();};

    m_fftSizeCombo.setSelectedItemIndex(2,NotificationType::dontSendNotification);
    addAndMakeVisible(m_fftSizeCombo);
    m_FreqLabel.setText("Analysis",juce::NotificationType::dontSendNotification);
    m_FreqLabel.setJustificationType(juce::Justification::centred);
    m_FreqLabel.setColour(Label::ColourIds::outlineColourId,JadeTeal);
    m_FreqLabel.setColour(Label::ColourIds::textColourId,juce::Colours::white);
    m_FreqLabel.setColour(Label::ColourIds::backgroundColourId,JadeTeal);
    addAndMakeVisible(m_FreqLabel);

    startTimer(40) ;
}

void JadeSpectrogramGUI::paint(juce::Graphics &g)
{
    CriticalSection crit;
    crit.enter();

    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker(0.2));

    int w = getWidth();
    int h = getHeight();

    float fs = m_processor.m_algo.getSamplerate();
    
    m_minDisplayFreq = exp(m_DisplayMinFreqSlider.getValue());
    m_maxDisplayFreq = exp(m_DisplayMaxFreqSlider.getValue());

    if (m_minDisplayFreq >= fs*0.5)
        m_minDisplayFreq = 0.9*fs*0.5;
    if (m_maxDisplayFreq >= fs*0.5)
        m_maxDisplayFreq = fs*0.5;

    if (m_minDisplayFreq >= m_maxDisplayFreq)
    {
        m_minDisplayFreq = 0.9*m_maxDisplayFreq;
        m_DisplayMinFreqSlider.setValue(log(m_minDisplayFreq));
    }

    int displayStartPixel = int(2.0*m_minDisplayFreq/fs *m_internalHeight+0.5);
    int displayEndPixel = int(2.0*m_maxDisplayFreq/fs *m_internalHeight + 0.5);
    int heightInterval = int(2.0*m_maxDisplayFreq/fs *m_internalHeight -2.0*m_minDisplayFreq/fs *m_internalHeight+0.5);

    int hStart = m_internalHeight - displayEndPixel;

    int wStartPic = m_scaleFactor*(g_SliderWidth + g_FreqMeter);

    g.drawImage(m_internalImg,wStartPic,0,0.8f*w,int(float(h)-m_scaleFactor*g_menuHeight+0.5),
                0,hStart,m_internalWidth,heightInterval);
    // Add frequency scale
    int nrOfYTicks = 11;
    float RangePerTick = float(m_maxDisplayFreq - m_minDisplayFreq)/(nrOfYTicks-1);
    int TextHeight = 20;
    g.setFont(0.8*m_scaleFactor*TextHeight);
    for (auto kk = 0; kk < nrOfYTicks; ++kk)
    {
        float newExaktFreq = int(m_minDisplayFreq + RangePerTick*kk + 0.5);
        String OutText;
        if (newExaktFreq >= 1000.f)
        {
            newExaktFreq = int(newExaktFreq*0.01 +0.5)*100;
            OutText += String(newExaktFreq/1000);
            OutText += "k";
        }
        else
        {
            if (newExaktFreq >= 150.f)
            {
                newExaktFreq = int(newExaktFreq*0.1 +0.5)*10;
                OutText += String(newExaktFreq);
            }
            else
            {
                 OutText += String(newExaktFreq);
            }
        }

        int ydelta = (h-m_scaleFactor*g_menuHeight)* (newExaktFreq-m_minDisplayFreq)/(m_maxDisplayFreq - m_minDisplayFreq);
        int x = wStartPic-g_FreqMeter*m_scaleFactor;
        int y ;
        if (kk < nrOfYTicks-1)
            y = h-m_scaleFactor*g_menuHeight-0.5*TextHeight*m_scaleFactor - ydelta;
        else
        {
            y = h-m_scaleFactor*g_menuHeight - ydelta;
        }
        g.drawText(OutText,x,y,g_FreqMeter*m_scaleFactor,m_scaleFactor*TextHeight,
                juce::Justification::centred,true);
    }

    // Plot Colorbar
    int cbHeight = h-m_scaleFactor*g_menuHeight;
    Image colorbar(Image::RGB,1,cbHeight,true);
    for (int kk = 0; kk < cbHeight; kk++)
    {   
        float val = float(kk)/cbHeight*(g_maxColorVal - g_minColorVal) + g_minColorVal;
        int color = m_colorpalette.getRGBColor(val);
        color = color|0xFF000000; // kein alpha blending
        colorbar.setPixelAt(0,cbHeight-1-kk,juce::Colour(color));
    }
    g.drawImage(colorbar,w-m_scaleFactor*(g_colorbar_width+g_FreqMeter + g_SliderWidth),0,m_scaleFactor*g_colorbar_width,h-m_scaleFactor*g_menuHeight,
                0,0,1,cbHeight);

    // draw scale
    // Add colorbar scale
    nrOfYTicks = 11;
    RangePerTick = float(g_maxColorVal - g_minColorVal)/(nrOfYTicks-1);
    for (auto kk = 0; kk < nrOfYTicks; ++kk)
    {
        float newExaktFreq = int((g_minColorVal + RangePerTick*kk)*0.1 )*10;
        String OutText;
        OutText += String(newExaktFreq);

        int ydelta = (h-m_scaleFactor*g_menuHeight)* (newExaktFreq-g_minColorVal)/(g_maxColorVal - g_minColorVal);
        int x = w - (g_FreqMeter + g_SliderWidth + g_SliderMaxFreq_x) *m_scaleFactor;
        int y ;
        if (kk < nrOfYTicks-1)
            y = h-m_scaleFactor*g_menuHeight-0.5*TextHeight*m_scaleFactor - ydelta;
        else
        {
            y = h-m_scaleFactor*g_menuHeight - ydelta;
        }
        
        g.drawText(OutText,x,y,g_FreqMeter*m_scaleFactor,m_scaleFactor*TextHeight,
                juce::Justification::centred,true);
    }
    
    m_newDataAvailable = 0;

    g.setColour (juce::Colours::white);
    g.setFont (12.0f);
    
    juce::String text2display = "JadeSpectrogram V " + juce::String(PLUGIN_VERSION_MAJOR) + "." + juce::String(PLUGIN_VERSION_MINOR) + "." + juce::String(PLUGIN_VERSION_PATCH);
    g.drawFittedText (text2display, getLocalBounds(), juce::Justification::bottomLeft, 1);
    crit.exit();    

}

void JadeSpectrogramGUI::resized()
{
	auto r = getLocalBounds();
    CriticalSection crit;
    crit.enter();
   
    // if you have to place several components, use scaleFactor
    int width = r.getWidth();
    int h = r.getHeight();
	m_scaleFactor = float(width)/g_minGuiSize_x;

    // use the given canvas in r
    m_DisplayMaxFreqSlider.setBounds(m_scaleFactor*g_SliderMaxFreq_x,m_scaleFactor*g_SliderMaxFreq_y,
            m_scaleFactor*g_SliderWidth,m_scaleFactor*g_SliderHeight);
    m_DisplayMinFreqSlider.setBounds(m_scaleFactor*g_SliderMinFreq_x,m_scaleFactor*g_SliderMinFreq_y,
            m_scaleFactor*g_SliderWidth,m_scaleFactor*g_SliderHeight);

    m_DisplayMaxColorSlider.setBounds(m_scaleFactor*g_SliderMaxColor_x,m_scaleFactor*g_SliderMaxColor_y,
            m_scaleFactor*g_SliderWidth,m_scaleFactor*g_SliderHeight);
    m_DisplayMinColorSlider.setBounds(m_scaleFactor*g_SliderMinColor_x,m_scaleFactor*g_SliderMinColor_y,
            m_scaleFactor*g_SliderWidth,m_scaleFactor*g_SliderHeight);

    m_pauseButton.setBounds(m_scaleFactor*g_PauseButton_x,  int(float(h)-m_scaleFactor*g_menuHeight+0.5f),
                    m_scaleFactor*g_ButtonWidth,m_scaleFactor*g_ButtonHeight);

    int w = getWidth();
    int x = m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.8*w - m_scaleFactor*g_ButtonWidth;

    m_runModeButton.setBounds(x, int(float(h)-m_scaleFactor*g_menuHeight+0.5f),
                    m_scaleFactor*g_ButtonWidth,m_scaleFactor*g_ButtonHeight);

    m_colorScheme.setBounds(w-m_scaleFactor*(g_colorbar_width+g_FreqMeter + g_SliderWidth), int(float(h)-m_scaleFactor*g_menuHeight+0.5f),
                    m_scaleFactor*g_colorbar_width, m_scaleFactor*g_ButtonHeight);

    x = m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.4*w - m_scaleFactor*0.5*100;
    m_FreqLabel.setBounds(x , int(float(h)-m_scaleFactor*g_menuHeight+0.5f),m_scaleFactor*100,m_scaleFactor*g_ButtonHeight );                    

    x = m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.2*w - m_scaleFactor*0.5*100;
    m_windowFktCombo.setBounds(x, int(float(h)-m_scaleFactor*g_menuHeight+0.5f),m_scaleFactor*100,m_scaleFactor*g_ButtonHeight);

    x = m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.6*w - m_scaleFactor*0.5*100;
    m_fftSizeCombo.setBounds(x, int(float(h)-m_scaleFactor*g_menuHeight+0.5f),m_scaleFactor*100,m_scaleFactor*g_ButtonHeight);
            
    crit.exit();
}

void JadeSpectrogramGUI::timerCallback()
{
    float maxValColor = m_DisplayMaxColorSlider.getValue();
    float minValColor = m_DisplayMinColorSlider.getValue();

    m_colorpalette.setValueRange(minValColor,maxValColor);
    CriticalSection crit;
    crit.enter();



    bool stilldataavailable;
    do
    {
        size_t actSpectrumSize = m_processor.m_algo.getSpectrumSize();
        if (actSpectrumSize != m_internalHeight)
        {
            m_internalHeight = static_cast<int>(actSpectrumSize);
            float fs = m_processor.m_algo.getSamplerate();
            m_internalWidth = static_cast<size_t>(g_pastTimeMemLen_s * fs/(m_internalHeight-1)); ///(m_internalHeigt-1) is hopsize

            m_recomputeAll = true;
            m_displaymem.resize(m_internalWidth);
            for (auto &vec : m_displaymem)
            {
                vec.resize(m_internalHeight);
                std::fill(vec.begin(), vec.end(), 10.f*log10f(g_minValForLogSpectrogram));
            }
            m_exchangeSpectrum.resize(m_internalHeight);
            m_internalImg = m_internalImg.rescaled(m_internalWidth,m_internalHeight);
            m_displaymem_writepos = 0;
        }
        stilldataavailable = m_processor.m_algo.getMemSlice(m_exchangeSpectrum);
        if (stilldataavailable)
        {
            m_displaymem[m_displaymem_writepos] = m_exchangeSpectrum;
            // write into Bitmap
            if (m_isRunningDisplay)
            {
                Image::BitmapData destData (m_internalImg, Image::BitmapData::writeOnly);                
                for (size_t hh = 0; hh < m_internalHeight; ++hh)
                {
                    float val = m_displaymem.at(m_displaymem_writepos).at(hh);

                    unsigned int color = m_colorpalette.getRGBColor(val);
                    color = color|0xFF000000; // kein alpha blending

                    //m_internalImg.setPixelAt(neww,m_internalHeight-1-hh,juce::Colour(color));
                    destData.setPixelColour (m_displaymem_writepos,m_internalHeight-1-hh,juce::Colour(color));
                }
                // plot red line at write position
                int drawwidth = 1;
                if (m_internalHeight < 2048)
                    drawwidth++;

                if (m_internalHeight < 1024)
                    drawwidth +=2 ;
                for (size_t hh = 0; hh < m_internalHeight; ++hh)
                {

                    for (int dd = 1 ; dd <= drawwidth ;++dd)
                    {
                        int drawpos = m_displaymem_writepos+dd;
                        if (drawpos >= m_internalWidth)
                            drawpos -= m_internalWidth;
                        destData.setPixelColour(drawpos,m_internalHeight-1-hh,juce::Colours::red);

                    }
                }

            }
            //if (!m_isRunningDisplay)           
            //    m_internalImg.moveImageSection(0,0,1,0,m_internalWidth-1,m_internalHeight);

            m_displaymem_writepos++;
            if (m_displaymem_writepos >= m_internalWidth)
            {
                m_displaymem_writepos = 0;
            }
            m_newDataAvailable++;
        }
    } while(stilldataavailable);

    // copy from displaymem to internal image
    if (!m_isRunningDisplay)
    {
        int startread = m_displaymem_writepos - m_newDataAvailable;
        m_internalImg.moveImageSection(0,0,m_newDataAvailable,0,m_internalWidth-m_newDataAvailable,m_internalHeight);
        const Image::BitmapData destData (m_internalImg, 0, 0, m_internalWidth, m_internalHeight, Image::BitmapData::readWrite);

        for (size_t ww = m_internalWidth-m_newDataAvailable ; ww < m_internalWidth; ++ww)
        {
            int readpos;
            if (startread < 0)
                readpos = m_internalWidth+(startread);
            else
                readpos = startread;
            for (size_t hh = 0; hh < m_internalHeight; ++hh)
            {
                float val = m_displaymem.at(readpos).at(hh);

                unsigned int color = m_colorpalette.getRGBColor(val);
                color = color|0xFF000000; // kein alpha blending
                destData.setPixelColour(ww,m_internalHeight-1-hh,juce::Colour(color));
                //m_internalImg.setPixelAt(ww,m_internalHeight-1-hh,juce::Colour(color));
            }
            startread++;
        }

    }
    if (m_recomputeAll == true) // if the color palette has changed, we have to recompute all colors in the bitmap
    {
        m_recomputeAll = false;
        int newwstart = m_internalWidth-m_displaymem_writepos;
        const Image::BitmapData destData (m_internalImg, 0, 0, m_internalWidth, m_internalHeight, Image::BitmapData::readWrite);

        for (size_t ww = 0; ww < m_internalWidth; ++ww)
        {
            int neww = ww+newwstart;
            if (neww>=m_internalWidth)
                neww -= m_internalWidth;
            for (size_t hh = 0; hh < m_internalHeight; ++hh)
            {
                float val = m_displaymem.at(ww).at(hh);

                unsigned int color = m_colorpalette.getRGBColor(val);
                color = color|0xFF000000; // kein alpha blending
                if (m_isRunningDisplay)
                {
                    destData.setPixelColour (ww,m_internalHeight-1-hh,juce::Colour(color));
                }
                else
                {
                    destData.setPixelColour (neww,m_internalHeight-1-hh,juce::Colour(color));
                }
            }
            if (m_isRunningDisplay)
            {
                // plot red line at write position
                int drawwidth = 1;
                if (m_internalHeight < 2048)
                    drawwidth++;

                if (m_internalHeight < 1024)
                    drawwidth +=2 ;
                for (size_t hh = 0; hh < m_internalHeight; ++hh)
                {

                    for (int dd = 1 ; dd <= drawwidth ;++dd)
                    {
                        int drawpos = m_displaymem_writepos+dd;
                        if (drawpos >= m_internalWidth)
                            drawpos -= m_internalWidth;
                        destData.setPixelColour(drawpos,m_internalHeight-1-hh,juce::Colours::red);

                    }
                }
            }
        }
    }   
    crit.exit();
    repaint();
}

void JadeSpectrogramGUI::runClicked()
{
    m_isRunningDisplay = !m_isRunningDisplay;
    
    if (m_isRunningDisplay)
    {
        m_runModeButton.setButtonText("Fix");
        m_runModeButton.setToggleState(false,NotificationType::dontSendNotification);
    }
    else
    {
        m_runModeButton.setButtonText("Run");
        m_runModeButton.setToggleState(true,NotificationType::dontSendNotification);
    }    
}
void JadeSpectrogramGUI::pauseClicked()
{
    m_isPaused = !m_isPaused;
    m_processor.m_algo.setPauseMode(m_isPaused);
    if (m_isPaused)
    {
        m_pauseButton.setToggleState(true,NotificationType::dontSendNotification);
    }
    else
    {
        m_pauseButton.setToggleState(false,NotificationType::dontSendNotification);
    }
}

void JadeSpectrogramGUI::changeFFTSize()
{
    auto FFTSizeIndex = m_fftSizeCombo.getSelectedItemIndex();
    int fftSize = pow(2.0,9+FFTSizeIndex);
    
    //DBG(String(fftSize));
    stopTimer();
    m_processor.m_algo.setFFTSize(fftSize);
    startTimer(40);
}

void JadeSpectrogramGUI::mouseMove (const MouseEvent& event)
{
    int x = event.getMouseDownX();
    int y = event.getMouseDownY();

    int w = getWidth();
    int h = getHeight();
    int wstart = m_scaleFactor*(g_FreqMeter+g_SliderMaxFreq_x+g_SliderWidth);
    if (y < h-m_scaleFactor*g_ButtonHeight && x > wstart && x < wstart + 0.8*w)
    {
        float freq = (1.0-float(y)/(float(h)-m_scaleFactor*g_ButtonHeight))*(m_maxDisplayFreq - m_minDisplayFreq)+m_minDisplayFreq;
        MidiMessage msg;
        int midinotenumber =  int(log(freq/440.0)/log(2) * 12 + 69 + 0.5);
        String midiNoteName = msg.getMidiNoteName(midinotenumber,true,true,4);

        m_FreqLabel.setText(String(int(freq+0.5)) + String(" Hz | ") + midiNoteName,juce::NotificationType::dontSendNotification);

    }


}
