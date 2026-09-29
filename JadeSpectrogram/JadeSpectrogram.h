#pragma once

#include <vector>
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "tools/AudioProcessParameter.h"
#include "tools/SynchronBlockProcessor.h"
#include "PluginSettings.h"

#include "SpectrumAnalyzer.h"
#include "TwoDimBlockFreeFiFo.h"
#include "CColorpalette.h"
#include "IconButton.h"
#include "RangeSlider.h"
#include "JadeLookAndFeel.h"

class JadeSpectrogramAudioProcessor;

const struct
{
	const std::string ID = "MinFreq";
	std::string name = "MinFreq";
	std::string unitName = "Hz";
	float minValue = log(1.f);
	float maxValue = log(10000.f);
	float defaultValue = log(1.f);
}paramDisplayMinFreq;
const struct
{
	const std::string ID = "MaxFreq";
	std::string name = "MaxFreq";
	std::string unitName = "Hz";
	float minValue = log(500.f);
	float maxValue = log(20000.f);
	float defaultValue = log(20000.f);
}paramDisplayMaxFreq;

const struct
{
	const std::string ID = "MinColor";
	std::string name = "MinColor";
	std::string unitName = "";
	float minValue = g_minColorVal;
	float maxValue = g_maxColorVal;
	float defaultValue = g_minColorVal;
}paramDisplayMinColor;
const struct
{
	const std::string ID = "MaxColor";
	std::string name = "MaxColor";
	std::string unitName = "";
	float minValue = g_minColorVal;
	float maxValue = g_maxColorVal;
	float defaultValue =g_maxColorVal;
}paramDisplayMaxColor;


// Display settings, saved with the project as non-automatable parameters (decision 9 of the
// V2 plan). Choice indices: FFT size 512 << index, window = SpectrumAnalyzer::WindowType,
// colour map = CColorPalette::PaletteName.
namespace JadeParamID
{
    inline const juce::String fftSize {"FFTSize"};
    inline const juce::String window {"Window"};
    inline const juce::String colorMap {"ColorMap"};
    inline const juce::String logFreqAxis {"LogFreqAxis"};
    inline const juce::String fixDisplay {"FixDisplay"};
    inline const juce::String averaging {"Averaging"}; // time constant in ms, 0 = off
    inline const juce::String overlap {"Overlap"}; // 0: 50 %, 1: 75 %
    inline const juce::String timeStart {"TimeStart"}; // visible part of the time window,
    inline const juce::String timeEnd {"TimeEnd"};     // fractions 0 ... 1 (1 = right edge)
    inline const juce::String keyboardOverlay {"KeyboardOverlay"}; // piano-roll bands over the spectrogram
}

// FFT sizes selectable at runtime: 2^9 = 512 ... 2^13 = 8192
constexpr size_t g_minFFTSizeLog2 = 9;
constexpr size_t g_nrOfFFTSizes = 5;
constexpr size_t g_maxFFTSize = size_t(1) << (g_minFFTSizeLog2 + g_nrOfFFTSizes - 1);
constexpr size_t g_nrOfOverlaps = 2; // 0: 50 % (hop = FFT size / 2), 1: 75 % (hop = FFT size / 4)

class JadeSpectrogramAudio : public SynchronBlockProcessor
{
public:
    enum class ChannelMixMode
    {
        TimeMean, // mixing in the time domain by calculating the mean of the channels for each sample
		AbsMean, // all following are working in the time domain
        Max,
        Min,
        Left,
        Right
    };
    JadeSpectrogramAudio(JadeSpectrogramAudioProcessor* processor);
    void prepareToPlay(double sampleRate, int max_samplesPerBlock, int max_channels);
    // hides SynchronBlockProcessor::processBlock: applies pending FFT size / window changes first
    void processBlock(juce::AudioBuffer<float>& data, juce::MidiBuffer& midiMessages);
    virtual int processSynchronBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midiMessages, int NrOfBlocksSinceLastProcessBlock);

    // parameter handling
  	void addParameter(std::vector < std::unique_ptr<juce::RangedAudioParameter>>& paramVector);
    void prepareParameter(std::unique_ptr<juce::AudioProcessorValueTreeState>&  vts);
    
    // some necessary info for the host
    int getLatency(){return m_Latency.load();}; // delay of the audio pass-through in samples (0 in Analyze mode)

	// setFFTSize and setWindowType can be called from any thread (GUI): they only post a request,
	// the audio thread applies it at the start of its next processBlock
	void setFFTSize(size_t newFFTSize){m_requestedFFTSize.store(newFFTSize);};
	size_t getFFTSize() const {return m_requestedFFTSize.load();};
    void setclosestFFTSize_ms(float fftsize_ms);
    void setPauseMode (bool mode){m_PauseMode.store(mode);};
    
    size_t getnextpowerof2(float fftsize_ms);

    size_t getSpectrumSize(){return m_publishedFreqSize.load();}; // spectrum size currently produced by the audio thread
    float getSamplerate(){return m_fs.load();};
	void setWindowType(SpectrumAnalyzer::WindowType type){m_requestedWindow.store(type);};
	void setChannelMixMode(ChannelMixMode mode){m_mixMode.store(mode);};
	// averaging time constant in ms (tests without parameters; otherwise the Averaging parameter)
	void setAveragingMs(float tauMs){m_averagingMs.store(tauMs);};
	void setOverlap(size_t overlapIndex){m_requestedOverlap.store(std::min(overlapIndex, g_nrOfOverlaps-1));}; // tests
	// GUI side of the FIFO: size of the next slice (0: nothing to read) and the slice itself
	size_t getNextMemSliceSize() const { return m_fifo.getNextSliceSize(); };
	SliceInfo getNextMemSliceInfo() const { return m_fifo.getNextSliceInfo(); };
	bool getMemSlice(std::vector<float>& outBlock){ return m_fifo.pop(outBlock); };

private:
	JadeSpectrogramAudioProcessor* m_processor;
    std::atomic<int> m_Latency {0}; // written by the audio thread on an FFT size switch
    std::atomic<float> m_fs; // written in prepareToPlay, read by the GUI
    size_t m_channels;

	size_t m_fftsize; // active FFT size, audio thread only
	size_t m_freqsize; // active spectrum size, audio thread only
	std::atomic<size_t> m_publishedFreqSize {2048/2 + 1}; // copy of m_freqsize for the GUI
	std::atomic<size_t> m_requestedFFTSize {2048};
	std::atomic<SpectrumAnalyzer::WindowType> m_requestedWindow {SpectrumAnalyzer::WindowType::Hann};

	// one prepared analyzer pair per FFT size, so switching the size never allocates
	// [overlap][FFT size]
	std::array<std::array<SpectrumAnalyzer, g_nrOfFFTSizes>, g_nrOfOverlaps> m_leftAnalyzers;
	std::array<std::array<SpectrumAnalyzer, g_nrOfFFTSizes>, g_nrOfOverlaps> m_rightAnalyzers;
	size_t m_activeAnalyzer = 0;
	size_t m_activeOverlap = 0;
	std::atomic<size_t> m_requestedOverlap {0};
	TwoDimBlockFreeFiFO m_fifo;
	std::vector<float> m_power;
	std::vector<float> m_perLeft;
	std::vector<float> m_perRight;
	std::vector<float> m_timeInLeft;
	std::vector<float> m_timeInRight;
    std::atomic<ChannelMixMode> m_mixMode; // GUI writes, audio thread reads
	SpectrumAnalyzer::WindowType m_windowChoice; // active window, audio thread only

	void applyPendingChanges(); // audio thread, realtime safe
	// FFT size and window parameters (raw values of the value tree, lock-free); nullptr without
	// prepareParameter (tests), then setFFTSize / setWindowType are used directly
	std::atomic<float>* m_fftSizeParam = nullptr;
	std::atomic<float>* m_windowParam = nullptr;
	std::atomic<float>* m_averagingParam = nullptr;
	std::atomic<float>* m_overlapParam = nullptr;
	std::atomic<float> m_averagingMs {0.f};
	// exponential averaging along time (first order IIR per bin on the power spectrum)
	std::vector<float> m_averagedPower;
	bool m_averagingStarted = false; // false: the next block starts the average
	void switchFFTSize(size_t newFFTSize); // audio thread, realtime safe

	// paramater
	jade::AudioProcessParameter<float> m_DisplayMinFreq;
	jade::AudioProcessParameter<float> m_DisplayMaxFreq;
	jade::AudioProcessParameter<float> m_DisplayMinColor;
	jade::AudioProcessParameter<float> m_DisplayMaxColor;

    std::atomic<bool> m_PauseMode; // GUI writes, audio thread reads
};

// logarithmic frequency axis: lowest displayed frequency and number of image rows
const float g_logAxisMinFreq = 20.f;
const size_t g_logAxisRows = 1024;

class JadeSpectrogramGUI : public juce::Component, public Timer
{
public:
	JadeSpectrogramGUI(JadeSpectrogramAudioProcessor& p, juce::AudioProcessorValueTreeState& apvts);
	~JadeSpectrogramGUI() override {stopTimer();};
	void paint(juce::Graphics& g) override;
	void resized() override;
    void setScaleFactor(float newscale){m_scaleFactor = newscale;};	
    void timerCallback() override;
    std::function<void()> somethingChanged;
    // lin/log switch; the editor places it above the frequency axis (in its title bar)
    juce::Button& getFreqAxisButton() { return m_freqAxisButton; }
    // buttons that the editor shows in its title bar, right of the title image
    juce::Button& getKeyboardButton() { return m_keyboardButton; }    
    //void mouseMove (const MouseEvent& event);    	
private:
	JadeSpectrogramAudioProcessor& m_processor;
    juce::AudioProcessorValueTreeState& m_apvts; 
	float m_scaleFactor = 1.f;

    std::vector<std::vector<float >> m_displaymem;
    size_t m_displaymem_writepos = 0;
    size_t m_newDataAvailable = 0;
    std::vector<float> m_exchangeSpectrum;
    Image m_internalImg;
    Image m_ColorbarImg;
    size_t m_internalWidth;
    size_t m_internalHeight;
    bool m_recomputeAll;


    float m_maxColorVal;
    float m_minColorVal;
    CColorPalette m_colorpalette;

    float m_maxDisplayFreq;
    float m_minDisplayFreq;

// Othe UI Elements

    // range sliders (V2 decision 7): frequency range left, colour range right
    RangeSlider m_freqRangeSlider {true};
    RangeSlider m_colorRangeSlider {true};
    RangeSlider m_timeRangeSlider {false}; // time zoom in the strip above the display
    std::unique_ptr<RangeParameterBinding> m_freqRangeBinding;
    std::unique_ptr<RangeParameterBinding> m_colorRangeBinding;
    std::unique_ptr<RangeParameterBinding> m_timeRangeBinding;
    float m_timeStart = 0.f, m_timeEnd = 1.f; // visible fraction of the time window (from the parameters)
    void updateTimeRange();
    float m_lastColorMin = 0.f, m_lastColorMax = 0.f; // palette range of the current image
    void setFloatParameter(const juce::String& id, float plainValue);

    TextButton m_runModeButton;
    IconButton m_pauseButton; // pause / play symbol
    void pauseClicked();
    void setDisplayMode(bool fixed); // Fix (fixed image, running cursor) or Scroll
    void syncFromParameters(); // lin/log and Run/Fix from the saved parameters
    void setBoolParameter(const juce::String& id, bool value);
    void mouseMove(const MouseEvent &event) override;
    void mouseExit(const MouseEvent &event) override;
    bool setLabelText(int x, int y); // true if (x, y) is inside the analysis display
    void drawCrosshair(juce::Graphics& g, juce::Rectangle<int> display);
    juce::Point<int> m_mousePos;

    // Frequency axis and image layout (m_axisMap):
    //  Bins:      linear axis with at most one bin per screen pixel: image row = FFT bin,
    //             paint() draws the visible range with an exact transform
    //  LinearMax: linear axis with more bins than pixels: one image row per screen pixel,
    //             each the maximum of its bins (narrow lines do not disappear)
    //  Log:       logarithmic axis, g_logAxisRows image rows
    // In LinearMax and Log the image covers exactly the displayed range (m_mapMinFreq ... m_mapMaxFreq).
    TextButton m_freqAxisButton;
    void freqAxisClicked();
    void setFreqAxisButtonText();
    bool m_logFreqAxis = false;
    // keyboard overlay (piano roll): one band per semitone, a quarter tone below to above the note
    IconButton m_keyboardButton;
    bool m_keyboardOverlay = false;
    void drawKeyboardOverlay(juce::Graphics& g, juce::Rectangle<int> display, float textH) const;
    size_t m_imageRows = 1; // height of m_internalImg
    struct RowMap { size_t bin0; size_t bin1; float frac; bool useMax; };
    enum class AxisMap { Bins, LinearMax, Log };
    AxisMap m_axisMap = AxisMap::Bins;
    std::vector<RowMap> m_rowMap; // empty for Bins
    float m_mapMinFreq = 0.f; // frequency range and data the log mapping was built for
    float m_mapMaxFreq = 0.f;
    size_t m_mapBins = 0;
    float m_mapFs = 0.f;
    void updateFrequencyMapping(); // rebuilds the mapping (and resizes the image) if needed
    float rowValue(const std::vector<float>& column, size_t row) const;
    void updateDisplayRange(); // frequency sliders -> m_minDisplayFreq, m_maxDisplayFreq
    juce::Rectangle<int> displayArea() const; // the analysis display in component coordinates
    float displayHeight() const; // height of the analysis display in pixels
    float frequencyToY(float freq, float displayH) const; // 0 = top of the display
    float yToFrequency(float y, float displayH) const;
    void drawFrequencyAxis(juce::Graphics& g, int x, int top, float displayH, float textH) const;
    float timeSpan() const; // seconds covered by the display memory
    void drawTimeAxis(juce::Graphics& g, juce::Rectangle<int> display, float textH) const;
    bool m_mouseInDisplay = false; // crosshair is drawn while the mouse is over the display
    void runClicked();
    bool m_isPaused;
    bool m_isRunningDisplay;

    ComboBox m_colorScheme;
    ComboBox m_windowFktCombo;
    
    //JadeSpectrogramAudioProcessorEditor& m_editor;
    ComboBox m_fftSizeCombo;
    ComboBox m_overlapCombo;
    size_t m_currentHop = 1; // hop of the slices in the display memory (from SliceInfo)
    // after the combo boxes: destroyed before them
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_colorSchemeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_windowAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_fftSizeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_overlapAttachment;
    // averaging along time (bottom row): "Avg" label, slider with the value ("off" / ms)
    Label m_averagingLabel;
    Slider m_averagingSlider {Slider::LinearHorizontal, Slider::TextBoxRight};
    std::unique_ptr<SliderParameterBinding> m_averagingBinding; // own scale: see makeAveragingRange
    juce::NormalisableRange<double> makeAveragingRange();
    void setFreqSliderScale(); // linear or log, as the frequency axis
    bool m_hideFFTSizeCombobox;
    juce::String m_readoutText; // frequency | note | level at the mouse (shown by the crosshair)

};
