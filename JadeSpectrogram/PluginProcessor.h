#pragma once
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "tools/MidiModPitchState.h"
#include "tools/PresetHandler.h"
#include "JadeSpectrogram.h"

//==============================================================================
class JadeSpectrogramAudioProcessor  : public juce::AudioProcessor, private juce::Timer
{
public:
    friend class JadeSpectrogramAudioProcessorEditor;
    //==============================================================================
    JadeSpectrogramAudioProcessor();
    ~JadeSpectrogramAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getScaleFactor(){return m_pluginScaleFactor;};
    void setScaleFactor(float newscalefactor){m_pluginScaleFactor = newscalefactor;};
    
    // Algo component and ValueTreeState must be public to be accessed by the editor
    JadeSpectrogramAudio m_algo;
    std::unique_ptr<AudioProcessorValueTreeState> m_parameterVTS;

    void setFFTSize(int fftsize){m_fftsize = fftsize;};
    bool getRunningStatus(){return isRunning;};

private:
    // reports a changed latency (FFT size switch) to the host; setLatencySamples
    // notifies the host, so it is called here on the message thread, not on the audio thread
    void timerCallback() override;
    float m_fs; // sampling rate is always needed

    //Parameterhandling
    std::vector <std::unique_ptr<RangedAudioParameter>> m_paramVector;
	PresetHandler m_presets;
    float m_pluginScaleFactor = 1.0;    
#if WITH_MIDIKEYBOARD    
    MidiKeyboardState m_keyboardState;
    MidiModPitchBendState m_wheelState;
#endif
    int m_fftsize;
    bool isRunning;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JadeSpectrogramAudioProcessor)
};
