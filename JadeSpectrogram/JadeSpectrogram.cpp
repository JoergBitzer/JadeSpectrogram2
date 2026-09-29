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
    for (size_t ov = 0; ov < g_nrOfOverlaps; ++ov)
    for (size_t idx = 0; idx < g_nrOfFFTSizes; ++idx)
    {
        size_t fftsize = size_t(1) << (g_minFFTSizeLog2 + idx);
        for (auto* analyzer : {&m_leftAnalyzers[ov][idx], &m_rightAnalyzers[ov][idx]})
        {
            analyzer->setSampleRate(sampleRate);
            analyzer->setOverlap(ov == 0 ? SpectrumAnalyzer::OverlapPercentage::perc50 : SpectrumAnalyzer::OverlapPercentage::perc75);
            analyzer->setBlockSize(fftsize);
            analyzer->setFFTSize(fftsize);
            analyzer->setWindowType(m_windowChoice);
            analyzer->reset();
        }
    }
    const size_t maxHopSize = m_leftAnalyzers[0][g_nrOfFFTSizes-1].getHopSize(); // 50 %, largest FFT
    prepareSynchronProcessing(static_cast<int>(m_channels),static_cast<int>(maxHopSize));
    m_timeInLeft.reserve(maxHopSize);
    m_timeInRight.reserve(maxHopSize);
    m_perLeft.reserve(g_maxFFTSize/2+1);
    m_perRight.reserve(g_maxFFTSize/2+1);
    m_power.reserve(g_maxFFTSize/2+1);
    m_averagedPower.reserve(g_maxFFTSize/2+1);

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
    if (m_fftSizeParam != nullptr) // saved settings drive FFT size and window
    {
        const int idx = juce::jlimit(0, static_cast<int>(g_nrOfFFTSizes)-1, juce::roundToInt(m_fftSizeParam->load(std::memory_order_relaxed)));
        m_requestedFFTSize.store(size_t(1) << (g_minFFTSizeLog2 + static_cast<size_t>(idx)));
    }
    if (m_windowParam != nullptr)
    {
        const int idx = juce::jlimit(0, static_cast<int>(SpectrumAnalyzer::WindowType::NrOfWindowTypes)-1,
                                     juce::roundToInt(m_windowParam->load(std::memory_order_relaxed)));
        m_requestedWindow.store(static_cast<SpectrumAnalyzer::WindowType>(idx));
    }
    if (m_overlapParam != nullptr)
        m_requestedOverlap.store(juce::roundToInt(m_overlapParam->load(std::memory_order_relaxed)) > 0 ? 1u : 0u);
    const size_t requestedFFTSize = m_requestedFFTSize.load();
    if (requestedFFTSize != m_fftsize || m_requestedOverlap.load() != m_activeOverlap)
        switchFFTSize(requestedFFTSize);

    const auto requestedWindow = m_requestedWindow.load();
    if (requestedWindow != m_windowChoice)
    {
        m_windowChoice = requestedWindow;
        // same size as before, so the window is recomputed in place
        m_leftAnalyzers[m_activeOverlap][m_activeAnalyzer].setWindowType(m_windowChoice);
        m_rightAnalyzers[m_activeOverlap][m_activeAnalyzer].setWindowType(m_windowChoice);
    }
}

void JadeSpectrogramAudio::switchFFTSize(size_t newFFTSize)
{
    // map to the prepared analyzers (512 ... 8192), unsupported sizes are clamped
    size_t idx = 0;
    while (idx + 1 < g_nrOfFFTSizes && (size_t(1) << (g_minFFTSizeLog2 + idx)) < newFFTSize)
        ++idx;
    m_activeAnalyzer = idx;
    m_activeOverlap = m_requestedOverlap.load();
    m_fftsize = newFFTSize;

    auto& left = m_leftAnalyzers[m_activeOverlap][m_activeAnalyzer];
    auto& right = m_rightAnalyzers[m_activeOverlap][m_activeAnalyzer];
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
    m_averagedPower.resize(m_freqsize);
    m_averagingStarted = false; // new bins: restart the average
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
    m_leftAnalyzers[m_activeOverlap][m_activeAnalyzer].getPeriodogram(m_timeInLeft, m_perLeft);
    if (numChannels>1)
    {
        m_rightAnalyzers[m_activeOverlap][m_activeAnalyzer].getPeriodogram(m_timeInRight, m_perRight);
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
    // averaging along time: y += alpha*(x - y) per bin, alpha = T/tau with T = one hop;
    // tau <= T gives alpha = 1, i.e. off (the leftmost slider position)
    {
        const float tauMs = (m_averagingParam != nullptr) ? m_averagingParam->load(std::memory_order_relaxed)
                                                          : m_averagingMs.load(std::memory_order_relaxed);
        const float hopSeconds = static_cast<float>(numSamples)/fs;
        const float alpha = (tauMs*0.001f > hopSeconds) ? hopSeconds/(tauMs*0.001f) : 1.f;
        if (alpha < 1.f && m_averagingStarted)
        {
            for (size_t kk = 0; kk < m_freqsize ; ++kk)
            {
                m_averagedPower[kk] += alpha*(m_power[kk] - m_averagedPower[kk]);
                m_power[kk] = m_averagedPower[kk];
            }
        }
        else
        {
            // off (or the first block): the average follows the input, so switching on starts
            // from the current spectrum
            std::copy(m_power.begin(), m_power.end(), m_averagedPower.begin());
            m_averagingStarted = true;
        }
    }
    // convert to dB
    for (size_t kk = 0; kk < m_freqsize ; ++kk)
    {
        m_power[kk] = 10.f*log10f(2.f*m_power[kk]/fs + g_minValForLogSpectrogram);
    }
    // save into mem
    if (!m_PauseMode.load(std::memory_order_relaxed))
        m_fifo.push(m_power, SliceInfo{numSamples}); // numSamples = hop

    return 0;
}

static void addDisplaySettings(std::vector<std::unique_ptr<juce::RangedAudioParameter>> &paramVector);

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

    addDisplaySettings(paramVector);
}

// display settings: saved with the project, not automatable
static void addDisplaySettings(std::vector<std::unique_ptr<juce::RangedAudioParameter>> &paramVector)
{
    const auto choice = AudioParameterChoiceAttributes().withAutomatable(false);
    const auto boolean = AudioParameterBoolAttributes().withAutomatable(false);
    paramVector.push_back(std::make_unique<AudioParameterChoice>(JadeParamID::fftSize, "FFT size",
        StringArray{"512", "1024", "2048", "4096", "8192"}, 2, choice));
    paramVector.push_back(std::make_unique<AudioParameterChoice>(JadeParamID::window, "Window",
        StringArray{"Rectangular", "Hann", "Hamming", "BlackmanHarris", "FlatTop", "HannPoisson"},
        static_cast<int>(SpectrumAnalyzer::WindowType::Hann), choice));
    paramVector.push_back(std::make_unique<AudioParameterChoice>(JadeParamID::colorMap, "Colour map",
        StringArray{"Mono", "BW", "Hot", "Rainbow", "Viridis", "Plasma", "Jade"},
        static_cast<int>(CColorPalette::PaletteName::kPlasma), choice));
    paramVector.push_back(std::make_unique<AudioParameterBool>(JadeParamID::logFreqAxis, "Log frequency axis", false, boolean));
    paramVector.push_back(std::make_unique<AudioParameterBool>(JadeParamID::fixDisplay, "Fix display", false, boolean));
    paramVector.push_back(std::make_unique<AudioParameterChoice>(JadeParamID::overlap, "Overlap",
        StringArray{"50 %", "75 %"}, 0, choice));
    const auto fraction = AudioParameterFloatAttributes().withAutomatable(false)
        .withStringFromValueFunction([](float v, int) { return String(juce::roundToInt(100.f*v)) + " %"; });
    paramVector.push_back(std::make_unique<AudioParameterFloat>(JadeParamID::timeStart, "Time zoom start",
        NormalisableRange<float>(0.f, 1.f), 0.f, fraction));
    paramVector.push_back(std::make_unique<AudioParameterFloat>(JadeParamID::timeEnd, "Time zoom end",
        NormalisableRange<float>(0.f, 1.f), 1.f, fraction));
    // averaging time constant: 0 ... 2000 ms, centre of the slider at 200 ms; 0 = off
    NormalisableRange<float> avgRange(0.f, 2000.f);
    avgRange.setSkewForCentre(200.f);
    paramVector.push_back(std::make_unique<AudioParameterFloat>(JadeParamID::averaging, "Averaging", avgRange, 0.f,
        AudioParameterFloatAttributes().withAutomatable(false).withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return v < 0.5f ? String("off") : String(juce::roundToInt(v)) + " ms"; })));
}

void JadeSpectrogramAudio::prepareParameter(std::unique_ptr<juce::AudioProcessorValueTreeState> &vts)
{
    m_fftSizeParam = vts->getRawParameterValue(JadeParamID::fftSize);
    m_windowParam = vts->getRawParameterValue(JadeParamID::window);
    m_averagingParam = vts->getRawParameterValue(JadeParamID::averaging);
    m_overlapParam = vts->getRawParameterValue(JadeParamID::overlap);
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
    m_currentHop = m_internalHeight-1; // 50 % overlap until the first slice tells the hop
    m_internalWidth = static_cast<size_t>(g_pastTimeMemLen_s * fs/static_cast<float>(m_currentHop));

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
    // frequency range (log values of MinFreq/MaxFreq, 1 Hz ... 20 kHz) and colour range (dB)
    // slider values in Hz (scale: setFreqSliderScale), the parameters store log(Hz)
    setFreqSliderScale();
    m_freqRangeSlider.setTooltip("Displayed frequency range: drag a thumb, or drag between the thumbs to move the range");
    m_freqRangeBinding = std::make_unique<RangeParameterBinding>(m_freqRangeSlider, m_apvts, paramDisplayMinFreq.ID, paramDisplayMaxFreq.ID,
        [](double hz) { return std::log(hz); }, [](double logHz) { return std::exp(logHz); });
    m_freqRangeBinding->onChange = [this]() { if (somethingChanged != nullptr) somethingChanged(); };
    addAndMakeVisible(m_freqRangeSlider);
    m_colorRangeSlider.setRange(g_minColorVal, g_maxColorVal);
    m_colorRangeSlider.setTooltip("Colour range in dB: drag a thumb, or drag between the thumbs to move the range");
    m_colorRangeBinding = std::make_unique<RangeParameterBinding>(m_colorRangeSlider, m_apvts, paramDisplayMinColor.ID, paramDisplayMaxColor.ID);
    m_colorRangeBinding->onChange = [this]() { if (somethingChanged != nullptr) somethingChanged(); };
    addAndMakeVisible(m_colorRangeSlider);
    // the selected range in light red between the red thumbs (track and background are both
    // grey in the Jade look and feel, which would hide the range)
    m_timeRangeSlider.setRange(0.0, 1.0);
    m_timeRangeSlider.setTooltip("Time zoom: drag a thumb, or drag between the thumbs to move the visible part");
    m_timeRangeBinding = std::make_unique<RangeParameterBinding>(m_timeRangeSlider, m_apvts, JadeParamID::timeStart, JadeParamID::timeEnd);
    addAndMakeVisible(m_timeRangeSlider);
    for (auto* rs : {&m_freqRangeSlider, &m_colorRangeSlider, &m_timeRangeSlider})
        rs->setColour(juce::Slider::trackColourId, JadeLightRed1);

    // pause bars while running (click pauses), play triangle while paused (click continues);
    // highlighted while paused
    m_pauseButton.drawIcon = [this](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour(c);
        if (m_isPaused)
        {
            juce::Path play;
            play.addTriangle(r.getX() + 0.15f*r.getWidth(), r.getY(), r.getX() + 0.15f*r.getWidth(), r.getBottom(),
                             r.getRight() - 0.05f*r.getWidth(), r.getCentreY());
            g.fillPath(play);
        }
        else
        {
            const float barW = 0.3f*r.getWidth();
            g.fillRect(r.getX() + 0.08f*r.getWidth(), r.getY(), barW, r.getHeight());
            g.fillRect(r.getRight() - 0.08f*r.getWidth() - barW, r.getY(), barW, r.getHeight());
        }
    };
    m_pauseButton.setTooltip("Pause / continue the analysis");
    m_pauseButton.setToggleState(false,NotificationType::dontSendNotification);
    m_pauseButton.onClick = [this](){pauseClicked();};
    addAndMakeVisible(m_pauseButton);

    syncFromParameters(); // lin/log and Run/Fix from the saved settings
    setFreqAxisButtonText();
    m_freqAxisButton.onClick = [this](){freqAxisClicked();};
    updateDisplayRange();
    // no addAndMakeVisible here: the editor shows the button above the frequency axis (title bar)
    updateFrequencyMapping();

    m_runModeButton.onClick = [this](){runClicked();};
    setDisplayMode(m_isRunningDisplay); // label for the restored mode (syncFromParameters above)
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
    m_colorSchemeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(m_apvts, JadeParamID::colorMap, m_colorScheme);
    addAndMakeVisible(m_colorScheme);
    m_windowFktCombo.addItem("Rectangular",1);
    m_windowFktCombo.addItem("Hann",2);
    m_windowFktCombo.addItem("Hamming",3);
    m_windowFktCombo.addItem("BlackmanHarris",4);
    m_windowFktCombo.addItem("FlatTop",5);
    m_windowFktCombo.addItem("HannPoisson",6);
    m_windowFktCombo.setColour(juce::ComboBox::ColourIds::backgroundColourId,JadeTeal);
    // window and FFT size: the audio thread reads the parameters (JadeParamID)
    m_windowAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(m_apvts, JadeParamID::window, m_windowFktCombo);
    addAndMakeVisible(m_windowFktCombo);

    m_fftSizeCombo.addItem("512",1);
    m_fftSizeCombo.addItem("1024",2);
    m_fftSizeCombo.addItem("2048",3);
    m_fftSizeCombo.addItem("4096",4);
    m_fftSizeCombo.addItem("8192",5);
    m_fftSizeCombo.setColour(juce::ComboBox::ColourIds::backgroundColourId,JadeTeal);
    m_fftSizeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(m_apvts, JadeParamID::fftSize, m_fftSizeCombo);

    m_overlapCombo.addItem("50 %", 1);
    m_overlapCombo.addItem("75 %", 2);
    m_overlapCombo.setColour(juce::ComboBox::ColourIds::backgroundColourId,JadeTeal);
    m_overlapCombo.setTooltip("Overlap of the analysis blocks (75 %: twice as many columns, smoother time axis)");
    m_overlapAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(m_apvts, JadeParamID::overlap, m_overlapCombo);
    addAndMakeVisible(m_overlapCombo);

    m_averagingLabel.setText("Avg", NotificationType::dontSendNotification);
    m_averagingLabel.setJustificationType(Justification::centredRight);
    addAndMakeVisible(m_averagingLabel);
    m_averagingSlider.setTooltip("Averaging along time (time constant); leftmost position: off");
    m_averagingSlider.setNormalisableRange(makeAveragingRange());
    m_averagingBinding = std::make_unique<SliderParameterBinding>(m_averagingSlider, m_apvts, JadeParamID::averaging);
    // the text shows "off" up to one hop, as the audio thread does
    m_averagingSlider.textFromValueFunction = [this](double v)
    {
        const double hopMs = 1000.0*static_cast<double>(m_currentHop)/static_cast<double>(m_processor.m_algo.getSamplerate());
        return v <= hopMs ? String("off") : String(juce::roundToInt(v)) + " ms";
    };
    m_averagingSlider.valueFromTextFunction = [](const String& t) { return t.trimStart().startsWithIgnoreCase("off") ? 0.0 : t.getDoubleValue(); };
    m_averagingSlider.setColour(juce::Slider::trackColourId, JadeLightRed1);
    m_averagingSlider.updateText();
    addAndMakeVisible(m_averagingSlider);
    addAndMakeVisible(m_fftSizeCombo);

    startTimer(40) ;
}

void JadeSpectrogramGUI::paint(juce::Graphics &g)
{

    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker(0.2f));

    int w = getWidth();

    float fs = m_processor.m_algo.getSamplerate();
    
    // the frequency range (m_minDisplayFreq, m_maxDisplayFreq) is set in timerCallback together with
    // the image (updateDisplayRange), so image, axis and readout always belong together


    const auto display = displayArea();
    const int wStartPic = display.getX();
    const int top = display.getY();
    const int displayW = display.getWidth();
    const float displayH = static_cast<float>(display.getHeight());

    int TextHeight = 20;
    // time zoom: image columns m_timeStart*W ... m_timeEnd*W fill the display width
    const float colStart = m_timeStart*static_cast<float>(m_internalWidth);
    const float sx = static_cast<float>(displayW)/((m_timeEnd - m_timeStart)*static_cast<float>(m_internalWidth));
    if (m_axisMap != AxisMap::Bins) // the image covers exactly the displayed frequency range
    {
        const float sy = displayH/static_cast<float>(m_imageRows);
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(display);
        g.drawImageTransformed(m_internalImg, juce::AffineTransform::scale(sx, sy)
                                                  .translated(static_cast<float>(wStartPic) - colStart*sx, static_cast<float>(top)));
    }
    else
    {
    // Linear axis: image row H-1-k shows bin k (frequency k*binWidth); the centre of that row has to
    // land exactly on the axis position of k*binWidth. A transform instead of an integer source
    // rectangle, so neither the half-row offset nor rounding to whole rows shifts the bins.
        const float binWidth = 0.5f*fs/static_cast<float>(m_internalHeight-1);
        const float rowsShown = (m_maxDisplayFreq - m_minDisplayFreq)/binWidth; // image rows between min and max
        const float rowTop = static_cast<float>(m_internalHeight) - 0.5f - m_maxDisplayFreq/binWidth; // image coordinate of max
        const float sy = displayH/rowsShown;
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(display);
        g.drawImageTransformed(m_internalImg, juce::AffineTransform::scale(sx, sy)
                                                  .translated(static_cast<float>(wStartPic) - colStart*sx, static_cast<float>(top) - rowTop*sy));
    }
    drawFrequencyAxis(g, static_cast<int>(static_cast<float>(wStartPic)-g_FreqMeter*m_scaleFactor), top, displayH,
                      m_scaleFactor*static_cast<float>(TextHeight));
    drawTimeAxis(g, display, m_scaleFactor*static_cast<float>(TextHeight));

    // Plot Colorbar (same height as the display)
    int cbHeight = display.getHeight();
    Image colorbar(Image::RGB,1,cbHeight,true);
    for (int kk = 0; kk < cbHeight; kk++)
    {   
        float val = float(kk)/cbHeight*(g_maxColorVal - g_minColorVal) + g_minColorVal;
        juce::uint32 color = m_colorpalette.getRGBColor(val) | 0xFF000000u; // kein alpha blending
        colorbar.setPixelAt(0,cbHeight-1-kk,juce::Colour(color));
    }
    g.drawImage(colorbar,w-static_cast<int>(m_scaleFactor*(g_colorbar_width+g_FreqMeter + g_SliderWidth)),top,static_cast<int>(m_scaleFactor*g_colorbar_width),cbHeight,
                0,0,1,cbHeight);

    // draw scale
    // Add colorbar scale
    const int nrOfYTicks = 11;
    const float RangePerTick = float(g_maxColorVal - g_minColorVal)/(nrOfYTicks-1);
    const float bottom = static_cast<float>(display.getBottom());
    for (auto kk = 0; kk < nrOfYTicks; ++kk)
    {
        float newExaktFreq = int((g_minColorVal + RangePerTick*kk)*0.1f)*10.f;
        String OutText;
        OutText += String(newExaktFreq);

        float ydelta = displayH*(newExaktFreq-g_minColorVal)/(g_maxColorVal - g_minColorVal);
        int x = static_cast<int>(static_cast<float>(w) - (g_FreqMeter + g_SliderWidth + g_SliderMaxFreq_x)*m_scaleFactor);
        int y ;
        if (kk < nrOfYTicks-1)
            y = static_cast<int>(bottom - 0.5f*static_cast<float>(TextHeight)*m_scaleFactor - ydelta);
        else
        {
            y = static_cast<int>(bottom - ydelta);
        }
        
        g.drawText(OutText,x,y,static_cast<int>(g_FreqMeter*m_scaleFactor),static_cast<int>(m_scaleFactor*TextHeight),
                juce::Justification::centred,true);
    }
    
    m_newDataAvailable = 0;

    drawCrosshair(g, display);

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
    const auto display = displayArea();
    const float sf = m_scaleFactor;
    auto sc = [sf](float v) { return static_cast<int>(sf*v); };
    const int top = display.getY();
    // range sliders along the full display height
    m_freqRangeSlider.setBounds(sc(g_SliderMinFreq_x), top, sc(g_SliderWidth), display.getHeight());
    m_colorRangeSlider.setBounds(width - sc(g_SliderWidth + g_SliderMinFreq_x), top, sc(g_SliderWidth), display.getHeight());
    m_timeRangeSlider.setBounds(display.getX(), 0, display.getWidth(), top); // zoom strip, as wide as the display

    // bottom row, left to right below the display: pause, run/fix, window, FFT size
    // (overlap and averaging follow in later versions); colour map below the colour bar
    const int rowY = static_cast<int>(static_cast<float>(h) - sf*g_menuHeight + 0.5f);
    const int rowH = sc(g_ButtonHeight);
    int x = display.getX();
    m_pauseButton.setBounds(x, rowY, sc(g_PauseButtonWidth), rowH);
    x += sc(g_PauseButtonWidth + 6);
    m_runModeButton.setBounds(x, rowY, sc(40), rowH);
    x += sc(40 + 12);
    m_windowFktCombo.setBounds(x, rowY, sc(110), rowH);
    x += sc(110 + 8);
    m_overlapCombo.setBounds(x, rowY, sc(62), rowH);
    x += sc(62 + 8);
    m_fftSizeCombo.setBounds(x, rowY, sc(70), rowH);
    x += sc(70 + 12);
    m_averagingLabel.setBounds(x, rowY, sc(30), rowH);
    x += sc(32);
    m_averagingSlider.setTextBoxStyle(Slider::TextBoxRight, false, sc(56), rowH);
    m_averagingSlider.setBounds(x, rowY, sc(170), rowH);

    m_colorScheme.setBounds(width - sc(g_colorbar_width + g_FreqMeter + g_SliderWidth), rowY, sc(g_colorbar_width), rowH);
}

void JadeSpectrogramGUI::timerCallback()
{
    // parameters -> range sliders (restored project, preset, automation)
    m_freqRangeBinding->update();
    m_colorRangeBinding->update();
    m_timeRangeBinding->update();
    m_averagingBinding->update();
    updateTimeRange();
    const float minValColor = m_apvts.getRawParameterValue(paramDisplayMinColor.ID)->load();
    const float maxValColor = m_apvts.getRawParameterValue(paramDisplayMaxColor.ID)->load();
    if (!juce::approximatelyEqual(minValColor, m_lastColorMin) || !juce::approximatelyEqual(maxValColor, m_lastColorMax))
    {
        m_lastColorMin = minValColor; m_lastColorMax = maxValColor;
        m_recomputeAll = true; // all pixels get new colours
    }

    m_colorpalette.setValueRange(minValColor,maxValColor);
    syncFromParameters();     // lin/log, Run/Fix (restored project, preset)
    updateDisplayRange();     // frequency sliders -> displayed range
    updateFrequencyMapping(); // image layout for this range (LinearMax, Log)

    bool stilldataavailable;
    do
    {
        // the size travels with each slice, so a new FFT size shows up exactly with its first slice
        size_t actSpectrumSize = m_processor.m_algo.getNextMemSliceSize();
        if (actSpectrumSize == 0) // nothing new
            break;
        size_t hop = m_processor.m_algo.getNextMemSliceInfo().hop;
        if (hop == 0)
            hop = actSpectrumSize-1;
        if (actSpectrumSize != m_internalHeight || hop != m_currentHop) // new FFT size or overlap
        {
            m_internalHeight = actSpectrumSize;
            m_currentHop = hop;
            float fs = m_processor.m_algo.getSamplerate();
            m_internalWidth = static_cast<size_t>(g_pastTimeMemLen_s * fs/static_cast<float>(m_currentHop)); // one column per hop

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
            m_averagingSlider.updateText(); // "off" limit and scale follow the hop
            m_averagingSlider.repaint();
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
    setBoolParameter(JadeParamID::fixDisplay, !m_isRunningDisplay);
    syncFromParameters();
}

void JadeSpectrogramGUI::setDisplayMode(bool fixed)
{
    m_isRunningDisplay = fixed;
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
    m_recomputeAll = true; // the columns are arranged differently in both modes
}

void JadeSpectrogramGUI::updateTimeRange()
{
    // visible fraction of the time window; at least 2 % (0.2 s of 10 s) so it cannot collapse
    const float minWidth = 0.02f;
    float s = juce::jlimit(0.f, 1.f, m_apvts.getRawParameterValue(JadeParamID::timeStart)->load());
    float e = juce::jlimit(0.f, 1.f, m_apvts.getRawParameterValue(JadeParamID::timeEnd)->load());
    if (e - s < minWidth)
    {
        if (s + minWidth <= 1.f) e = s + minWidth; else s = e - minWidth;
        setFloatParameter(JadeParamID::timeStart, s);
        setFloatParameter(JadeParamID::timeEnd, e);
    }
    m_timeStart = s;
    m_timeEnd = e;
}

void JadeSpectrogramGUI::setFloatParameter(const juce::String& id, float plainValue)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(m_apvts.getParameter(id)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
        p->endChangeGesture();
    }
}

void JadeSpectrogramGUI::setBoolParameter(const juce::String& id, bool value)
{
    if (auto* p = m_apvts.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(value ? 1.f : 0.f);
        p->endChangeGesture();
    }
}

void JadeSpectrogramGUI::syncFromParameters()
{
    // message thread; the values change by the buttons, a restored project or a preset
    const bool logAxis = m_apvts.getRawParameterValue(JadeParamID::logFreqAxis)->load() > 0.5f;
    if (logAxis != m_logFreqAxis)
    {
        m_logFreqAxis = logAxis;
        setFreqAxisButtonText();
    }
    const bool fixed = m_apvts.getRawParameterValue(JadeParamID::fixDisplay)->load() > 0.5f;
    if (fixed != m_isRunningDisplay)
        setDisplayMode(fixed);
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
    setFreqSliderScale(); // the frequency slider follows the axis
}

void JadeSpectrogramGUI::setFreqSliderScale()
{
    // linear axis: 1 Hz ... 20 kHz linear in Hz; log axis: 20 Hz (lowest log frequency) ... 20 kHz log
    if (m_logFreqAxis)
        m_freqRangeSlider.setNormalisableRange(juce::NormalisableRange<double>(g_logAxisMinFreq, 20000.0,
            [](double start, double end, double t) { return start*std::pow(end/start, t); },
            [](double start, double end, double v) { return std::log(v/start)/std::log(end/start); }));
    else
        m_freqRangeSlider.setNormalisableRange(juce::NormalisableRange<double>(1.0, 20000.0));
    if (m_freqRangeBinding != nullptr)
        m_freqRangeBinding->update();
    m_freqRangeSlider.repaint();
}

juce::NormalisableRange<double> JadeSpectrogramGUI::makeAveragingRange()
{
    // lowest 2 % of the slider: off; above: 2 hops (alpha = 0.5) ... 2000 ms, logarithmic.
    // The hop depends on sample rate, FFT size and overlap, so the scale follows m_currentHop.
    auto hopMs = [this]() { return 1000.0*static_cast<double>(m_currentHop)/static_cast<double>(m_processor.m_algo.getSamplerate()); };
    const double offPart = 0.02;
    return juce::NormalisableRange<double>(0.0, 2000.0,
        [hopMs, offPart](double, double end, double p)
        {
            if (p < offPart)
                return 0.0;
            const double tauMin = std::min(2.0*hopMs(), 0.5*end);
            return tauMin*std::pow(end/tauMin, (p - offPart)/(1.0 - offPart));
        },
        [hopMs, offPart](double, double end, double v)
        {
            const double hop = hopMs(), tauMin = std::min(2.0*hop, 0.5*end);
            if (v <= hop)
                return 0.0; // off (alpha = 1)
            if (v <= tauMin)
                return offPart;
            return offPart + (1.0 - offPart)*std::log(v/tauMin)/std::log(end/tauMin);
        },
        [](double, double, double v) { return v; });
}

void JadeSpectrogramGUI::freqAxisClicked()
{
    setBoolParameter(JadeParamID::logFreqAxis, !m_logFreqAxis);
    syncFromParameters();
    timerCallback(); // new mapping and image at once (also repaints)
}

void JadeSpectrogramGUI::updateDisplayRange()
{
    const float fs = m_processor.m_algo.getSamplerate();
    m_minDisplayFreq = std::exp(m_apvts.getRawParameterValue(paramDisplayMinFreq.ID)->load());
    m_maxDisplayFreq = std::exp(m_apvts.getRawParameterValue(paramDisplayMaxFreq.ID)->load());

    if (m_minDisplayFreq >= fs*0.5f)
        m_minDisplayFreq = 0.9f*fs*0.5f;
    if (m_maxDisplayFreq >= fs*0.5f)
        m_maxDisplayFreq = fs*0.5f;

    if (1.1f*m_minDisplayFreq >= m_maxDisplayFreq)
    {
        //m_minDisplayFreq = 0.8f*m_maxDisplayFreq;
        m_maxDisplayFreq = 1.1f*m_minDisplayFreq;
        setFloatParameter(paramDisplayMaxFreq.ID, std::log(1.1f*m_minDisplayFreq));
        setFloatParameter(paramDisplayMinFreq.ID, std::log(m_minDisplayFreq));
    }
}

juce::Rectangle<int> JadeSpectrogramGUI::displayArea() const
{
    // left: frequency slider and labels; right: gap, colour bar, colour labels, colour slider;
    // top: zoom strip; bottom: time axis labels and the bottom row
    const float s = m_scaleFactor;
    const int x = static_cast<int>(s*(g_SliderWidth + g_FreqMeter));
    const int right = getWidth() - static_cast<int>(s*(g_colorbar_width + g_FreqMeter + g_SliderWidth + g_displayGap));
    const int top = static_cast<int>(s*g_zoomStripHeight + 0.5f);
    const int bottom = static_cast<int>(float(getHeight()) - s*(g_menuHeight + g_timeAxisHeight) + 0.5f);
    return {x, top, juce::jmax(1, right - x), juce::jmax(1, bottom - top)};
}

float JadeSpectrogramGUI::displayHeight() const
{
    return static_cast<float>(displayArea().getHeight());
}

float JadeSpectrogramGUI::timeSpan() const
{
    // one display column per hop (the hop travels with the slices)
    const float fs = m_processor.m_algo.getSamplerate();
    return static_cast<float>(m_internalWidth)*static_cast<float>(m_currentHop)/fs;
}

void JadeSpectrogramGUI::drawTimeAxis(juce::Graphics& g, juce::Rectangle<int> display, float textH) const
{
    // Scroll mode: time relative to now (-span ... 0 s at the right edge).
    // Fix mode: sweep time (0 s at the left edge ... span), the running cursor shows "now".
    const float span = timeSpan();
    if (!(span > 0.f))
        return;
    const float t0 = m_isRunningDisplay ? 0.f : -span; // time at the left edge of the whole window
    const float tmin = t0 + m_timeStart*span;         // visible part (time zoom)
    const float tmax = t0 + m_timeEnd*span;
    const float labelW = 44.f*m_scaleFactor;
    const int maxLabels = juce::jmax(2, static_cast<int>(static_cast<float>(display.getWidth())/(1.3f*labelW)));
    double step = 1.0;
    for (double st : {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0})
    {
        step = st;
        if (static_cast<int>(std::floor(double(tmax - tmin)/st)) + 1 <= maxLabels)
            break;
    }
    const int decimals = (step < 1.0) ? 1 : 0;
    const float y0 = static_cast<float>(display.getBottom());
    const float tick = 4.f*m_scaleFactor;
    g.setFont(0.7f*textH);
    for (double t = std::ceil(tmin/step - 1e-6)*step; t <= double(tmax) + 1e-6; t += step)
    {
        const float x = static_cast<float>(display.getX()) + static_cast<float>(display.getWidth())*(static_cast<float>(t) - tmin)/(tmax - tmin);
        g.drawLine(x, y0, x, y0 + tick, 1.f);
        const double shown = (std::abs(t) < 1e-9) ? 0.0 : t; // no "-0 s"
        const String text = String(shown, decimals) + " s";
        const float tx = juce::jlimit(static_cast<float>(display.getX()) - 0.5f*labelW, static_cast<float>(display.getRight()) - 0.5f*labelW,
                                      x - 0.5f*labelW);
        g.drawText(text, static_cast<int>(tx), static_cast<int>(y0 + tick), static_cast<int>(labelW),
                   static_cast<int>(g_timeAxisHeight*m_scaleFactor - tick), juce::Justification::centred, false);
    }
}

float JadeSpectrogramGUI::frequencyToY(float freq, float displayH) const
{
    if (m_axisMap == AxisMap::Log)
        return displayH*(1.f - std::log(freq/m_mapMinFreq)/std::log(m_mapMaxFreq/m_mapMinFreq));
    const float fmin = (m_axisMap == AxisMap::LinearMax) ? m_mapMinFreq : m_minDisplayFreq;
    const float fmax = (m_axisMap == AxisMap::LinearMax) ? m_mapMaxFreq : m_maxDisplayFreq;
    return displayH*(fmax - freq)/(fmax - fmin);
}

float JadeSpectrogramGUI::yToFrequency(float y, float displayH) const
{
    const float yrel = 1.f - y/displayH; // 0 bottom ... 1 top
    if (m_axisMap == AxisMap::Log)
        return m_mapMinFreq * std::pow(m_mapMaxFreq/m_mapMinFreq, yrel);
    const float fmin = (m_axisMap == AxisMap::LinearMax) ? m_mapMinFreq : m_minDisplayFreq;
    const float fmax = (m_axisMap == AxisMap::LinearMax) ? m_mapMaxFreq : m_maxDisplayFreq;
    return fmin + yrel*(fmax - fmin);
}

void JadeSpectrogramGUI::drawFrequencyAxis(juce::Graphics& g, int x, int top, float displayH, float textH) const
{
    const float fmin = (m_axisMap == AxisMap::Bins) ? m_minDisplayFreq : m_mapMinFreq;
    const float fmax = (m_axisMap == AxisMap::Bins) ? m_maxDisplayFreq : m_mapMaxFreq;
    if (!(fmax > fmin) || displayH <= textH)
        return;
    const int maxLabels = juce::jmax(2, static_cast<int>(displayH/(1.4f*textH)));

    // candidates: round steps, the smallest one whose labels still fit (never an empty axis)
    std::vector<double> ticks;
    double step = 0.0;
    auto linearTicks = [&]() {
        for (double s : {10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
        {
            ticks.clear();
            for (double f = std::ceil(fmin/s)*s; f <= fmax*1.0001; f += s)
                ticks.push_back(f);
            step = s;
            if (static_cast<int>(ticks.size()) <= maxLabels)
                break;
        }
    };
    if (m_axisMap == AxisMap::Log)
    {
        // 1-2-5 per decade; a strongly zoomed range (fewer than 3 of them) gets round linear steps
        for (double decade = 10.0; decade <= 20000.0; decade *= 10.0)
            for (double mult : {1.0, 2.0, 5.0})
                if (decade*mult >= fmin*0.999 && decade*mult <= fmax*1.001)
                    ticks.push_back(decade*mult);
        if (ticks.size() < 3)
            linearTicks();
    }
    else
        linearTicks();

    // labels: whole Hz below 1 kHz (steps >= 10 Hz: last digit 0), kHz above, with as many
    // decimals as the step needs (1.5k, 1.02k); whole kHz without decimals (1k, 10k)
    int kHzDecimals = 0;
    if (step > 0.0 && step < 1000.0)
        kHzDecimals = (step >= 100.0) ? 1 : 2;
    g.setFont(0.8f*textH);
    for (double f : ticks)
    {
        String text;
        if (f >= 1000.0)
        {
            const bool wholeKHz = std::abs(f/1000.0 - std::round(f/1000.0)) < 1e-6;
            text = (kHzDecimals == 0 || wholeKHz) ? String(juce::roundToInt(f/1000.0)) + "k" : String(f/1000.0, kHzDecimals) + "k";
        }
        else
            text = String(juce::roundToInt(f));
        const float y = static_cast<float>(top) + juce::jlimit(0.f, displayH-textH, frequencyToY(static_cast<float>(f), displayH) - 0.5f*textH);
        g.drawText(text, x, static_cast<int>(y), static_cast<int>(g_FreqMeter*m_scaleFactor), static_cast<int>(textH),
                   juce::Justification::centred, true);
    }
}

void JadeSpectrogramGUI::updateFrequencyMapping()
{
    const size_t bins = m_internalHeight;
    const float fs = m_processor.m_algo.getSamplerate();
    const float displayH = displayHeight();
    AxisMap kind = AxisMap::Bins;
    float fmin = m_minDisplayFreq, fmax = m_maxDisplayFreq;
    size_t rows = bins;
    if (bins >= 2 && m_logFreqAxis)
    {
        kind = AxisMap::Log;
        // displayed range: the frequency sliders, but at least g_logAxisMinFreq at the bottom
        fmin = std::min(std::max(m_minDisplayFreq, g_logAxisMinFreq), fmax/1.1f);
        rows = g_logAxisRows;
    }
    else if (bins >= 2 && displayH >= 2.f && (fmax - fmin)/(0.5f*fs/float(bins-1)) > displayH)
    {
        kind = AxisMap::LinearMax; // more bins than pixel rows in the visible range
        rows = static_cast<size_t>(displayH);
    }

    if (kind == AxisMap::Bins)
    {
        if (m_axisMap != AxisMap::Bins || m_imageRows != bins)
        {
            m_axisMap = AxisMap::Bins;
            m_rowMap.clear();
            m_imageRows = bins;
            m_recomputeAll = true;
        }
    }
    else
    {
        const bool upToDate = m_axisMap == kind && m_rowMap.size() == rows && m_mapBins == bins
            && std::abs(m_mapMinFreq - fmin) < 1e-3f && std::abs(m_mapMaxFreq - fmax) < 1e-3f
            && std::abs(m_mapFs - fs) < 1e-3f;
        if (!upToDate)
        {
            m_axisMap = kind;
            m_mapMinFreq = fmin; m_mapMaxFreq = fmax; m_mapBins = bins; m_mapFs = fs;
            m_imageRows = rows;
            m_rowMap.resize(rows);
            const double binWidth = 0.5*double(fs)/double(bins-1); // Hz per bin
            const double ratio = double(fmax)/double(fmin);
            // lower edge of row r (r = 0: bottom row), t = r/rows
            auto edge = [&](double t) { return (kind == AxisMap::Log) ? fmin*std::pow(ratio, t) : fmin + t*(fmax - fmin); };
            for (size_t r = 0; r < rows; ++r)
            {
                const double flo = edge(double(r)/double(rows));
                const double fhi = edge(double(r+1)/double(rows));
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
                    // row centre: geometric for log rows, arithmetic for linear rows
                    const double bc = ((kind == AxisMap::Log) ? std::sqrt(flo*fhi) : 0.5*(flo + fhi))/binWidth;
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
    if (m_axisMap == AxisMap::Bins) // row = bin
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
    const juce::String text = m_readoutText;
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
    const auto display = displayArea();
    if (display.contains(x, y))
    {
        const float displayH = static_cast<float>(display.getHeight());
        const float yd = static_cast<float>(y - display.getY()); // 0 = top of the display
        const float freq = yToFrequency(yd, displayH);
        // mapped image (LinearMax, Log): the readout shows the value of the image row, i.e. what is drawn
        const size_t imageRow = std::min(m_imageRows-1, static_cast<size_t>(juce::jmax(0.f, (1.f - (yd+0.5f)/displayH)*float(m_imageRows))));
        // recompute freq to freq index in the internal memory
        float fshalf = 0.5f*m_processor.m_algo.getSamplerate();
        size_t freqindex  = static_cast<size_t> ((m_internalHeight-1) * freq / fshalf + 0.5f);
        freqindex = std::min(freqindex, m_internalHeight-1);

        // recompute the time index from display index
        // image column under the pixel centre (the same mapping as in paint(), including the time zoom)
        const float frac = m_timeStart + (m_timeEnd - m_timeStart)*(static_cast<float>(x - display.getX()) + 0.5f)/static_cast<float>(display.getWidth());
        size_t timeindex = std::min(m_internalWidth-1, static_cast<size_t>(juce::jmax(0.f, frac*static_cast<float>(m_internalWidth))));
        float val = -100.f;
        size_t column = timeindex;
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
            column = memindex;
        }
        if (m_axisMap != AxisMap::Bins)
            val = rowValue(m_displaymem.at(column), imageRow);

        MidiMessage msg;
        int midinotenumber =  int(log(freq/440.0)/log(2) * 12 + 69 + 0.5);
        String midiNoteName = msg.getMidiNoteName(midinotenumber,true,true,4);

        m_readoutText = String(int(freq+0.5)) + String(" Hz | ") + midiNoteName + String(" | ") + String(0.1*int(val*10+0.5),1) + String(" dB");
        return true;
    }
    return false;
}

