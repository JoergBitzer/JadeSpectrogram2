#pragma once

#include <vector>
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
    virtual int processSynchronBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midiMessages, int NrOfBlocksSinceLastProcessBlock);

    // parameter handling
  	void addParameter(std::vector < std::unique_ptr<juce::RangedAudioParameter>>& paramVector);
    void prepareParameter(std::unique_ptr<juce::AudioProcessorValueTreeState>&  vts);
    
    // some necessary info for the host
    int getLatency(){return m_Latency;};

	void setFFTSize(size_t newFFTSize);
    void setclosestFFTSize_ms(float fftsize_ms);
    void setPauseMode (bool mode){m_PauseMode = mode;};
    
    size_t getnextpowerof2(float fftsize_ms);

    size_t getSpectrumSize(){return m_freqsize;};
    float getSamplerate(){return m_fs;};
	void setWindowType(SpectrumAnalyzer::WindowType type){m_windowChoice = type; 
		m_leftAnalyzer.setWindowType(type); m_rightAnalyzer.setWindowType(type);};
	void setChannelMixMode(ChannelMixMode mode){m_mixMode = mode;};
	bool getMemSlice(std::vector<float>& outBlock){ return m_fifo.pop(outBlock); };

private:
	JadeSpectrogramAudioProcessor* m_processor;
    CriticalSection m_protectBlock;
    int m_Latency = 0;
    float m_fs;
    size_t m_channels;

	size_t m_fftsize;
	size_t m_freqsize;
	SpectrumAnalyzer m_leftAnalyzer;
	SpectrumAnalyzer m_rightAnalyzer;
	TwoDimBlockFreeFiFO m_fifo;
	std::vector<float> m_power;
	std::vector<float> m_perLeft;
	std::vector<float> m_perRight;
	std::vector<float> m_timeInLeft;
	std::vector<float> m_timeInRight;
    ChannelMixMode m_mixMode;
	SpectrumAnalyzer::WindowType m_windowChoice;

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
