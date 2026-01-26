#pragma once

#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "tools/AudioProcessParameter.h"
#include "tools/SynchronBlockProcessor.h"
#include "PluginSettings.h"

#include "SpectrumAnalyzer.h"
#include "TwoDimBlockFreeFiFo.h"

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
        AbsMean,
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
    void setmemoryTime_s (float memsize_s);
    void setPauseMode (bool mode){m_PauseMode = mode;};
    
    size_t getnextpowerof2(float fftsize_ms);

    int getSpectrumSize(){return m_freqsize;};
    float getSamplerate(){return m_fs;};

private:
	JadeSpectrogramAudioProcessor* m_processor;
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
    ChannelMixMode m_mode;
	SpectrumAnalyzer::WindowType m_windowChoice;

	// paramater
	jade::AudioProcessParameter<float> m_DisplayMinFreq;
	jade::AudioProcessParameter<float> m_DisplayMaxFreq;
	jade::AudioProcessParameter<float> m_DisplayMinColor;
	jade::AudioProcessParameter<float> m_DisplayMaxColor;

    bool m_PauseMode;	
};

class JadeSpectrogramGUI : public juce::Component
{
public:
	JadeSpectrogramGUI(JadeSpectrogramAudioProcessor& p, juce::AudioProcessorValueTreeState& apvts);

	void paint(juce::Graphics& g) override;
	void resized() override;
private:
	JadeSpectrogramAudioProcessor& m_processor;
    juce::AudioProcessorValueTreeState& m_apvts; 

};
