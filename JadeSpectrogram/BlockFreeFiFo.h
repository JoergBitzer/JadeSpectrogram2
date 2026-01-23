#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include <memory.h>

class BlockFreeFiFo
{
public:
    BlockFreeFiFo(size_t capacity):m_memory(capacity), m_fifo(capacity)
    {
        m_fifo.reset();
    }
    ~BlockFreeFiFo(){};
    void setCapacity(size_t capacity, size_t nrofbytesperelement = sizeof(float))
    {
        m_memory.setSize(capacity*nrofbytesperelement);
        m_fifo.setTotalSize(capacity);
        m_fifo.reset();
    }
    int read (std::vector<float> & data)
    {
        size_t nrofelements = data.size();
        //size_t nrofbytes = nrofelements * sizeof(float);
        auto readHandle = m_fifo.read (nrofelements);
        if (readHandle.blockSize1 + readHandle.blockSize2 != nrofelements)
            return -1;
        memcpy(data.data(), (float*) m_memory.getData() + readHandle.startIndex1, readHandle.blockSize1 * sizeof(float));
        memcpy(data.data() + readHandle.blockSize1, (float*) m_memory.getData() + readHandle.startIndex2, readHandle.blockSize2 * sizeof(float));
        return 0;
    };
    int read (std::vector<float> & data, size_t nrofelements)
    {
        //size_t nrofbytes = nrofelements * sizeof(float);
        auto readHandle = m_fifo.read (nrofelements);
        if (readHandle.blockSize1 + readHandle.blockSize2 != nrofelements)
            return -1;
        memcpy(data.data(), (float*) m_memory.getData() + readHandle.startIndex1, readHandle.blockSize1 * sizeof(float));
        memcpy(data.data() + readHandle.blockSize1, (float*) m_memory.getData() + readHandle.startIndex2, readHandle.blockSize2 * sizeof(float));
        return 0;
    };
    int write (const std::vector<float> & data)
    {
        size_t nrofelements = data.size();
        //size_t nrofbytes = nrofelements * sizeof(float);
        auto writeHandle = m_fifo.write (nrofelements);
        memcpy((float*) m_memory.getData() + writeHandle.startIndex1, data.data(), writeHandle.blockSize1 * sizeof(float));
        memcpy((float*) m_memory.getData() + writeHandle.startIndex2, data.data() + writeHandle.blockSize1, writeHandle.blockSize2 * sizeof(float));
        return 0;
    };
    void clear()
    {
        m_fifo.reset();
        m_memory.fillWith(0);
    }

private:
    juce::MemoryBlock m_memory;
    juce::AbstractFifo m_fifo;
    
};
