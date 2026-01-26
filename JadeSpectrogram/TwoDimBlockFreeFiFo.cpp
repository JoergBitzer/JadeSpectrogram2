#include <memory.h>
#include <cassert>
#include "TwoDimBlockFreeFiFo.h"

TwoDimBlockFreeFiFO::TwoDimBlockFreeFiFO()
:m_initValue(0.f)
{
    m_maxCapacity_x = static_cast<int>(10*96000/128) + 1; // 10s * 96000 Hz / smallest feed (25% with 512)
    m_maxCapacity_y = 16384;
    buildMem();
}

TwoDimBlockFreeFiFO::TwoDimBlockFreeFiFO(size_t max_x, size_t max_y)
:m_maxCapacity_x(max_x), m_maxCapacity_y(max_y), m_initValue(0.f)
{ 
    buildMem();
}

bool TwoDimBlockFreeFiFO::push(const std::vector<float> &inBlock)
{
    assert(inBlock.size() == m_actSize_y);
    memcpy(m_Mem[m_writecounter].data(), inBlock.data(), m_actSize_y*sizeof(float));
    m_writecounter++;
    if (m_writecounter == m_actSize_x)
        m_writecounter = 0;

    // This will happen, if there is no reading (or reading is slower than writing)
    if (m_writecounter == m_readcounter) // m_writecounter overtook m_readcounter ==> overwrite latest data
    {
        m_readcounter++;
        if (m_readcounter == m_actSize_x)        
            m_readcounter = 0;
    }
    return true;
}

bool TwoDimBlockFreeFiFO::pop(std::vector<float> &outBlock)
{
    size_t next = m_readcounter + 1;
    if (next == m_actSize_x)
        next = 0;
    
    if (next == m_writecounter) // reading is faster than writing
        return false;
    
    assert(outBlock.size() == m_actSize_y);
    memcpy(outBlock.data(), m_Mem[m_readcounter].data(), m_actSize_y*sizeof(float));
    m_readcounter = next;
    return true;
}

size_t TwoDimBlockFreeFiFO::getNumAvailableToRead() const
{
    if (m_writecounter > m_readcounter)
        return m_writecounter - m_readcounter - 1; // always one block delay, so never access to the same memory

    if (m_readcounter > m_writecounter)
        return m_actSize_x - m_readcounter + m_writecounter - 1;
    return 0;
}

bool TwoDimBlockFreeFiFO::getBlock(std::vector<std::vector<float>> &outBlock) 
{
    size_t nrOfBlocks = outBlock.size();

    for (size_t kk = 0; kk < nrOfBlocks; kk++)
    {
        bool success = pop(outBlock[kk]);
        if (!success)
            assert("This should not happen");
    }

    return false;
}

bool TwoDimBlockFreeFiFO::setActSize(size_t act_x, size_t act_y)
{
    m_actSize_x = act_x;
    m_Mem.resize(m_actSize_x);
    m_actSize_y = act_y;
    for (size_t kk = 0; kk < m_actSize_x ; kk++)
    {
        m_Mem[kk].resize(m_actSize_y);
    }
    return true;
}

void TwoDimBlockFreeFiFO::fill(float value)
{
    size_t x_size = m_Mem.size();
    for (size_t kk = 0; kk < x_size ; kk++)
    {
        std::fill(m_Mem[kk].begin(), m_Mem[kk].end(),value);
    }
}

bool TwoDimBlockFreeFiFO::buildMem()
{
    m_Mem.resize(m_maxCapacity_x);
    assert(m_Mem.size() == m_maxCapacity_x);

    for (size_t kk = 0; kk < m_maxCapacity_x ; kk++)
    {
        m_Mem[kk].reserve(m_maxCapacity_y);
        assert(m_Mem[kk].capacity() == m_maxCapacity_y);
    }

    return true;
}

void TwoDimBlockFreeFiFO::clearMem()
{
    fill(m_initValue);
}
