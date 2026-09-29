#include <math.h>
#include <cassert>
#include "JadeSpectrogram.h"

#include "PluginProcessor.h"
const float g_minValForLogSpectrogram = 1e-10f;

JadeSpectrogramAudio::JadeSpectrogramAudio(JadeSpectrogramAudioProcessor* processor)
:SynchronBlockProcessor(), m_processor(processor), m_fs(48000.f), m_channels(2), m_fftsize(2048), m_freqsize(2048/2+1),
m_fifo(1000, g_maxFFTSize/2+1) // 1000 time slices should be enough
{
    m_mixMode = JadeSpectrogramAudio::ChannelMixMode::AbsMean;
    m_windowChoice = SpectrumAnalyzer::WindowType::Hann;
    m_PauseMode = false;
    // pure analyzer: the audio passes unchanged and without latency
    setProcessingMode(SynchronBlockProcessor::ProcessingMode::Analyze);
    m_fifo.setActSize(999, g_maxFFTSize/2+1); // we push one time slice after the other
    m_fifo.reset();
    m_fifo.fill(10.f*log10f(g_minValForLogSpectrogram)); // fill with very low values
}

void JadeSpectrogramAudio::prepareToPlay(double sampleRate, int max_samplesPerBlock, int max_channels)
{
    juce::ignoreUnused(max_samplesPerBlock);
    m_channels = static_cast<size_t>(max_channels);
    // here your code
    m_fs = static_cast<float>(sampleRate);

    // allocate everything for all FFT sizes here, so switchFFTSize() never allocates
    for (size_t idx = 0; idx < g_nrOfFFTSizes; ++idx)
    {
        size_t fftsize = size_t(1) << (g_minFFTSizeLog2 + idx);
        for (auto* analyzer : {&m_leftAnalyzers[idx], &m_rightAnalyzers[idx]})
        {
            analyzer->setSampleRate(sampleRate);
            analyzer->setOverlap(SpectrumAnalyzer::OverlapPercentage::perc50);
            analyzer->setBlockSize(fftsize);
            analyzer->setFFTSize(fftsize);
            analyzer->setWindowType(m_windowChoice);
            analyzer->reset();
        }
    }
    const size_t maxHopSize = m_leftAnalyzers[g_nrOfFFTSizes-1].getHopSize();
    prepareSynchronProcessing(static_cast<int>(m_channels),static_cast<int>(maxHopSize));
    m_timeInLeft.reserve(maxHopSize);
    m_timeInRight.reserve(maxHopSize);
    m_perLeft.reserve(g_maxFFTSize/2+1);
    m_perRight.reserve(g_maxFFTSize/2+1);
    m_power.reserve(g_maxFFTSize/2+1);

    m_fftsize = 0; // force the switch, also if the requested size did not change
    applyPendingChanges();
}

void JadeSpectrogramAudio::processBlock(juce::AudioBuffer<float>& data, juce::MidiBuffer& midiMessages)
{
    applyPendingChanges();
    SynchronBlockProcessor::processBlock(data, midiMessages);
}

void JadeSpectrogramAudio::applyPendingChanges()
{
    const size_t requestedFFTSize = m_requestedFFTSize.load();
    if (requestedFFTSize != m_fftsize)
        switchFFTSize(requestedFFTSize);

    const auto requestedWindow = m_requestedWindow.load();
    if (requestedWindow != m_windowChoice)
    {
        m_windowChoice = requestedWindow;
        // same size as before, so the window is recomputed in place
        m_leftAnalyzers[m_activeAnalyzer].setWindowType(m_windowChoice);
        m_rightAnalyzers[m_activeAnalyzer].setWindowType(m_windowChoice);
    }
}

void JadeSpectrogramAudio::switchFFTSize(size_t newFFTSize)
{
    // map to the prepared analyzers (512 ... 8192), unsupported sizes are clamped
    size_t idx = 0;
    while (idx + 1 < g_nrOfFFTSizes && (size_t(1) << (g_minFFTSizeLog2 + idx)) < newFFTSize)
        ++idx;
    m_activeAnalyzer = idx;
    m_fftsize = newFFTSize;

    auto& left = m_leftAnalyzers[m_activeAnalyzer];
    auto& right = m_rightAnalyzers[m_activeAnalyzer];
    left.reset();
    right.reset();
    if (left.getWindowType() != m_windowChoice)
    {
        left.setWindowType(m_windowChoice);
        right.setWindowType(m_windowChoice);
    }

    // synchronblocksize should be the same as the hop size of the analyzers
    const size_t synchronblocksize = left.getHopSize();
    prepareSynchronProcessing(static_cast<int>(m_channels),static_cast<int>(synchronblocksize));
    m_Latency.store(getDelay()); // 0 in Analyze mode

    // all within the capacity reserved in prepareToPlay, so no allocation
    m_timeInLeft.resize(synchronblocksize);
    m_timeInRight.resize(synchronblocksize);
    m_freqsize = (size_t(1) << (g_minFFTSizeLog2 + m_activeAnalyzer))/2 + 1;
    m_perLeft.resize(m_freqsize);
    m_perRight.resize(m_freqsize);
    m_power.resize(m_freqsize);
    m_publishedFreqSize.store(m_freqsize);
}

int JadeSpectrogramAudio::processSynchronBlock(juce::AudioBuffer<float> & buffer, juce::MidiBuffer &midiMessages, int NrOfBlocksSinceLastProcessBlock)
{
    juce::ignoreUnused(midiMessages, NrOfBlocksSinceLastProcessBlock);
    // read the GUI-controlled values once per block (consistent within the block)
    const auto mixMode = m_mixMode.load(std::memory_order_relaxed);
    const float fs = m_fs.load(std::memory_order_relaxed);

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
            {
                if (mixMode == JadeSpectrogramAudio::ChannelMixMode::TimeMean)
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
    }
    // Compute Periodograms for each channel
    m_leftAnalyzers[m_activeAnalyzer].getPeriodogram(m_timeInLeft, m_perLeft);
    if (numChannels>1)
    {
        m_rightAnalyzers[m_activeAnalyzer].getPeriodogram(m_timeInRight, m_perRight);
        switch (mixMode)
        {
            case JadeSpectrogramAudio::ChannelMixMode::TimeMean: // mean is already in both time signals
                m_power = m_perLeft;
                break;
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
        m_power[kk] = 10.f*log10f(2.f*m_power[kk]/fs + g_minValForLogSpectrogram);
    }
    // save into mem
    if (!m_PauseMode.load(std::memory_order_relaxed))
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

void JadeSpectrogramAudio::setclosestFFTSize_ms(float fftsize_ms)
{
    setFFTSize(getnextpowerof2(fftsize_ms));
}

size_t JadeSpectrogramAudio::getnextpowerof2(float fftsize_ms)
{
    float firstguessFFTSize = (fftsize_ms*0.001f*m_fs.load());
    int nextpowerof2 = static_cast<int>(log(firstguessFFTSize)/log(2.f))+1;
    return static_cast<size_t>(pow(2.f,static_cast<float>(nextpowerof2)));
}


JadeSpectrogramGUI::JadeSpectrogramGUI(JadeSpectrogramAudioProcessor& p, juce::AudioProcessorValueTreeState& apvts)
:m_processor(p) ,m_apvts(apvts),
m_internalImg(Image::RGB,1,1,true),
m_internalHeight(1), m_recomputeAll(true),m_maxColorVal(g_maxColorVal),m_minColorVal(g_minColorVal),
m_colorpalette(256,CColorPalette::PaletteName::kPlasma),m_maxDisplayFreq(20000.f),m_minDisplayFreq(1.f),
//somethingChanged(nullptr),
m_isPaused(false),m_isRunningDisplay(false),m_hideFFTSizeCombobox(false)
//,m_editor(editor)
{
    m_internalHeight = m_processor.m_algo.getSpectrumSize();
    float fs = m_processor.m_algo.getSamplerate();
    m_internalWidth = static_cast<size_t>(g_pastTimeMemLen_s * fs/static_cast<float>(m_internalHeight-1)); ///(m_internalHeigt-1) is hopsize

    m_exchangeSpectrum.resize(m_internalHeight);

    m_displaymem.resize(m_internalWidth);
    for (auto &vec : m_displaymem)
    {
        vec.resize(static_cast<size_t>(m_internalHeight));
        std::fill(vec.begin(), vec.end(), 10.f*log10f(g_minValForLogSpectrogram));
    }
    m_imageRows = m_internalHeight;
    m_internalImg = m_internalImg.rescaled(static_cast<int>(m_internalWidth),static_cast<int>(m_imageRows));

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

    m_logFreqAxis = m_processor.getLogFreqAxis();
    setFreqAxisButtonText();
    m_freqAxisButton.onClick = [this](){freqAxisClicked();};
    // no addAndMakeVisible here: the editor shows the button above the frequency axis (title bar)
    updateFrequencyMapping();

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
    m_colorScheme.setSelectedItemIndex(static_cast<int>(CColorPalette::PaletteName::kPlasma),NotificationType::dontSendNotification);
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

    // show the size the processor runs with (the editor may be reopened after a change)
    int fftSizeIndex = static_cast<int>(std::log2(static_cast<double>(m_processor.m_algo.getFFTSize()))) - 9;
    m_fftSizeCombo.setSelectedItemIndex(juce::jlimit(0, 4, fftSizeIndex),NotificationType::dontSendNotification);
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

    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker(0.2f));

    int w = getWidth();
    int h = getHeight();

    float fs = m_processor.m_algo.getSamplerate();
    
    m_minDisplayFreq = static_cast<float>(exp(m_DisplayMinFreqSlider.getValue()));
    m_maxDisplayFreq = static_cast<float>(exp(m_DisplayMaxFreqSlider.getValue()));

    if (m_minDisplayFreq >= fs*0.5f)
        m_minDisplayFreq = 0.9f*fs*0.5f;
    if (m_maxDisplayFreq >= fs*0.5f)
        m_maxDisplayFreq = fs*0.5f;

    if (1.1f*m_minDisplayFreq >= m_maxDisplayFreq)
    {
        //m_minDisplayFreq = 0.8f*m_maxDisplayFreq;
        m_maxDisplayFreq = 1.1f*m_minDisplayFreq;
        m_DisplayMaxFreqSlider.setValue(log(1.1f*m_minDisplayFreq));
        m_DisplayMinFreqSlider.setValue(log(m_minDisplayFreq));
    }


    //int displayStartPixel = int(2.0f*m_minDisplayFreq/fs *m_internalHeight+0.5f);
    int displayEndPixel = int(2.0f*m_maxDisplayFreq/fs *m_internalHeight + 0.5f);
    int heightInterval = int(2.0f*m_maxDisplayFreq/fs *m_internalHeight -2.0f*m_minDisplayFreq/fs *m_internalHeight+0.5f);

    int hStart = static_cast<int>(m_internalHeight) - displayEndPixel;

    int wStartPic =static_cast<int>(m_scaleFactor*(g_SliderWidth + g_FreqMeter));

    int TextHeight = 20;
    int nrOfYTicks = 11;
    float RangePerTick = 0.f;
    if (!m_rowMap.empty()) // logarithmic axis: the image already covers exactly the displayed range
    {
        g.drawImage(m_internalImg,wStartPic,0,static_cast<int>(0.8f*w),int(float(h)-m_scaleFactor*g_menuHeight+0.5),
                    0,0,static_cast<int>(m_internalWidth),static_cast<int>(m_imageRows));
        // ticks at 1, 2, 5 x 10^k
        g.setFont(0.8f*m_scaleFactor*static_cast<float>(TextHeight));
        const float displayH = float(h)-m_scaleFactor*g_menuHeight;
        const float logRange = std::log(m_mapMaxFreq/m_mapMinFreq);
        const float textH = m_scaleFactor*static_cast<float>(TextHeight);
        for (float decade = 10.f; decade <= 20000.f; decade *= 10.f)
            for (float mult : {1.f, 2.f, 5.f})
            {
                const float f = decade*mult;
                if (f < m_mapMinFreq*0.999f || f > m_mapMaxFreq*1.001f)
                    continue;
                const String OutText = (f >= 1000.f) ? String(f/1000.f) + "k" : String(static_cast<int>(f));
                const float ydelta = displayH*std::log(f/m_mapMinFreq)/logRange;
                const float y = juce::jlimit(0.f, displayH-textH, displayH - ydelta - 0.5f*textH);
                g.drawText(OutText,static_cast<int>(static_cast<float>(wStartPic)-g_FreqMeter*m_scaleFactor),static_cast<int>(y),
                        static_cast<int>(g_FreqMeter*m_scaleFactor),static_cast<int>(textH),juce::Justification::centred,true);
            }
    }
    else
    {
    g.drawImage(m_internalImg,wStartPic,0,static_cast<int>(0.8f*w),int(float(h)-m_scaleFactor*g_menuHeight+0.5),
                0,hStart,static_cast<int>(m_internalWidth),heightInterval);
    // Add frequency scale
    RangePerTick = float(m_maxDisplayFreq - m_minDisplayFreq)/(nrOfYTicks-1);
    g.setFont(0.8*m_scaleFactor*TextHeight);
    for (auto kk = 0; kk < nrOfYTicks; ++kk)
    {
        float newExaktFreq = int(m_minDisplayFreq + RangePerTick*kk + 0.5);
        String OutText;
        if (newExaktFreq >= 1000.f)
        {
            newExaktFreq = int(newExaktFreq*0.02f +0.5f)*50.f;
            OutText += String(newExaktFreq/1000.f);
            OutText += "k";
        }
        else
        {
            if (newExaktFreq >= 150.f)
            {
                newExaktFreq = int(newExaktFreq*0.2f +0.5f)*5.f;
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
        g.drawText(OutText,x,y,static_cast<int>(g_FreqMeter*m_scaleFactor),static_cast<int>(m_scaleFactor*TextHeight),
                juce::Justification::centred,true);
    }
    }

    // Plot Colorbar
    int cbHeight = static_cast<int>(h-m_scaleFactor*g_menuHeight);
    Image colorbar(Image::RGB,1,cbHeight,true);
    for (int kk = 0; kk < cbHeight; kk++)
    {   
        float val = float(kk)/cbHeight*(g_maxColorVal - g_minColorVal) + g_minColorVal;
        juce::uint32 color = m_colorpalette.getRGBColor(val) | 0xFF000000u; // kein alpha blending
        colorbar.setPixelAt(0,cbHeight-1-kk,juce::Colour(color));
    }
    g.drawImage(colorbar,w-static_cast<int>(m_scaleFactor*(g_colorbar_width+g_FreqMeter + g_SliderWidth)),0,static_cast<int>(m_scaleFactor*g_colorbar_width),static_cast<int>(h-m_scaleFactor*g_menuHeight),
                0,0,1,cbHeight);

    // draw scale
    // Add colorbar scale
    nrOfYTicks = 11;
    RangePerTick = float(g_maxColorVal - g_minColorVal)/(nrOfYTicks-1);
    for (auto kk = 0; kk < nrOfYTicks; ++kk)
    {
        float newExaktFreq = int((g_minColorVal + RangePerTick*kk)*0.1f)*10.f;
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
        
        g.drawText(OutText,x,y,static_cast<int>(g_FreqMeter*m_scaleFactor),static_cast<int>(m_scaleFactor*TextHeight),
                juce::Justification::centred,true);
    }
    
    m_newDataAvailable = 0;

    drawCrosshair(g, juce::Rectangle<int>(wStartPic, 0, static_cast<int>(0.8f*w),
                                          int(float(h)-m_scaleFactor*g_menuHeight+0.5)));

    g.setColour (JadeTeal);
    g.setFont (9.0f*m_scaleFactor);
    
    juce::String text2display = "V " + juce::String(PLUGIN_VERSION_MAJOR) + "." + juce::String(PLUGIN_VERSION_MINOR) + "." + juce::String(PLUGIN_VERSION_PATCH);
    g.drawFittedText (text2display, getLocalBounds(), juce::Justification::bottomLeft, 1);

}

void JadeSpectrogramGUI::resized()
{
	auto r = getLocalBounds();
   
    // if you have to place several components, use scaleFactor
    int width = r.getWidth();
    int h = r.getHeight();
	m_scaleFactor = float(width)/g_minGuiSize_x;

    // use the given canvas in r
    m_DisplayMaxFreqSlider.setBounds(static_cast<int>(m_scaleFactor*g_SliderMaxFreq_x),static_cast<int>(m_scaleFactor*g_SliderMaxFreq_y),
            static_cast<int>(m_scaleFactor*g_SliderWidth),static_cast<int>(m_scaleFactor*g_SliderHeight));
    m_DisplayMinFreqSlider.setBounds(static_cast<int>(m_scaleFactor*g_SliderMinFreq_x),static_cast<int>(m_scaleFactor*g_SliderMinFreq_y),
            static_cast<int>(m_scaleFactor*g_SliderWidth),static_cast<int>(m_scaleFactor*g_SliderHeight));

    m_DisplayMaxColorSlider.setBounds(static_cast<int>(m_scaleFactor*g_SliderMaxColor_x),static_cast<int>(m_scaleFactor*g_SliderMaxColor_y),
            static_cast<int>(m_scaleFactor*g_SliderWidth),static_cast<int>(m_scaleFactor*g_SliderHeight));
    m_DisplayMinColorSlider.setBounds(static_cast<int>(m_scaleFactor*g_SliderMinColor_x),static_cast<int>(m_scaleFactor*g_SliderMinColor_y),
            static_cast<int>(m_scaleFactor*g_SliderWidth),static_cast<int>(m_scaleFactor*g_SliderHeight));

    m_pauseButton.setBounds(static_cast<int>(m_scaleFactor*g_PauseButton_x),  static_cast<int>(static_cast<float>(h)-m_scaleFactor*g_menuHeight+0.5f),
                    static_cast<int>(m_scaleFactor*g_ButtonWidth),static_cast<int>(m_scaleFactor*g_ButtonHeight));

    int w = getWidth();
    int x = static_cast<int>(m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.8*w - m_scaleFactor*g_ButtonWidth);

    m_runModeButton.setBounds(x, static_cast<int>(static_cast<float>(h)-m_scaleFactor*g_menuHeight+0.5f),
                    static_cast<int>(m_scaleFactor*g_ButtonWidth),static_cast<int>(m_scaleFactor*g_ButtonHeight));

    m_colorScheme.setBounds(w-static_cast<int>(m_scaleFactor*(g_colorbar_width+g_FreqMeter + g_SliderWidth)), static_cast<int>(static_cast<float>(h)-m_scaleFactor*g_menuHeight+0.5f),
                    static_cast<int>(m_scaleFactor*g_colorbar_width), static_cast<int>(m_scaleFactor*g_ButtonHeight));

    int LabelWidth = 140;
    x = static_cast<int>(m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.4*w - m_scaleFactor*0.5*LabelWidth);
    m_FreqLabel.setBounds(x , static_cast<int>(static_cast<float>(h)-m_scaleFactor*g_menuHeight+0.5f),static_cast<int>(m_scaleFactor*LabelWidth),static_cast<int>(m_scaleFactor*g_ButtonHeight));                    

    x = static_cast<int>(m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.2*w - m_scaleFactor*0.5*100);
    m_windowFktCombo.setBounds(x, static_cast<int>(static_cast<float>(h)-m_scaleFactor*g_menuHeight+0.5f),static_cast<int>(m_scaleFactor*100),static_cast<int>(m_scaleFactor*g_ButtonHeight));

    x = static_cast<int>(m_scaleFactor*(g_SliderWidth + g_FreqMeter) + 0.6*w - m_scaleFactor*0.5*100);
    m_fftSizeCombo.setBounds(x, static_cast<int>(static_cast<float>(h)-m_scaleFactor*g_menuHeight+0.5f),static_cast<int>(m_scaleFactor*100),static_cast<int>(m_scaleFactor*g_ButtonHeight));
            
}

void JadeSpectrogramGUI::timerCallback()
{
    float maxValColor = m_DisplayMaxColorSlider.getValue();
    float minValColor = m_DisplayMinColorSlider.getValue();

    m_colorpalette.setValueRange(minValColor,maxValColor);
    updateFrequencyMapping(); // log axis: follows the frequency sliders

    bool stilldataavailable;
    do
    {
        // the size travels with each slice, so a new FFT size shows up exactly with its first slice
        size_t actSpectrumSize = m_processor.m_algo.getNextMemSliceSize();
        if (actSpectrumSize == 0) // nothing new
            break;
        if (actSpectrumSize != m_internalHeight)
        {
            m_internalHeight = actSpectrumSize;
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
            m_displaymem_writepos = 0;
            updateFrequencyMapping(); // new number of bins: new image size (and log mapping)
        }
        stilldataavailable = m_processor.m_algo.getMemSlice(m_exchangeSpectrum);
        if (stilldataavailable)
        {
            m_displaymem[m_displaymem_writepos] = m_exchangeSpectrum;
            // write into Bitmap
            if (m_isRunningDisplay)
            {
                Image::BitmapData destData (m_internalImg, Image::BitmapData::writeOnly);                
                for (size_t hh = 0; hh < m_imageRows; ++hh)
                {
                    float val = rowValue(m_displaymem.at(m_displaymem_writepos), hh);

                    unsigned int color = m_colorpalette.getRGBColor(val);
                    color = color|0xFF000000; // kein alpha blending

                    //m_internalImg.setPixelAt(neww,m_internalHeight-1-hh,juce::Colour(color));
                    destData.setPixelColour (m_displaymem_writepos,m_imageRows-1-hh,juce::Colour(color));
                }
                // plot red line at write position
                int drawwidth = 1;
                if (m_internalHeight < 2048)
                    drawwidth++;

                if (m_internalHeight < 1024)
                    drawwidth +=2 ;
                for (size_t hh = 0; hh < m_imageRows; ++hh)
                {

                    for (int dd = 1 ; dd <= drawwidth ;++dd)
                    {
                        size_t drawpos = m_displaymem_writepos + static_cast<size_t>(dd);
                        if (drawpos >= m_internalWidth)
                            drawpos -= m_internalWidth;
                        destData.setPixelColour(drawpos,m_imageRows-1-hh,juce::Colours::red);

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
        m_internalImg.moveImageSection(0,0,m_newDataAvailable,0,m_internalWidth-m_newDataAvailable,m_imageRows);
        const Image::BitmapData destData (m_internalImg, 0, 0, m_internalWidth, m_imageRows, Image::BitmapData::readWrite);

        for (size_t ww = m_internalWidth-m_newDataAvailable ; ww < m_internalWidth; ++ww)
        {
            size_t readpos;
            if (startread < 0)
                readpos = m_internalWidth - static_cast<size_t>(-startread);
            else
                readpos = static_cast<size_t>(startread);
            for (size_t hh = 0; hh < m_imageRows; ++hh)
            {
                float val = rowValue(m_displaymem.at(readpos), hh);

                unsigned int color = m_colorpalette.getRGBColor(val);
                color = color|0xFF000000; // kein alpha blending
                destData.setPixelColour(ww,m_imageRows-1-hh,juce::Colour(color));
                //m_internalImg.setPixelAt(ww,m_internalHeight-1-hh,juce::Colour(color));
            }
            startread++;
        }
        auto p = getMouseXYRelative();
        setLabelText(p.getX(),p.getY());

    }
    if (m_recomputeAll == true) // if the color palette has changed, we have to recompute all colors in the bitmap
    {
        m_recomputeAll = false;
        size_t newwstart = m_internalWidth-m_displaymem_writepos; // writepos < width, so > 0
        const Image::BitmapData destData (m_internalImg, 0, 0, m_internalWidth, m_imageRows, Image::BitmapData::readWrite);

        for (size_t ww = 0; ww < m_internalWidth; ++ww)
        {
            size_t neww = ww+newwstart;
            if (neww>=m_internalWidth)
                neww -= m_internalWidth;
            for (size_t hh = 0; hh < m_imageRows; ++hh)
            {
                float val = rowValue(m_displaymem.at(ww), hh);

                unsigned int color = m_colorpalette.getRGBColor(val);
                color = color|0xFF000000; // kein alpha blending
                if (m_isRunningDisplay)
                {
                    destData.setPixelColour (ww,m_imageRows-1-hh,juce::Colour(color));
                }
                else
                {
                    destData.setPixelColour (neww,m_imageRows-1-hh,juce::Colour(color));
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
                for (size_t hh = 0; hh < m_imageRows; ++hh)
                {

                    for (int dd = 1 ; dd <= drawwidth ;++dd)
                    {
                        size_t drawpos = m_displaymem_writepos + static_cast<size_t>(dd);
                        if (drawpos >= m_internalWidth)
                            drawpos -= m_internalWidth;
                        destData.setPixelColour(drawpos,m_imageRows-1-hh,juce::Colours::red);

                    }
                }
            }
        }
    }   






    repaint();
}

void JadeSpectrogramGUI::runClicked()
{
    m_isRunningDisplay = !m_isRunningDisplay;
    
    // the label shows what a click does (as at startup: scrolling display -> "Fix")
    if (m_isRunningDisplay) // fixed image, red cursor runs over it
    {
        m_runModeButton.setButtonText("Run");
        m_runModeButton.setToggleState(true,NotificationType::dontSendNotification);
    }
    else // scrolling display
    {
        m_runModeButton.setButtonText("Fix");
        m_runModeButton.setToggleState(false,NotificationType::dontSendNotification);
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
    // only a request, the audio thread switches at its next block;
    // the timer picks up the new size with the first slice of that size
    m_processor.m_algo.setFFTSize(static_cast<size_t>(fftSize));
}

void JadeSpectrogramGUI::mouseMove (const MouseEvent& event)
{
    m_mousePos = event.getPosition();
    m_mouseInDisplay = setLabelText(m_mousePos.getX(), m_mousePos.getY());
    repaint();
}

void JadeSpectrogramGUI::mouseExit (const MouseEvent& event)
{
    juce::ignoreUnused(event);
    m_mouseInDisplay = false;
    repaint();
}

void JadeSpectrogramGUI::setFreqAxisButtonText()
{
    // as the Run/Fix button: the label shows what a click does; same colour in both states
    // (lin and log are equally important)
    m_freqAxisButton.setButtonText(m_logFreqAxis ? "Lin" : "Log");
}

void JadeSpectrogramGUI::freqAxisClicked()
{
    m_logFreqAxis = !m_logFreqAxis;
    m_processor.setLogFreqAxis(m_logFreqAxis);
    setFreqAxisButtonText();
    updateFrequencyMapping();
    repaint();
}

void JadeSpectrogramGUI::updateFrequencyMapping()
{
    const size_t bins = m_internalHeight;
    if (!m_logFreqAxis || bins < 2)
    {
        if (!m_rowMap.empty() || m_imageRows != bins)
        {
            m_rowMap.clear();
            m_imageRows = bins;
            m_recomputeAll = true;
        }
    }
    else
    {
        // displayed range: the frequency sliders, but at least g_logAxisMinFreq at the bottom
        const float fmax = m_maxDisplayFreq;
        const float fmin = std::min(std::max(m_minDisplayFreq, g_logAxisMinFreq), fmax/1.1f);
        const float fs = m_processor.m_algo.getSamplerate();
        const bool upToDate = m_rowMap.size() == g_logAxisRows && m_mapBins == bins
            && std::abs(m_mapMinFreq - fmin) < 1e-3f && std::abs(m_mapMaxFreq - fmax) < 1e-3f
            && std::abs(m_mapFs - fs) < 1e-3f;
        if (!upToDate)
        {
            m_mapMinFreq = fmin; m_mapMaxFreq = fmax; m_mapBins = bins; m_mapFs = fs;
            m_imageRows = g_logAxisRows;
            m_rowMap.resize(g_logAxisRows);
            const double binWidth = 0.5*double(fs)/double(bins-1); // Hz per bin
            const double ratio = double(fmax)/double(fmin);
            for (size_t r = 0; r < g_logAxisRows; ++r) // r = 0: bottom row
            {
                const double flo = fmin*std::pow(ratio, double(r)/double(g_logAxisRows));
                const double fhi = fmin*std::pow(ratio, double(r+1)/double(g_logAxisRows));
                const double blo = flo/binWidth, bhi = fhi/binWidth;
                RowMap& m = m_rowMap[r];
                if (bhi - blo > 1.0) // the row covers several bins: take their maximum (keeps narrow peaks)
                {
                    m.useMax = true;
                    m.bin0 = std::min(static_cast<size_t>(std::ceil(blo)), bins-1);
                    m.bin1 = std::max(m.bin0, std::min(static_cast<size_t>(std::floor(bhi)), bins-1));
                    m.frac = 0.f;
                }
                else // less than one bin per row: interpolate between the neighbours
                {
                    const double bc = std::sqrt(flo*fhi)/binWidth;
                    m.useMax = false;
                    m.bin0 = std::min(static_cast<size_t>(bc), bins-2);
                    m.bin1 = m.bin0 + 1;
                    m.frac = static_cast<float>(juce::jlimit(0.0, 1.0, bc - double(m.bin0)));
                }
            }
            m_recomputeAll = true;
        }
    }
    if (static_cast<size_t>(m_internalImg.getWidth()) != m_internalWidth
        || static_cast<size_t>(m_internalImg.getHeight()) != m_imageRows)
    {
        m_internalImg = m_internalImg.rescaled(static_cast<int>(m_internalWidth), static_cast<int>(m_imageRows));
        m_recomputeAll = true;
    }
}

float JadeSpectrogramGUI::rowValue(const std::vector<float>& column, size_t row) const
{
    if (m_rowMap.empty()) // linear axis: row = bin
        return column[row];
    const RowMap& m = m_rowMap[row];
    if (m.useMax)
    {
        float v = column[m.bin0];
        for (size_t b = m.bin0 + 1; b <= m.bin1; ++b)
            v = std::max(v, column[b]);
        return v;
    }
    return column[m.bin0] + m.frac*(column[m.bin1] - column[m.bin0]);
}

void JadeSpectrogramGUI::drawCrosshair(juce::Graphics& g, juce::Rectangle<int> display)
{
    if (!m_mouseInDisplay || !display.contains(m_mousePos))
        return;
    const float x = static_cast<float>(m_mousePos.getX()) + 0.5f;
    const float y = static_cast<float>(m_mousePos.getY()) + 0.5f;

    // thin lines through the mouse position, across the whole display
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.drawLine(static_cast<float>(display.getX()), y, static_cast<float>(display.getRight()), y, 1.0f);
    g.drawLine(x, static_cast<float>(display.getY()), x, static_cast<float>(display.getBottom()), 1.0f);

    // readout (the same text as below the display) next to the cursor,
    // on the other side of the cursor near the right and bottom edges
    const juce::String text = m_FreqLabel.getText();
    const juce::Font font(juce::FontOptions(13.0f*m_scaleFactor));
    const int textW = juce::GlyphArrangement::getStringWidthInt(font, text) + static_cast<int>(10.0f*m_scaleFactor);
    const int textH = static_cast<int>(18.0f*m_scaleFactor);
    const int gap = static_cast<int>(10.0f*m_scaleFactor);
    juce::Rectangle<int> box(m_mousePos.getX() + gap, m_mousePos.getY() + gap, textW, textH);
    if (box.getRight() > display.getRight())
        box.setX(m_mousePos.getX() - gap - textW);
    if (box.getBottom() > display.getBottom())
        box.setY(m_mousePos.getY() - gap - textH);

    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.fillRoundedRectangle(box.toFloat(), 3.0f*m_scaleFactor);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(text, box, juce::Justification::centred, false);
}

bool JadeSpectrogramGUI::setLabelText(int x, int y)
{
   
    int w = getWidth();
    int h = getHeight();
    int wstart = m_scaleFactor*(g_FreqMeter+g_SliderMaxFreq_x+g_SliderWidth);
    if (y >= 0 && y < h-m_scaleFactor*g_ButtonHeight && x > wstart && x < wstart + 0.8*w)
    {
        
        const float yrel = 1.f - float(y)/(float(h)-m_scaleFactor*g_menuHeight); // 0 bottom ... 1 top
        float freq;
        if (!m_rowMap.empty()) // logarithmic axis
            freq = m_mapMinFreq * std::pow(m_mapMaxFreq/m_mapMinFreq, yrel);
        else
            freq = yrel*(m_maxDisplayFreq - m_minDisplayFreq)+m_minDisplayFreq;
        // recompute freq to freq index in the internal memory
        float fshalf = 0.5f*m_processor.m_algo.getSamplerate();
        size_t freqindex  = static_cast<size_t> ((m_internalHeight-1) * freq / fshalf + 0.5f);
        freqindex = std::min(freqindex, m_internalHeight-1);

        // recompute the time index from display index
        float maxw = 0.8*w;
        size_t timeindex = static_cast<size_t> ((m_internalWidth-1) * static_cast<size_t>(x-wstart)/maxw +0.5); // x > wstart (see if)
        float val = -100.f;
        if (m_isRunningDisplay)
        {   
            val = m_displaymem.at(timeindex).at(freqindex);
        }
        else
        {
            // index of the column that is shown at timeindex (the newest data is at the right edge)
            size_t distanz = m_internalWidth - timeindex; // 1 ... width
            size_t memindex = (m_displaymem_writepos >= distanz) ? m_displaymem_writepos - distanz
                                                                 : m_displaymem_writepos + m_internalWidth - distanz;

            val = m_displaymem.at(memindex).at(freqindex);

        }

        MidiMessage msg;
        int midinotenumber =  int(log(freq/440.0)/log(2) * 12 + 69 + 0.5);
        String midiNoteName = msg.getMidiNoteName(midinotenumber,true,true,4);

        m_FreqLabel.setText(String(int(freq+0.5)) + String(" Hz | ") + midiNoteName + String(" | ") + String(0.1*int(val*10+0.5),1) + String(" dB") ,juce::NotificationType::dontSendNotification);
        return true;
    }
    return false;
}

