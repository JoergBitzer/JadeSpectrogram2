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


// FFT sizes selectable at runtime: 2^9 = 512 ... 2^13 = 8192
constexpr size_t g_minFFTSizeLog2 = 9;
constexpr size_t g_nrOfFFTSizes = 5;
constexpr size_t g_maxFFTSize = size_t(1) << (g_minFFTSizeLog2 + g_nrOfFFTSizes - 1);

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
    void setPauseMode (bool mode){m_PauseMode = mode;};
    
    size_t getnextpowerof2(float fftsize_ms);

    size_t getSpectrumSize(){return m_publishedFreqSize.load();}; // spectrum size currently produced by the audio thread
    float getSamplerate(){return m_fs;};
	void setWindowType(SpectrumAnalyzer::WindowType type){m_requestedWindow.store(type);};
	void setChannelMixMode(ChannelMixMode mode){m_mixMode = mode;};
	// GUI side of the FIFO: size of the next slice (0: nothing to read) and the slice itself
	size_t getNextMemSliceSize() const { return m_fifo.getNextSliceSize(); };
	bool getMemSlice(std::vector<float>& outBlock){ return m_fifo.pop(outBlock); };

private:
	JadeSpectrogramAudioProcessor* m_processor;
    std::atomic<int> m_Latency {0}; // written by the audio thread on an FFT size switch
    float m_fs;
    size_t m_channels;

	size_t m_fftsize; // active FFT size, audio thread only
	size_t m_freqsize; // active spectrum size, audio thread only
	std::atomic<size_t> m_publishedFreqSize {2048/2 + 1}; // copy of m_freqsize for the GUI
	std::atomic<size_t> m_requestedFFTSize {2048};
	std::atomic<SpectrumAnalyzer::WindowType> m_requestedWindow {SpectrumAnalyzer::WindowType::Hann};

	// one prepared analyzer pair per FFT size, so switching the size never allocates
	std::array<SpectrumAnalyzer, g_nrOfFFTSizes> m_leftAnalyzers;
	std::array<SpectrumAnalyzer, g_nrOfFFTSizes> m_rightAnalyzers;
	size_t m_activeAnalyzer = 0;
	TwoDimBlockFreeFiFO m_fifo;
	std::vector<float> m_power;
	std::vector<float> m_perLeft;
	std::vector<float> m_perRight;
	std::vector<float> m_timeInLeft;
	std::vector<float> m_timeInRight;
    ChannelMixMode m_mixMode;
	SpectrumAnalyzer::WindowType m_windowChoice; // active window, audio thread only

	void applyPendingChanges(); // audio thread, realtime safe
	void switchFFTSize(size_t newFFTSize); // audio thread, realtime safe

	// paramater
	jade::AudioProcessParameter<float> m_DisplayMinFreq;
	jade::AudioProcessParameter<float> m_DisplayMaxFreq;
	jade::AudioProcessParameter<float> m_DisplayMinColor;
	jade::AudioProcessParameter<float> m_DisplayMaxColor;

    bool m_PauseMode;	
};

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

    Label m_DisplayMinFreqLabel;
    Slider m_DisplayMinFreqSlider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> m_DisplayMinFreqAttachment;

    Label m_DisplayMaxFreqLabel;
    Slider m_DisplayMaxFreqSlider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> m_DisplayMaxFreqAttachment;

    Label m_DisplayMinColorLabel;
    Slider m_DisplayMinColorSlider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> m_DisplayMinColorAttachment;

    Label m_DisplayMaxColorLabel;
    Slider m_DisplayMaxColorSlider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> m_DisplayMaxColorAttachment;

    TextButton m_runModeButton;
    TextButton m_pauseButton;
    void pauseClicked();
    void changeFFTSize();
    void mouseMove(const MouseEvent &event);
    void setLabelText(int x, int y);
    void runClicked();
    bool m_isPaused;
    bool m_isRunningDisplay;

    ComboBox m_colorScheme;
    ComboBox m_windowFktCombo;
    
    //JadeSpectrogramAudioProcessorEditor& m_editor;
    ComboBox m_fftSizeCombo;
    bool m_hideFFTSizeCombobox;
    //void changeFFTSize();
    Label m_FreqLabel;

};
