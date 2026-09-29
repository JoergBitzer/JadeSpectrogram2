/**
 * @file SynchronBlockProcessor.h
 * @author J. Bitzer @ Jade HS, BSD Licence
 * @brief class to rebuffer an JUCE AudioBuffer and MidiMessageQueue of arbitrary length to AudioBuffer of a given length
 * Useful for fft processing or faster parameter updates and modulation
 * Usage: Inherit from this class and override ProcessSynchronBlock
 * @version 2.2
 * @date 2026-09-29
 * 
 * @copyright Copyright (c) 2022
 * 
 */

// class to rebuffer an JUCE AudioBuffer and MidiMessageQueue of arbitrary length to AudioBuffer of a given length
// Useful for fft processing or faster parameter updates and modulation
// if desiredSize is < 1 (zero or negative) , the processing will be done directly without buffering
// Usage: Inherit from this class and override ProcessSynchronBlock
// (c) J. Bitzer @ Jade HS, BSD Licence
//
// Version 2.0 (only JUCE AUdioBUffer, without std::vector)
// Version 2.1 (added directthrue option and changed CriticalSection to ScopedLock (RAII))
// Version 2.2 (added ProcessingMode::Analyze: the audio passes unchanged and without delay,
//              the synchron blocks are only analysed; prepareSynchronProcessing does not
//              reallocate for smaller sizes; directthrue no longer runs the buffering too)

/* ToDO:
1) rewrite as template class for double
//*/

#pragma once
#include <JuceHeader.h>

class SynchronBlockProcessor
{
public:
    enum class ProcessingMode
    {
        Process, // default: the output is the processed synchron blocks, delayed by desiredSize (getDelay)
        Analyze  // for analyzers: the output is the unchanged input without delay, processSynchronBlock
                 // gets a copy of the input (changes to it are ignored), getDelay() is 0
    };
    SynchronBlockProcessor();
    ~SynchronBlockProcessor(){};
    /**
     * @brief preparetoprocess sets the desired blocksize for a given numer of channels
     * it is not threadsafe: call it from prepareToPlay or from the audio thread between two processBlock calls.
     * It only allocates if desiredSize (or channels) is larger than in any call before, so calling it once
     * with the maximum size in prepareToPlay makes later calls realtime safe
     * 
     * @param channels 
     * @param desiredSize 
     */
    void prepareSynchronProcessing(int channels, int desiredSize); 
    /**
     * @brief the typical JUCE call just forward the call in Processor
     * 
     * @param data 
     * @param midiMessages 
     */
    void processBlock(juce::AudioBuffer<float>& data, juce::MidiBuffer& midiMessages);
    /**
     * @brief processSynchronBlock is your new processing routine. The block will always be of size desiredSize
     * 
     * @param midiMessages 
     * @return int 
     */
    virtual int processSynchronBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midiMessages, int NrOfBlocksSinceLastProcessBlock = 0) = 0;
    /**
     * @brief Get the Delay object
     * 
     * @return int this will be DesiredSize
     */
    int getDelay();
    /**
     * @brief set Process (default) or Analyze mode, see ProcessingMode.
     * Call it before or together with prepareSynchronProcessing (not while processBlock runs)
     */
    void setProcessingMode(ProcessingMode mode){m_mode = mode;};
    ProcessingMode getProcessingMode() const {return m_mode;};
private:
    ProcessingMode m_mode = ProcessingMode::Process;
    CriticalSection m_protectBlock;
    int m_NrOfChannels;
    int m_OutBlockSize;
    int m_OutCounter;
    int m_InCounter;

    juce::AudioBuffer<float> m_memory;
    juce::AudioBuffer<float> m_block;

    MidiBuffer m_mididata;
    int m_pastSamples;
    bool m_directthrue = false;
};

class WOLA : public SynchronBlockProcessor
{
public: 
    enum class WOLAType
    {
        NoWin_over75,
        NoWin_over50,
        HannRect_over75,
        HannRect_over50,
        RectHann_over75,
        RectHann_over50,
        SqrtHann_over75,
        SqrtHann_over50,
    };
    enum class WinType
    {
        Rect,
        Hann,
        SqrtHann,
    };

    WOLA();
    ~WOLA();
    int prepareWOLAprocessing(int channels, int desiredSize, WOLAType wolalaptype = WOLAType::NoWin_over50); 
    int processSynchronBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midiMessages, int NrOfBlocksSinceLastProcessBlock);    
    virtual int processWOLA(juce::AudioBuffer<float>&, juce::MidiBuffer& midiMessages) = 0;
    int getDelay();
    int getWindow(juce::AudioBuffer<float>&, WinType wintype = WinType::Hann);
    
private:
    int m_FullBlockSize;
    int m_NrOfChannels;
    int m_InCounter;
    int m_OutCounter;
    int m_nrOfBlocks;
    WOLAType m_wolaType;

    juce::AudioBuffer<float> m_audioBlock;
    juce::AudioBuffer<float> m_analWin;
    juce::AudioBuffer<float> m_synWin;
    
    // memory blocks for 50% overlap
    juce::AudioBuffer<float> m_mem50aOut;
    juce::AudioBuffer<float> m_mem50bOut;
    juce::AudioBuffer<float> m_mem50aIn;
    juce::AudioBuffer<float> m_mem50bIn;

    // memory blocks for 75% overlap
    juce::AudioBuffer<float> m_mem25aIn;
    juce::AudioBuffer<float> m_mem25bIn;
    juce::AudioBuffer<float> m_mem25cIn;
    juce::AudioBuffer<float> m_mem25dIn;

    juce::AudioBuffer<float> m_mem25aOut;
    juce::AudioBuffer<float> m_mem25bOut;
    juce::AudioBuffer<float> m_mem25cOut;
    juce::AudioBuffer<float> m_mem25dOut;


};