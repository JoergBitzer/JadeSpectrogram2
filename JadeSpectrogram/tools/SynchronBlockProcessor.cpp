#define _USE_MATH_DEFINES
#include <cmath>
#include <fstream>
#include <iostream>


#include "SynchronBlockProcessor.h"

SynchronBlockProcessor::SynchronBlockProcessor()
:m_NrOfChannels(2),m_OutBlockSize(256)
{
    prepareSynchronProcessing(m_NrOfChannels,m_OutBlockSize);
}
void SynchronBlockProcessor::prepareSynchronProcessing(int channels, int desiredSize)
{
    ScopedLock lock(m_protectBlock);
    //m_protectBlock.enter();
    m_OutBlockSize = desiredSize;
    m_NrOfChannels = channels;
    m_memory.setSize(m_NrOfChannels,2*m_OutBlockSize);
    m_memory.clear();
    m_block.setSize(m_NrOfChannels,m_OutBlockSize);
    m_block.clear();
    m_OutCounter = 0;
    m_InCounter = 0;
    m_mididata.clear();
    m_pastSamples = 0;
    if (desiredSize < 1)
        m_directthrue = true;
    else
        m_directthrue = false;
    //m_protectBlock.exit();
}
void SynchronBlockProcessor::processBlock(juce::AudioBuffer<float>& data, juce::MidiBuffer& midiMessages)
{
    ScopedLock lock(m_protectBlock);
    int nrofBlockProcessed = 0;
    if (m_directthrue == true)
    {
        processSynchronBlock(data, midiMessages, nrofBlockProcessed);
    }
    // m_protectBlock.enter();
    auto readdatapointers = data.getArrayOfReadPointers();
    auto writedatapointers = data.getArrayOfWritePointers();
    int nrOfInputSamples = data.getNumSamples();
    int nrOfChannels = data.getNumChannels();
    auto blockreaddatapointers = m_block.getArrayOfReadPointers();
    auto blockwritedatapointers = m_block.getArrayOfWritePointers();
    auto memreaddatapointers = m_memory.getArrayOfReadPointers();
    auto memwritedatapointers = m_memory.getArrayOfWritePointers();

    for (auto kk = 0; kk < nrOfInputSamples; ++kk)
    {
        for (auto cc = 0; cc < nrOfChannels; ++cc)
        {
            blockwritedatapointers[cc][m_InCounter] = readdatapointers[cc][kk];
            writedatapointers[cc][kk] = memreaddatapointers[cc][m_OutCounter];
        }
        m_InCounter++;
        if (m_InCounter == m_OutBlockSize)
        {
            m_InCounter = 0;
            if (kk < m_OutBlockSize)
                m_mididata.addEvents(midiMessages,0, kk ,m_pastSamples);
            else
            {
                m_mididata.addEvents(midiMessages,kk-m_OutBlockSize,m_OutBlockSize,-(kk-m_OutBlockSize));
            }

            processSynchronBlock(m_block, m_mididata, nrofBlockProcessed);
            nrofBlockProcessed++;
            m_mididata.clear();
            m_pastSamples = 0;

                // copy block into mem
            if (m_OutCounter < m_OutBlockSize)
            {
                for (auto channel = 0; channel < nrOfChannels; ++channel)
                {
                    for (auto sample = 0; sample < m_OutBlockSize; ++sample)
                    {
                        memwritedatapointers[channel][sample + m_OutBlockSize] = blockreaddatapointers[channel][sample];
                    }
                }
 
            }
            else
            {
                for (auto channel = 0; channel < nrOfChannels; ++channel)
                {
                    for (auto sample = 0; sample < m_OutBlockSize; ++sample)
                    {
                        memwritedatapointers[channel][sample] = blockreaddatapointers[channel][sample];
                    }
                }
            }
            
        }
        m_OutCounter++;
        if (m_OutCounter == 2*m_OutBlockSize)
            m_OutCounter = 0;
    }
    if (nrofBlockProcessed>0)
    {
        int lastMidiSamples = nrOfInputSamples- m_InCounter;
        m_mididata.addEvents(midiMessages,lastMidiSamples, m_InCounter,-lastMidiSamples);
        m_pastSamples += m_InCounter;

    }
    else
    {
        m_mididata.addEvents(midiMessages,0,nrOfInputSamples,m_pastSamples);
        m_pastSamples += nrOfInputSamples;
    }
    //m_protectBlock.exit();
}

int SynchronBlockProcessor::getDelay()
{
    if (m_directthrue)
        return 0;
    else
        return m_OutBlockSize;
}
/* // Midi Debugcode
    auto a = midiMessages.getNumEvents();
    auto b = midiMessages.getFirstEventTime();
    auto c = midiMessages.getLastEventTime();    

    if (a > 0)
    {    
        DBG(String("Anz= ") + String(a) + String("Start= ") + String(b) + String("End= ") + String(c) );
    }


            auto a = m_mididata.getNumEvents();
            auto b = m_mididata.getFirstEventTime();
            auto c = m_mididata.getLastEventTime();    

            if (a > 0)
            {    
                DBG(String("BlockFkt Anz= ") + String(a) + String("Start= ") + String(b) + String("End= ") + String(c) );
                DBG(String("InCounter= " + String(m_InCounter) + "kk = " + String(kk) ));
            }
//*/

WOLA::WOLA()
:m_FullBlockSize(1024),m_NrOfChannels(2),m_InCounter(0),m_OutCounter(0),m_nrOfBlocks(3),m_wolaType(WOLAType::SqrtHann_over75)
{
    prepareWOLAprocessing(m_NrOfChannels,m_FullBlockSize);
}

WOLA::~WOLA()
{
}

int WOLA::prepareWOLAprocessing(int channels, int desiredSize, WOLAType wolalaptype)
{
    m_NrOfChannels = channels;
    m_FullBlockSize = desiredSize;
    m_wolaType = wolalaptype;

    m_analWin.setSize(1,m_FullBlockSize);
    m_analWin.clear();
    m_synWin.setSize(1,m_FullBlockSize);
    m_synWin.clear();
    m_audioBlock.setSize(m_NrOfChannels,m_FullBlockSize);
    m_audioBlock.clear();
    m_mem50aOut.setSize(m_NrOfChannels,m_FullBlockSize/2);
    m_mem50aOut.clear();
    m_mem50bOut.setSize(m_NrOfChannels,m_FullBlockSize/2);
    m_mem50bOut.clear();
    m_mem50aIn.setSize(m_NrOfChannels,m_FullBlockSize/2);
    m_mem50aIn.clear();
    m_mem50bIn.setSize(m_NrOfChannels,m_FullBlockSize/2);
    m_mem50bIn.clear();
    // 75% Overlap
    m_mem25aIn.setSize(m_NrOfChannels,m_FullBlockSize/4);
    m_mem25aIn.clear();
    m_mem25bIn.setSize(m_NrOfChannels,m_FullBlockSize/4);
    m_mem25bIn.clear();
    m_mem25cIn.setSize(m_NrOfChannels,m_FullBlockSize/4);
    m_mem25cIn.clear();
    m_mem25dIn.setSize(m_NrOfChannels,m_FullBlockSize/4);
    m_mem25dIn.clear();
    m_mem25aOut.setSize(m_NrOfChannels,3*m_FullBlockSize/4);
    m_mem25aOut.clear();
    m_mem25bOut.setSize(m_NrOfChannels,3*m_FullBlockSize/4);
    m_mem25bOut.clear();
    m_mem25cOut.setSize(m_NrOfChannels,3*m_FullBlockSize/4);
    m_mem25cOut.clear();
    m_mem25dOut.setSize(m_NrOfChannels,3*m_FullBlockSize/4);
    m_mem25dOut.clear();
    
    m_OutCounter = 0;
    m_InCounter = 0;

    switch (m_wolaType)
    {
    case WOLAType::NoWin_over75:
        getWindow(m_analWin, WinType::Rect);
        getWindow(m_synWin, WinType::Rect);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/4);
        break;
    case WOLAType::NoWin_over50:
        getWindow(m_analWin, WinType::Rect);
        getWindow(m_synWin, WinType::Rect);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/2);
        break;
    case WOLAType::HannRect_over75: 
        getWindow(m_analWin, WinType::Hann);
        getWindow(m_synWin, WinType::Rect);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/4);
        break;
    case WOLAType::HannRect_over50:
        getWindow(m_analWin, WinType::Hann);
        getWindow(m_synWin, WinType::Rect);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/2);
        break;
    case WOLAType::RectHann_over75: 
        getWindow(m_synWin, WinType::Hann);
        getWindow(m_analWin, WinType::Rect);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/4);
        break;
    case WOLAType::RectHann_over50:
        getWindow(m_synWin, WinType::Hann);
        getWindow(m_analWin, WinType::Rect);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/2);
        break;
    case WOLAType::SqrtHann_over75:
        getWindow(m_analWin, WinType::SqrtHann);
        getWindow(m_synWin, WinType::SqrtHann);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/4);
        break;
    case WOLAType::SqrtHann_over50:
        getWindow(m_analWin, WinType::SqrtHann);
        getWindow(m_synWin, WinType::SqrtHann);
        prepareSynchronProcessing(m_NrOfChannels,m_FullBlockSize/2);

        break;
    default:
        break;
    }


    return 0;
}

int WOLA::processSynchronBlock(juce::AudioBuffer<float> &inBlock, juce::MidiBuffer &midiMessages, int NrOfBlocksSinceLastProcessBlock)
{
    juce::ignoreUnused(NrOfBlocksSinceLastProcessBlock);
    //m_protectBlock.enter();
    int nrOfChannels = inBlock.getNumChannels();
    
    for (auto kk = 0; kk < nrOfChannels; ++kk)
    {
        if ((m_wolaType == WOLAType::NoWin_over50) | (m_wolaType == WOLAType::SqrtHann_over50) | (m_wolaType == WOLAType::HannRect_over50) | (m_wolaType == WOLAType::RectHann_over50))
        {
            m_audioBlock.copyFrom(kk,m_FullBlockSize/2,inBlock,kk,0,m_FullBlockSize/2);
            m_audioBlock.copyFrom(kk,0,m_mem50aIn,kk,0,m_FullBlockSize/2);
            m_mem50aIn.copyFrom(kk,0,inBlock,kk,0,m_FullBlockSize/2);

        }
        else if ((m_wolaType == WOLAType::NoWin_over75) | (m_wolaType == WOLAType::SqrtHann_over75) | (m_wolaType == WOLAType::HannRect_over75)| (m_wolaType == WOLAType::RectHann_over75))
        {
            m_audioBlock.copyFrom(kk,3*m_FullBlockSize/4,inBlock,kk,0,m_FullBlockSize/4);
            m_audioBlock.copyFrom(kk,2*m_FullBlockSize/4,m_mem25aIn, kk,0,m_FullBlockSize/4);
            m_audioBlock.copyFrom(kk,1*m_FullBlockSize/4,m_mem25bIn, kk,0,m_FullBlockSize/4);
            m_audioBlock.copyFrom(kk,0*m_FullBlockSize/4,m_mem25cIn, kk,0,m_FullBlockSize/4);
            // This can be solved in a better way by changing pointers
            m_mem25cIn.copyFrom(kk,0,m_mem25bIn,kk,0,m_FullBlockSize/4);
            m_mem25bIn.copyFrom(kk,0,m_mem25aIn,kk,0,m_FullBlockSize/4);
            m_mem25aIn.copyFrom(kk,0,inBlock,kk,0,m_FullBlockSize/4);

        }
        // apply window
        auto winptr = m_analWin.getReadPointer(0);
        auto audioptr = m_audioBlock.getWritePointer(kk);
        for (auto ss = 0; ss < m_FullBlockSize; ++ss)
        {
            audioptr[ss] *= winptr[ss];
        }
    }
    // processing

/*     char filename[256];
    sprintf(filename,"./tester/WOLATest/OneAudioBlock.txt");

    std::ofstream file1(filename);

    file1 << "data_ab = np.array([";
    auto dataptr = m_audioBlock.getReadPointer(0);
    for (auto kk = 0; kk < m_audioBlock.getNumSamples(); ++kk)
    {
        file1 << dataptr[kk] << ", ";
    }
    file1 << "])" << std::endl;
    file1.close();
 */

    processWOLA(m_audioBlock,midiMessages);

    // defines outputs
    for (auto kk = 0; kk < nrOfChannels; ++kk)
    {
        // apply sythesis window
        auto winptr = m_synWin.getReadPointer(0);
        auto audioptr = m_audioBlock.getWritePointer(kk);
        for (auto ss = 0; ss < m_FullBlockSize; ++ss)
        {
            audioptr[ss] *= winptr[ss];
        }

        if ((m_wolaType == WOLAType::NoWin_over50) | (m_wolaType == WOLAType::SqrtHann_over50) | (m_wolaType == WOLAType::HannRect_over50) | (m_wolaType == WOLAType::RectHann_over50))
        {
            inBlock.copyFrom(kk,0,m_audioBlock,kk,0,m_FullBlockSize/2);
            inBlock.addFrom(kk,0,m_mem50aOut,kk,0,m_FullBlockSize/2);
            m_mem50aOut.copyFrom(kk,0,m_audioBlock,kk,m_FullBlockSize/2,m_FullBlockSize/2);
        }
        else if ((m_wolaType == WOLAType::NoWin_over75) | (m_wolaType == WOLAType::SqrtHann_over75) | (m_wolaType == WOLAType::HannRect_over75) | (m_wolaType == WOLAType::RectHann_over75))
        {
            inBlock.copyFrom(kk,0,m_audioBlock,kk,0,m_FullBlockSize/4);
            int whichPartCounter = (m_OutCounter+2)%m_nrOfBlocks;
            inBlock.addFrom(kk,0,m_mem25aOut,kk,whichPartCounter*m_FullBlockSize/4,m_FullBlockSize/4);
            whichPartCounter = (m_OutCounter+1)%m_nrOfBlocks;
            inBlock.addFrom(kk,0,m_mem25bOut,kk,whichPartCounter*m_FullBlockSize/4,m_FullBlockSize/4);
            whichPartCounter = (m_OutCounter)%m_nrOfBlocks;
            inBlock.addFrom(kk,0,m_mem25cOut,kk,whichPartCounter*m_FullBlockSize/4,m_FullBlockSize/4);

            if (m_OutCounter == 0)
            {
                m_mem25aOut.copyFrom(kk,0,m_audioBlock,kk,m_FullBlockSize/4,3*m_FullBlockSize/4);
            }
            else if (m_OutCounter == 1)
            {
                m_mem25bOut.copyFrom(kk,0,m_audioBlock,kk,m_FullBlockSize/4,3*m_FullBlockSize/4);
            }
            else if (m_OutCounter == 2)
            {
                m_mem25cOut.copyFrom(kk,0,m_audioBlock,kk,m_FullBlockSize/4,3*m_FullBlockSize/4);
            }
            
        }
    }
    m_OutCounter++;
    if (m_OutCounter == m_nrOfBlocks)
    {
        m_OutCounter = 0;
    }

    switch (m_wolaType)
    {
    case WOLAType::NoWin_over75:
        inBlock.applyGain(0.25f);
        break;
    case WOLAType::NoWin_over50:
        inBlock.applyGain(0.5f);
        break;
    case WOLAType::HannRect_over75: 
        inBlock.applyGain(0.5f);
        break;
    case WOLAType::HannRect_over50:
        break;
    case WOLAType::RectHann_over75: 
        inBlock.applyGain(0.5f);
        break;
    case WOLAType::RectHann_over50:
        break;
    case WOLAType::SqrtHann_over75:
        inBlock.applyGain(0.5f);
        break;
    case WOLAType::SqrtHann_over50:

        break;
    default:
        break;
    }

    return 0;
}

int WOLA::getDelay()
{
    return m_FullBlockSize;
}

int WOLA::getWindow(juce::AudioBuffer<float> &win, WinType wintype)
{
    int len = win.getNumSamples();
    auto winptr = win.getWritePointer(0);
    if (wintype == WinType::SqrtHann)
    {
        for (auto kk = 0; kk < len; ++kk)
        {
            winptr[kk] = sqrtf(0.5f*(1.f - static_cast<float>(cos(2.f*kk*M_PI / (len - 1)))));
        }
    }
    else if (wintype == WinType::Hann)
    {
        for (auto kk = 0; kk < len; ++kk)
        {
            winptr[kk] = 0.5f*(1.f - static_cast<float>(cos(2.f*kk*M_PI / (len - 1))));
        }
    }
    else if (wintype == WinType::Rect)
    {
        for (auto kk = 0; kk < len; ++kk)
        {
            winptr[kk] = 1.f;
        }
    }

    return 0;
}
